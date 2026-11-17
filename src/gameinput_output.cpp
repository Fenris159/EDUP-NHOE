#include "edtrace/gameinput_output.hpp"
#include "edtrace/gameinput_compat.hpp"
#include <GameInput.h>
#include <array>
#include <atomic>
#include <cstring>
#include <cmath>

namespace gi = GameInput::v4;
namespace edtrace { namespace {
SRWLOCK cache_lock = SRWLOCK_INIT;
std::array<gi::IGameInputDevice*, 16> devices{};
gi::IGameInput* input = nullptr;
gi::IGameInputDevice* selected = nullptr;  // Separate owned reference.
gi::GameInputCallbackToken token = 0;
std::atomic<DWORD> error{ERROR_NOT_READY};
std::atomic<bool> initialized{false};
std::atomic<std::uintptr_t> runtime_base{0};
std::atomic<std::size_t> runtime_size{0};
bool enabled = false, foreground = false, stopped = false;
bool controlled = false;
bool overflowed = false;
MotorBackend native_output = nullptr;
unsigned matches = 0;
std::uint64_t submissions = 0, stops = 0;
std::uint64_t trigger_submissions = 0;

void StopSelected() noexcept {
  if (!selected) return;
  const gi::GameInputRumbleParams zero{};
  selected->SetRumbleState(&zero); ++stops;
  std::uint32_t index = 99;
  if (native_output && !GameInputCompatGetStats().output_owned && selected->IsConnected() && selected->GetXInputDeviceIndex(&index) && index == 0) {
    XINPUT_VIBRATION native_zero{}; native_output(0, &native_zero);
  }
  selected->Release(); selected = nullptr;
}
gi::IGameInputDevice* Unique() noexcept {
  matches = 0; gi::IGameInputDevice* candidate = nullptr;
  if (overflowed) return nullptr;
  for (auto* device : devices) {
    if (!device || !device->IsConnected()) continue;
    std::uint32_t index = 99;
    if (!device->GetXInputDeviceIndex(&index) || index != 0 ||
        (device->GetSupportedRumbleMotors() & 3) != 3) continue;
    ++matches; candidate = device;
  }
  return matches == 1 ? candidate : nullptr;
}
void CALLBACK Device(gi::GameInputCallbackToken, void*, gi::GameInputDeviceEvent event, gi::IGameInputDevice* device) noexcept {
  AcquireSRWLockExclusive(&cache_lock);
  if (event == gi::GameInputDeviceEventConnected) {
    bool found = false; for (const auto item : devices) if (item == device) found = true;
    if (!found) {
      bool added = false;
      for (auto& slot : devices) if (!slot) { device->AddRef(); slot = device; added = true; break; }
      if (!added) overflowed = true;  // Untracked duplicates must never look unique.
    }
  } else if (event == gi::GameInputDeviceEventDisconnected) {
    for (auto& slot : devices) if (slot == device) { slot = nullptr; device->Release(); }
  }
  ReleaseSRWLockExclusive(&cache_lock);
}
DWORD Start(GameInputFactory factory) noexcept {
  if (input || initialized.load()) return ERROR_ALREADY_INITIALIZED;
  if (!factory) { error.store(ERROR_PROC_NOT_FOUND); return ERROR_PROC_NOT_FOUND; }
  void* object = nullptr;
  const HRESULT result = factory(gi::IID_IGameInput, &object);
  if (FAILED(result) || !object) {
    if (object) static_cast<gi::IGameInput*>(object)->Release();
    const DWORD failure = FAILED(result) ? static_cast<DWORD>(result) : ERROR_INVALID_DATA;
    error.store(failure); return failure;
  }
  input = static_cast<gi::IGameInput*>(object);
  const HRESULT registration = input->RegisterDeviceCallback(nullptr, gi::GameInputKindGamepad, nullptr, Device, &token);
  if (FAILED(registration)) {
    input->Release(); input = nullptr;
    error.store(static_cast<DWORD>(registration)); return static_cast<DWORD>(registration);
  }
  initialized.store(true, std::memory_order_release); error.store(ERROR_SUCCESS);
  return ERROR_SUCCESS;
}
}}

namespace edtrace {
DWORD GameInputOutputStart(const std::wstring& path, MotorBackend original) noexcept {
  native_output = original;
  // The host verifies the exact provided runtime/helper SHA-256 before this.
  HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!module) { const auto failure = GetLastError(); error.store(failure); return failure; }
  const auto base = reinterpret_cast<std::uintptr_t>(module);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
  runtime_size.store(nt->OptionalHeader.SizeOfImage, std::memory_order_release);
  runtime_base.store(base, std::memory_order_release);
  const auto raw = GetProcAddress(module, "GameInputInitialize"); GameInputFactory factory{};
  std::memcpy(&factory, &raw, sizeof(factory));
  // Keep the runtime loaded through process exit: its own asynchronous output
  // workers can still call the original XInput path after callback removal.
  const DWORD result = Start(factory);
  if (!result) GameInputCompatStart(module, input);
  return result;
}
DWORD GameInputOutputStartControlled(GameInputFactory factory) noexcept { controlled = true; return Start(factory); }
void GameInputOutputPoll(bool requested, bool focused) noexcept {
  GameInputCompatPoll();
  AcquireSRWLockExclusive(&cache_lock);
  enabled = requested && initialized.load(std::memory_order_acquire) && !stopped;
  foreground = focused;
  auto* candidate = enabled ? Unique() : nullptr;
  if (!foreground) candidate = nullptr;
  if (!enabled) matches = 0;
  if (candidate != selected) {
    StopSelected();
    if (candidate) {
      if (native_output && !GameInputCompatGetStats().output_owned) { XINPUT_VIBRATION zero{}; native_output(0, &zero); }
      candidate->AddRef(); selected = candidate;
    }
  }
  ReleaseSRWLockExclusive(&cache_lock);
}
GameInputSubmission GameInputOutputSubmit(DWORD index, const XINPUT_VIBRATION& vibration, float left, float right) noexcept {
  AcquireSRWLockExclusive(&cache_lock);
  GameInputSubmission result = GameInputSubmission::NativeFallback;
  if (enabled && index == 0) {
    if (!controlled) { DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid); foreground = pid == GetCurrentProcessId(); }
    if (!foreground) { StopSelected(); result = GameInputSubmission::Quiet; }
    else if (selected && Unique() == selected) {
      // Unsupported trigger amplitudes can be emulated into the main motors.
      // Submit them only when both actual trigger-motor capability bits exist.
      const bool triggers = (selected->GetSupportedRumbleMotors() & 12) == 12;
      const float left_value = triggers && std::isfinite(left) && left > 0 ? (left > 1 ? 1 : left) : 0;
      const float right_value = triggers && std::isfinite(right) && right > 0 ? (right > 1 ? 1 : right) : 0;
      const gi::GameInputRumbleParams value{vibration.wLeftMotorSpeed / 65535.0f,
          vibration.wRightMotorSpeed / 65535.0f, left_value, right_value};
      if (left_value || right_value) ++trigger_submissions;
      selected->SetRumbleState(&value); ++submissions;
      if (!vibration.wLeftMotorSpeed && !vibration.wRightMotorSpeed && !left_value && !right_value) ++stops;
      result = GameInputSubmission::Submitted;
    } else StopSelected();
  }
  ReleaseSRWLockExclusive(&cache_lock); return result;
}
bool GameInputOutputOwnsCaller(std::uintptr_t caller) noexcept {
  const auto base = runtime_base.load(std::memory_order_acquire);
  return base && caller >= base && caller - base < runtime_size.load(std::memory_order_acquire);
}
void GameInputOutputStop() noexcept {
  GameInputCompatStop();
  // Unregister waits for callbacks only when called outside a callback. Never
  // hold cache_lock while waiting for a callback that needs that same lock.
  AcquireSRWLockExclusive(&cache_lock); stopped = true; enabled = false; StopSelected(); ReleaseSRWLockExclusive(&cache_lock);
  if (input && initialized.load(std::memory_order_acquire)) input->UnregisterCallback(token);
  AcquireSRWLockExclusive(&cache_lock);
  for (auto& device : devices) if (device) { device->Release(); device = nullptr; }
  if (input) { input->Release(); input = nullptr; }
  initialized.store(false, std::memory_order_release); matches = 0;
  ReleaseSRWLockExclusive(&cache_lock);
}
GameInputOutputStats GameInputOutputGetStats() noexcept {
  AcquireSRWLockShared(&cache_lock);
  const GameInputOutputStats stats{error.load(), initialized.load(), selected != nullptr, foreground, matches, submissions, stops,
                                  selected ? static_cast<DWORD>(selected->GetSupportedRumbleMotors()) : 0, trigger_submissions};
  ReleaseSRWLockShared(&cache_lock); return stats;
}
}
