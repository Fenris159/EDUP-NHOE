#include "edtrace/sco_feedback.hpp"

#include <windows.h>
#include <shlobj.h>
#include <array>
#include <memory>

namespace edtrace {
namespace {
constexpr std::uint64_t kPollMs = 50;
constexpr std::uint64_t kReadGraceMs = 250;
constexpr wchar_t kStatusSuffix[] = L"\\Frontier Developments\\Elite Dangerous\\Status.json";
std::uint64_t Ticks(FILETIME time) noexcept {
  return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}
std::wstring SavedGames() {
  const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  PWSTR result = nullptr;
  const auto known = SUCCEEDED(initialized) || initialized == RPC_E_CHANGED_MODE ?
      SHGetKnownFolderPath(FOLDERID_SavedGames, KF_FLAG_DONT_VERIFY, nullptr, &result) : E_FAIL;
  if (SUCCEEDED(initialized)) CoUninitialize();
  const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> folder(result, &CoTaskMemFree);
  if (SUCCEEDED(known) && folder && *folder) return folder.get();
  std::array<wchar_t, 32768> profile{};
  const auto size = GetEnvironmentVariableW(L"USERPROFILE", profile.data(), static_cast<DWORD>(profile.size()));
  return size && size < profile.size() ? std::wstring(profile.data(), size) + L"\\Saved Games" : std::wstring{};
}
std::optional<ScoStatus> ReadStatus(const wchar_t* path, std::uint64_t earliest) noexcept {
  const HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (file == INVALID_HANDLE_VALUE) return {};
  std::array<char, 4096> buffer{};
  BY_HANDLE_FILE_INFORMATION before{}, after{};
  DWORD read = 0;
  const bool complete = GetFileInformationByHandle(file, &before) &&
      !(before.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
      !before.nFileSizeHigh && before.nFileSizeLow && before.nFileSizeLow <= buffer.size() &&
      Ticks(before.ftLastWriteTime) >= earliest &&
      ReadFile(file, buffer.data(), before.nFileSizeLow, &read, nullptr) &&
      read == before.nFileSizeLow && GetFileInformationByHandle(file, &after) &&
      before.nFileSizeLow == after.nFileSizeLow && !after.nFileSizeHigh &&
      Ticks(before.ftLastWriteTime) == Ticks(after.ftLastWriteTime);
  CloseHandle(file);
  return complete ? ParseScoStatus({buffer.data(), read}) : std::nullopt;
}
}  // namespace

bool ScoFeedback::Start(float strength) noexcept {
  strength_ = HapticUnit(strength);
  if (strength_ == 0) return false;
  try {
    const auto folder = SavedGames();
    // Keep status reads on a local volume. A redirected network path could
    // stall the motor worker past a stop command.
    std::array<wchar_t, MAX_PATH> volume{};
    if (folder.empty() || !GetVolumePathNameW(folder.c_str(), volume.data(), static_cast<DWORD>(volume.size())) ||
        GetDriveTypeW(volume.data()) != DRIVE_FIXED) return false;
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return false;
    earliest_write_ = Ticks(created);
    path_ = folder + kStatusSuffix;
    return true;
  } catch (...) {
    return false;
  }
}
HapticFrame ScoFeedback::Tick(bool ready, std::uint64_t now) noexcept {
  if (path_.empty()) return {};
  if (!ready) {
    haptics_.Reset();
    last_success_.reset();
    next_poll_ = 0;
    return {};
  }
  if (now >= next_poll_) {
    next_poll_ = now + kPollMs;
    if (const auto status = ReadStatus(path_.c_str(), earliest_write_)) {
      haptics_.Observe(*status, now);
      last_success_ = now;
    } else if (last_success_ && now - *last_success_ >= kReadGraceMs) {
      haptics_.Reset();
      last_success_.reset();
    }
  }
  return HapticScale(haptics_.Sample(now), strength_);
}
}  // namespace edtrace
