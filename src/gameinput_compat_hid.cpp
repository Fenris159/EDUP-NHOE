#include "edtrace/gameinput_compat_hid.hpp"
#include <bcrypt.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace edtrace { namespace {
constexpr DWORD vendor_id = 0x045e, product_id = 0x0b22;
constexpr std::array<unsigned char, 32> descriptor_sha{{
  0x54,0x3e,0xe3,0x8c,0x91,0x05,0x6a,0x03,0xde,0xc4,0x54,0xf9,0x7a,0x63,0x02,0x1c,
  0x6d,0x3b,0xb9,0x04,0x71,0x8d,0xef,0xdd,0x9c,0x98,0xb5,0x9e,0xd0,0x6d,0xa5,0xdc}};
constexpr std::array<unsigned char, 9> zero_report{{3,15,0,0,0,0,0,0,0}};
SRWLOCK lifecycle = SRWLOCK_INIT, state_lock = SRWLOCK_INIT;
GameInputCompatHidStats stats{};
HANDLE worker = nullptr, wake = nullptr, device = INVALID_HANDLE_VALUE;
std::atomic<bool> running{false}, stop_requested{false}, poll_requested{false}, ready{false};
bool pending = false, zero_pending = false;
std::array<unsigned char, 9> pending_report{};
std::string pinned_address;
std::wstring opened_name;

struct Endpoint { std::wstring name; std::string address; };
bool ReadFileBytes(const std::wstring& path, std::vector<unsigned char>& bytes, DWORD limit) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  bytes.resize(limit + 1); DWORD count = 0;
  const bool ok = ReadFile(file, bytes.data(), limit + 1, &count, nullptr) != 0 && count <= limit;
  CloseHandle(file);
  if (!ok) { bytes.clear(); return false; }
  bytes.resize(count); return true;
}
bool Field(const std::string& text, const char* key, std::string& result) {
  std::size_t at = 0; bool found = false;
  while (at < text.size()) {
    const auto end = text.find('\n', at);
    const auto length = (end == std::string::npos ? text.size() : end) - at;
    const std::string line = text.substr(at, length);
    const auto key_size = std::strlen(key);
    if (line.compare(0, key_size, key) == 0 && line.size() > key_size && line[key_size] == '=') {
      if (found) return false;
      result = line.substr(key_size + 1); found = true;
    }
    if (end == std::string::npos) break;
    at = end + 1;
  }
  return found;
}
bool NormalizeAddress(std::string& value) {
  if (value.size() != 17) return false;
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (i % 3 == 2) { if (value[i] != ':') return false; }
    else {
      if (value[i] >= 'A' && value[i] <= 'F') value[i] = static_cast<char>(value[i] + ('a' - 'A'));
      if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f'))) return false;
    }
  }
  return value != "00:00:00:00:00:00" && value != "ff:ff:ff:ff:ff:ff";
}
bool Identity(const std::wstring& name, Endpoint& endpoint) {
  std::vector<unsigned char> data;
  if (!ReadFileBytes(L"Z:\\sys\\class\\hidraw\\" + name + L"\\device\\uevent", data, 4096)) return false;
  const std::string text(data.begin(), data.end());
  std::string driver, hid, address;
  if (!Field(text, "DRIVER", driver) || driver != "xpadneo" ||
      !Field(text, "HID_ID", hid) || hid != "0005:0000045E:00000B22" ||
      !Field(text, "HID_UNIQ", address) || !NormalizeAddress(address)) return false;
  endpoint = {name, address}; return true;
}
bool Descriptor(const std::wstring& name) {
  std::vector<unsigned char> data;
  if (!ReadFileBytes(L"Z:\\sys\\class\\hidraw\\" + name + L"\\device\\report_descriptor", data, 464) || data.size() != 464) return false;
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
  std::array<unsigned char, 32> hash{};
  std::array<unsigned char, 4096> object{};
  DWORD object_size = 0, returned = 0;
  auto status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size),
                                  sizeof(object_size), &returned, 0);
  BCRYPT_HASH_HANDLE handle = nullptr;
  if (status >= 0 && returned == sizeof(object_size) && object_size <= object.size())
    status = BCryptCreateHash(algorithm, &handle, object.data(), object_size, nullptr, 0, 0);
  else status = static_cast<NTSTATUS>(0xc000000d);
  if (status >= 0) status = BCryptHashData(handle, data.data(), static_cast<ULONG>(data.size()), 0);
  if (status >= 0) status = BCryptFinishHash(handle, hash.data(), static_cast<ULONG>(hash.size()), 0);
  if (handle) BCryptDestroyHash(handle);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  return status >= 0 && hash == descriptor_sha;
}
void PublishError(DWORD error) noexcept {
  AcquireSRWLockExclusive(&state_lock); stats.error = error; stats.ready = false;
  pending = false; zero_pending = false; ReleaseSRWLockExclusive(&state_lock);
  ready.store(false, std::memory_order_release);
}
bool WriteReport(const std::array<unsigned char, 9>& report) noexcept {
  DWORD written = 0; SetLastError(ERROR_SUCCESS);
  const bool ok = device != INVALID_HANDLE_VALUE &&
      WriteFile(device, report.data(), static_cast<DWORD>(report.size()), &written, nullptr) && written == report.size();
  DWORD error = ok ? ERROR_SUCCESS : GetLastError();
  if (!ok && !error) error = ERROR_WRITE_FAULT;
  AcquireSRWLockExclusive(&state_lock);
  stats.last_write_error = error;
  if (ok) { ++stats.writes; stats.last_report = report; if (report == zero_report) ++stats.stops; }
  else ++stats.write_failures;
  ReleaseSRWLockExclusive(&state_lock); return ok;
}
void CloseDevice(bool quiet) noexcept {
  PublishError(ERROR_NOT_READY);
  if (device != INVALID_HANDLE_VALUE) { if (quiet) WriteReport(zero_report); CloseHandle(device); }
  device = INVALID_HANDLE_VALUE; opened_name.clear();
}
DWORD Discover() {
  WIN32_FIND_DATAW found{};
  HANDLE search = FindFirstFileW(L"Z:\\sys\\class\\hidraw\\hidraw*", &found);
  if (search == INVALID_HANDLE_VALUE) return ERROR_NOT_FOUND;
  std::vector<Endpoint> endpoints;
  do {
    Endpoint endpoint;
    if (Identity(found.cFileName, endpoint)) endpoints.push_back(endpoint);
  } while (FindNextFileW(search, &found));
  FindClose(search);
  if (endpoints.empty()) return ERROR_NOT_FOUND;
  if (endpoints.size() != 1) return ERROR_DUP_NAME;
  const auto& candidate = endpoints.front();
  if (!pinned_address.empty() && pinned_address != candidate.address) return ERROR_DEVICE_NOT_CONNECTED;
  if (!Descriptor(candidate.name)) {
    AcquireSRWLockExclusive(&state_lock); ++stats.descriptor_failures; ReleaseSRWLockExclusive(&state_lock);
    return ERROR_REVISION_MISMATCH;
  }
  if (device != INVALID_HANDLE_VALUE && opened_name == candidate.name) return ERROR_SUCCESS;
  CloseDevice(true);
  const std::wstring path = L"Z:\\dev\\" + candidate.name;
  HANDLE opened = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, 0, nullptr);
  if (opened == INVALID_HANDLE_VALUE) return GetLastError();
  Endpoint after;
  if (GetFileType(opened) != FILE_TYPE_CHAR || !Identity(candidate.name, after) ||
      after.address != candidate.address || !Descriptor(candidate.name)) {
    CloseHandle(opened);
    AcquireSRWLockExclusive(&state_lock); ++stats.identity_failures; ReleaseSRWLockExclusive(&state_lock);
    return ERROR_REVISION_MISMATCH;
  }
  device = opened; opened_name = candidate.name;
  if (pinned_address.empty()) pinned_address = candidate.address;
  AcquireSRWLockExclusive(&state_lock);
  std::copy(candidate.address.begin(), candidate.address.end(), stats.controller_address.begin());
  const auto count = std::min(path.size(), stats.device_path.size() - 1);
  std::copy_n(path.begin(), count, stats.device_path.begin()); stats.device_path[count] = L'\0';
  ++stats.reconnects;
  ReleaseSRWLockExclusive(&state_lock); return ERROR_SUCCESS;
}
void Refresh() noexcept {
  DWORD error = ERROR_NOT_ENOUGH_MEMORY;
  try { error = Discover(); } catch (...) { error = ERROR_NOT_ENOUGH_MEMORY; }
  if (error) { CloseDevice(true); PublishError(error); }
  else {
    AcquireSRWLockExclusive(&state_lock); stats.error = ERROR_SUCCESS; stats.ready = true;
    ReleaseSRWLockExclusive(&state_lock); ready.store(true, std::memory_order_release);
  }
}
std::array<unsigned char, 9> Encode(float low, float high, float left, float right, unsigned& invalid) noexcept {
  const auto byte = [&invalid](float value) -> unsigned char {
    if (!std::isfinite(value)) { ++invalid; return 0; }
    if (value < 0) value = 0; else if (value > 1) value = 1;
    return static_cast<unsigned char>(value * 100.0f + 0.5f);
  };
  std::array<unsigned char, 9> report{{3,15,byte(left),byte(right),byte(low),byte(high),20,0,0}};
  if (!report[2] && !report[3] && !report[4] && !report[5]) report[6] = 0;
  return report;
}
DWORD WINAPI Run(void*) noexcept {
  for (;;) {
    WaitForSingleObject(wake, 250);
    if (stop_requested.load(std::memory_order_acquire)) break;
    if (poll_requested.exchange(false, std::memory_order_acq_rel)) Refresh();
    std::array<unsigned char, 9> report{}; bool have = false, have_zero = false;
    AcquireSRWLockExclusive(&state_lock);
    have_zero = zero_pending; zero_pending = false;
    if (pending) { report = pending_report; pending = false; have = true; }
    ReleaseSRWLockExclusive(&state_lock);
    if ((!have && !have_zero) || !ready.load(std::memory_order_acquire)) continue;
    Endpoint identity;
    bool valid = false;
    try { valid = Identity(opened_name, identity) && identity.address == pinned_address; } catch (...) { valid = false; }
    if (!valid) {
      AcquireSRWLockExclusive(&state_lock); ++stats.identity_failures; ReleaseSRWLockExclusive(&state_lock);
      CloseDevice(true); PublishError(ERROR_DEVICE_NOT_CONNECTED); continue;
    }
    if (stop_requested.load(std::memory_order_acquire)) break;
    if (have_zero && !WriteReport(zero_report)) { CloseDevice(true); PublishError(ERROR_WRITE_FAULT); continue; }
    if (stop_requested.load(std::memory_order_acquire)) break;
    if (have && !WriteReport(report)) { CloseDevice(true); PublishError(ERROR_WRITE_FAULT); }
  }
  CloseDevice(true); return 0;
}
}  // namespace

DWORD GameInputCompatHidStart(DWORD vendor, DWORD product) noexcept {
  AcquireSRWLockExclusive(&lifecycle);
  if (vendor != vendor_id || product != product_id) { ReleaseSRWLockExclusive(&lifecycle); return ERROR_NOT_SUPPORTED; }
  if (running.load(std::memory_order_acquire)) {
    AcquireSRWLockShared(&state_lock); const DWORD error = stats.error; ReleaseSRWLockShared(&state_lock);
    ReleaseSRWLockExclusive(&lifecycle); return error;
  }
  stop_requested.store(false, std::memory_order_release); poll_requested.store(false, std::memory_order_release);
  pinned_address.clear(); Refresh();
  wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  DWORD error = wake ? ERROR_SUCCESS : GetLastError();
  if (wake) { worker = CreateThread(nullptr, 0, Run, nullptr, 0, nullptr); if (!worker) error = GetLastError(); }
  if (error) { if (wake) CloseHandle(wake); wake = nullptr; CloseDevice(false); PublishError(error); }
  else {
    running.store(true, std::memory_order_release);
    AcquireSRWLockExclusive(&state_lock); stats.started = true; error = stats.error; ReleaseSRWLockExclusive(&state_lock);
  }
  ReleaseSRWLockExclusive(&lifecycle); return error;
}
void GameInputCompatHidPoll() noexcept {
  AcquireSRWLockShared(&lifecycle);
  if (running.load(std::memory_order_acquire) && !stop_requested.load(std::memory_order_acquire)) {
    poll_requested.store(true, std::memory_order_release); SetEvent(wake);
  }
  ReleaseSRWLockShared(&lifecycle);
}
bool GameInputCompatHidReady() noexcept { return running.load(std::memory_order_acquire) && ready.load(std::memory_order_acquire); }
bool GameInputCompatHidSubmit(float low, float high, float left, float right) noexcept {
  unsigned invalid = 0; const auto report = Encode(low, high, left, right, invalid);
  AcquireSRWLockShared(&lifecycle);
  bool accepted = false;
  AcquireSRWLockExclusive(&state_lock);
  if (running.load(std::memory_order_acquire) && ready.load(std::memory_order_acquire) && !stop_requested.load(std::memory_order_acquire)) {
    if (report == zero_report) { zero_pending = true; pending = false; }
    else { pending_report = report; pending = true; }
    ++stats.queued; stats.invalid_channels += invalid; accepted = true;
  }
  ReleaseSRWLockExclusive(&state_lock);
  if (accepted) SetEvent(wake);
  ReleaseSRWLockShared(&lifecycle); return accepted;
}
void GameInputCompatHidStop() noexcept {
  AcquireSRWLockExclusive(&lifecycle);
  stop_requested.store(true, std::memory_order_release); ready.store(false, std::memory_order_release);
  if (worker) {
    SetEvent(wake);
    DWORD joined = WaitForSingleObject(worker, 1000);
    if (joined != WAIT_OBJECT_0) { CancelSynchronousIo(worker); joined = WaitForSingleObject(worker, 4000); }
    if (joined != WAIT_OBJECT_0) {
      // Never close handles that may still be in use or claim ownership has
      // ended. The pinned caller may retry Stop; process exit is final cleanup.
      AcquireSRWLockExclusive(&state_lock); stats.error = ERROR_TIMEOUT; stats.ready = false;
      pending = false; zero_pending = false; ReleaseSRWLockExclusive(&state_lock);
      ReleaseSRWLockExclusive(&lifecycle); return;
    }
    CloseHandle(worker); worker = nullptr;
  }
  else CloseDevice(true);
  if (wake) { CloseHandle(wake); wake = nullptr; }
  running.store(false, std::memory_order_release);
  AcquireSRWLockExclusive(&state_lock); stats.started = false; stats.ready = false; pending = false; zero_pending = false; ReleaseSRWLockExclusive(&state_lock);
  ReleaseSRWLockExclusive(&lifecycle);
}
GameInputCompatHidStats GameInputCompatHidGetStats() noexcept {
  AcquireSRWLockShared(&state_lock); const auto result = stats; ReleaseSRWLockShared(&state_lock); return result;
}
}  // namespace edtrace
