#include "edtrace/gameinput_compat.hpp"
#include "edtrace/gameinput_compat_hid.hpp"
#include "MinHook.h"
#include <GameInput.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>

namespace gi = GameInput::v4;
namespace edtrace { namespace {
using Device = gi::IGameInputDevice;
using Motors = gi::GameInputRumbleMotors;
using Params = gi::GameInputRumbleParams;
using CapsFn = Motors (WINAPI*)(Device*);
using RumbleFn = void (WINAPI*)(Device*, const Params*);
using BasicFn = HRESULT (WINAPI*)(void*, void*);
static_assert(sizeof(APP_LOCAL_DEVICE_ID) == 32 && sizeof(Params) == 16);
static_assert(std::atomic<CapsFn>::is_always_lock_free && std::atomic<RumbleFn>::is_always_lock_free &&
              std::atomic<BasicFn>::is_always_lock_free);
constexpr DWORD kVendor = 0x045e, kProduct = 0x0b22;
constexpr DWORD kSteamVendor = 0x28de, kSteamProduct = 0x11ff;
constexpr std::size_t kCapacity = 16, kBasicRva = 0x64d20;
constexpr std::array<unsigned char, 32> kBasicSignature{{
  0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xec,0x20,0x48,
  0x8b,0xfa,0x48,0x8b,0xf1,0x48,0x8d,0x4a,0x20,0xe8,0xfe,0xf9,0xff,0xff,0x84,0xc0}};
// SendEventDeviceSetRumble allocates exactly 0x50 bytes, copies its complete
// APP_LOCAL_DEVICE_ID to +0x20 and copies all four float bit patterns to +0x40.
struct NativeEvent {
  DWORD type, size;
  std::uint64_t timestamp, field16, field24;
  APP_LOCAL_DEVICE_ID identity;
  Params values;
};
static_assert(offsetof(NativeEvent, identity) == 0x20 && offsetof(NativeEvent, values) == 0x40 && sizeof(NativeEvent) == 0x50);
enum class Phase { Native, AwaitHidFence, Hid, ClosingHid, AwaitNativeFence, AbortingInitial };
SRWLOCK operation = SRWLOCK_INIT, cache_lock = SRWLOCK_INIT;
std::array<Device*, kCapacity> devices{};
std::atomic<bool> accept_callbacks{false};
std::atomic<std::uint64_t> generation{0};
bool overflowed = false, started = false, stopping = false;
bool hooks_enabled = false;
bool initial_failed = false;
DWORD initial_failure_reason = ERROR_SUCCESS;
ULONGLONG initial_fence_tick = 0;
std::atomic<DWORD> attempted{0};
DWORD error = ERROR_NOT_READY;
gi::IGameInput* input = nullptr;
gi::GameInputCallbackToken token{};
Device* selected = nullptr;  // Separate owned reference.
APP_LOCAL_DEVICE_ID selected_id{};
DWORD native_motors = 0;
unsigned matches = 0;
unsigned connected_gamepads = 0;
DWORD selected_vendor = 0, selected_product = 0;
bool selected_alias = false;
std::uint64_t selection_generation = 0;
Phase phase = Phase::Native;
HANDLE fence_event = nullptr;
std::uintptr_t image = 0, caps_target = 0, rumble_target = 0;
std::size_t image_size = 0, caps_slot = 0, rumble_slot = 0;
DWORD installed_mask = 0;
std::atomic<CapsFn> original_caps{nullptr};
std::atomic<RumbleFn> original_rumble{nullptr};
std::atomic<BasicFn> original_basic{nullptr};
std::uint64_t submissions = 0, trigger_submissions = 0, stops = 0;
std::uint64_t stale_suppressed = 0, quieted = 0, native_forwards = 0;
std::uint64_t native_events = 0, native_identity_matches = 0, native_fences = 0;
Params held{};
bool held_nonzero = false, output_quiet = true;
ULONGLONG last_refresh_tick = 0;
std::uint64_t refreshes = 0, focus_stops = 0;
thread_local bool inside_native = false;
struct NativeScope {
  bool previous = inside_native;
  NativeScope() noexcept { inside_native = true; }
  ~NativeScope() { inside_native = previous; }
};
struct OperationGuard {
  OperationGuard() noexcept { AcquireSRWLockExclusive(&operation); }
  ~OperationGuard() { ReleaseSRWLockExclusive(&operation); }
};
template <typename T> T Function(void* address) noexcept {
  T value{}; static_assert(sizeof(value) == sizeof(address));
  std::memcpy(&value, &address, sizeof(value)); return value;
}
template <typename T> void* Bits(T function) noexcept {
  void* address{}; static_assert(sizeof(address) == sizeof(function));
  std::memcpy(&address, &function, sizeof(address)); return address;
}
bool Read(std::uintptr_t address, void* destination, std::size_t bytes) noexcept {
  if (!address || address > std::numeric_limits<std::uintptr_t>::max() - bytes) return false;
  SIZE_T count = 0;
  return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), destination, bytes, &count) && count == bytes;
}
bool NativeAddress(std::uintptr_t address, std::size_t bytes = 1) noexcept {
  return address >= image && address - image < image_size && bytes <= image_size - (address - image);
}
bool Executable(std::uintptr_t address) noexcept {
  MEMORY_BASIC_INFORMATION memory{};
  if (!NativeAddress(address) || !VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof(memory)) ||
      memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
  return (memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}
// MinGW uses an offset-plus-one virtual member encoding. Verify that encoding
// and the positions against the official v4 declaration, rather than treating
// a guessed vtable index as an ABI contract.
template <typename T> bool VirtualSlot(T member, std::size_t expected, std::size_t& slot) noexcept {
  struct Encoding { std::ptrdiff_t entry, adjustment; } value{};
  static_assert(sizeof(member) == sizeof(value), "Verified MinGW x64 member representation required");
  std::memcpy(&value, &member, sizeof(value));
  if (value.entry <= 0 || !(value.entry & 1) || value.adjustment ||
      (value.entry - 1) % static_cast<std::ptrdiff_t>(sizeof(void*))) return false;
  slot = static_cast<std::size_t>(value.entry - 1) / sizeof(void*);
  return slot == expected;
}
std::uintptr_t Method(Device* device, std::size_t slot) noexcept {
  std::uintptr_t table = 0, address = 0;
  if (!Read(reinterpret_cast<std::uintptr_t>(device), &table, sizeof(table)) ||
      !NativeAddress(table, (slot + 1) * sizeof(void*)) ||
      !Read(table + slot * sizeof(void*), &address, sizeof(address)) || !Executable(address)) return 0;
  return address;
}
DWORD NativeMotors(Device* device) noexcept {
  const auto method = Method(device, caps_slot);
  if (!method) return 0;
  NativeScope scope;
  const auto original = original_caps.load(std::memory_order_acquire);
  return static_cast<DWORD>(method == caps_target && original ? original(device) :
                            Function<CapsFn>(reinterpret_cast<void*>(method))(device));
}
bool SingleConnectedGamepad(Device* target) noexcept {
  std::array<Device*, kCapacity> snapshot{};
  AcquireSRWLockShared(&cache_lock);
  const bool overflow = overflowed;
  const auto version = generation.load(std::memory_order_acquire);
  for (std::size_t i = 0; i < devices.size(); ++i) if ((snapshot[i] = devices[i])) snapshot[i]->AddRef();
  ReleaseSRWLockShared(&cache_lock);
  unsigned count = 0; bool found = false;
  for (auto* device : snapshot) {
    if (device && device->IsConnected() && (device->GetSupportedInput() & gi::GameInputKindGamepad) != gi::GameInputKindUnknown) {
      ++count; if (device == target) found = true;
    }
  }
  const bool valid = !overflow && found && count == 1 && version == generation.load(std::memory_order_acquire);
  for (auto* device : snapshot) if (device) device->Release();
  return valid;
}
bool Identity(Device* device, APP_LOCAL_DEVICE_ID* identity = nullptr) noexcept {
  if (!device->IsConnected()) return false;
  const auto vendor = device->GetVendorId(), product = device->GetProductId();
  const bool physical = vendor == kVendor && product == kProduct;
  const bool steam = vendor == kSteamVendor && product == kSteamProduct;
  // A Steam session can expose the one physical Elite as Valve's virtual
  // Xbox profile. Permit that exact alias only in a single-gamepad session.
  // The transport still independently verifies the real Elite descriptor and
  // endpoint uniqueness before any capability augmentation or output.
  if (!physical && !(steam && SingleConnectedGamepad(device))) return false;
  std::uint32_t index = 99;
  if (!device->GetXInputDeviceIndex(&index) || index != 0) return false;
  if (identity) {
    APP_LOCAL_DEVICE_ID again{}; device->GetDeviceId(identity); device->GetDeviceId(&again);
    const APP_LOCAL_DEVICE_ID zero{};
    if (std::memcmp(identity, &again, sizeof(again)) || !std::memcmp(identity, &zero, sizeof(zero))) return false;
  }
  return true;
}
Device* Unique() noexcept {
  std::array<Device*, kCapacity> snapshot{};
  AcquireSRWLockShared(&cache_lock);
  const bool overflow = overflowed;
  const auto version = generation.load(std::memory_order_acquire);
  for (std::size_t i = 0; i < devices.size(); ++i) if ((snapshot[i] = devices[i])) snapshot[i]->AddRef();
  ReleaseSRWLockShared(&cache_lock);
  Device* candidate = nullptr; matches = 0; connected_gamepads = 0;
  for (auto* device : snapshot) {
    if (device && device->IsConnected() && (device->GetSupportedInput() & gi::GameInputKindGamepad) != gi::GameInputKindUnknown)
      ++connected_gamepads;
    if (!overflow && device && Identity(device) && (NativeMotors(device) & 3) == 3) { ++matches; candidate = device; }
  }
  if (matches != 1 || version != generation.load(std::memory_order_acquire)) candidate = nullptr;
  for (auto* device : snapshot) if (device && device != candidate) device->Release();
  return candidate;
}
void CALLBACK DeviceChanged(gi::GameInputCallbackToken, void*, gi::GameInputDeviceEvent event, Device* device) noexcept {
  if (!accept_callbacks.load(std::memory_order_acquire)) return;
  Device* released = nullptr;
  AcquireSRWLockExclusive(&cache_lock);
  if (accept_callbacks.load(std::memory_order_acquire)) {
    if (event == gi::GameInputDeviceEventConnected) {
      bool found = false;
      for (auto* known : devices) if (known == device) found = true;
      if (!found) {
        bool added = false;
        for (auto& slot : devices) if (!slot) { device->AddRef(); slot = device; added = true; break; }
        if (!added) overflowed = true;
      }
    } else if (event == gi::GameInputDeviceEventDisconnected) {
      for (auto& slot : devices) if (slot == device) { released = slot; slot = nullptr; break; }
    }
    generation.fetch_add(1, std::memory_order_release);
  }
  ReleaseSRWLockExclusive(&cache_lock);
  if (released) released->Release();
}
bool Foreground() noexcept {
  DWORD owner = 0; GetWindowThreadProcessId(GetForegroundWindow(), &owner);
  return owner == GetCurrentProcessId();
}
bool Nonzero(const Params& value) noexcept {
  return value.lowFrequency || value.highFrequency || value.leftTrigger || value.rightTrigger;
}
void ClearHeld() noexcept {
  held = {}; held_nonzero = false; output_quiet = true; last_refresh_tick = 0;
}
void AbortInitial(DWORD reason) noexcept {
  // No HID frame has been submitted while awaiting the first native fence.
  // Disable this optional extension after failure, instead of repeatedly
  // interrupting the working main-motor path with unsuccessful handoffs.
  initial_failed = true; initial_failure_reason = reason; error = reason;
  ClearHeld(); phase = Phase::AbortingInitial;
}
// Signed zero is a legitimate zero amplitude. The verified native frontend
// copies these four bit patterns unchanged, making an internal stop fence
// distinguishable from ordinary authored all-positive-zero stops. The Basic
// converter still sends exactly WORD(0,0). No timer is used as a drain ACK.
Params Fence() noexcept { return {-0.0f, -0.0f, -0.0f, -0.0f}; }
bool IsFence(const Params& value) noexcept {
  const auto fence = Fence(); return std::memcmp(&value, &fence, sizeof(fence)) == 0;
}
bool SelectedEvent(void* pointer, NativeEvent& event) noexcept {
  return selected && phase != Phase::Native &&
    Read(reinterpret_cast<std::uintptr_t>(pointer), &event, sizeof(event)) &&
    event.type == 0x302 && event.size == sizeof(event) &&
    !std::memcmp(&event.identity, &selected_id, sizeof(selected_id));
}
void NativeFence() noexcept {
  if (!selected) return;
  ResetEvent(fence_event);
  const auto fence = Fence(); NativeScope scope;
  original_rumble.load(std::memory_order_acquire)(selected, &fence);
}
HRESULT WINAPI BasicOutput(void* self, void* pointer) noexcept {
  const DWORD incoming = GetLastError();
  const auto original = original_basic.load(std::memory_order_acquire);
  AcquireSRWLockExclusive(&operation);
  ++native_events;
  NativeEvent event{};
  if (SelectedEvent(pointer, event)) {
    ++native_identity_matches;
    const bool fence = IsFence(event.values);
    if ((phase == Phase::AwaitHidFence || phase == Phase::AwaitNativeFence) && fence) {
      const auto waiting = phase;
      SetLastError(incoming);
      HRESULT result = E_FAIL;
      { NativeScope scope; result = original(self, pointer); }
      const DWORD outgoing = GetLastError();
      if (SUCCEEDED(result)) {
        ++native_fences;
        if (waiting == Phase::AwaitHidFence && !stopping && GameInputCompatHidReady()) {
          phase = Phase::Hid; error = ERROR_SUCCESS;
        } else if (waiting == Phase::AwaitNativeFence) phase = Phase::Native;
        else AbortInitial(ERROR_NOT_READY);
        SetEvent(fence_event);
      } else {
        error = static_cast<DWORD>(result);
        if (waiting == Phase::AwaitHidFence) AbortInitial(error);
      }
      ReleaseSRWLockExclusive(&operation); SetLastError(outgoing); return result;
    }
    if (phase != Phase::AwaitHidFence) {
      ++stale_suppressed;
      ReleaseSRWLockExclusive(&operation); SetLastError(incoming); return S_OK;
    }
  }
  // Serialize original native writes with ownership changes as well: a write
  // already inside its backend must finish before a handoff fence can ACK.
  SetLastError(incoming);
  HRESULT result = E_FAIL;
  { NativeScope scope; result = original(self, pointer); }
  const DWORD outgoing = GetLastError();
  ReleaseSRWLockExclusive(&operation); SetLastError(outgoing); return result;
}
Motors WINAPI SupportedMotors(Device* self) noexcept {
  const auto original = original_caps.load(std::memory_order_acquire);
  const auto value = original(self); const DWORD outgoing = GetLastError();
  if (inside_native) return value;
  AcquireSRWLockShared(&operation);
  const bool enabled = started && !stopping && phase == Phase::Hid && self == selected &&
    selection_generation == generation.load(std::memory_order_acquire) && GameInputCompatHidReady() && Identity(self);
  ReleaseSRWLockShared(&operation); SetLastError(outgoing);
  return enabled ? static_cast<Motors>(static_cast<DWORD>(value) | 12u) : value;
}
float Bound(float value) noexcept { return !std::isfinite(value) || value < 0 ? 0 : (value > 1 ? 1 : value); }
void WINAPI Rumble(Device* self, const Params* value) noexcept {
  const DWORD incoming = GetLastError();
  const auto original = original_rumble.load(std::memory_order_acquire);
  if (inside_native) { original(self, value); return; }
  AcquireSRWLockExclusive(&operation);
  if (self == selected && phase != Phase::Native) {
    if (phase == Phase::Hid && !stopping && selection_generation == generation.load(std::memory_order_acquire) &&
        GameInputCompatHidReady() && Identity(self)) {
      Params copied{};
      const bool readable = !value || Read(reinterpret_cast<std::uintptr_t>(value), &copied, sizeof(copied));
      if (readable) {
        copied = {Bound(copied.lowFrequency), Bound(copied.highFrequency), Bound(copied.leftTrigger), Bound(copied.rightTrigger)};
        held = copied; held_nonzero = Nonzero(held);
        if (!Foreground()) copied = {};
        if (GameInputCompatHidSubmit(copied.lowFrequency, copied.highFrequency, copied.leftTrigger, copied.rightTrigger)) {
          ++submissions;
          if (copied.leftTrigger || copied.rightTrigger) ++trigger_submissions;
          if (!copied.lowFrequency && !copied.highFrequency && !copied.leftTrigger && !copied.rightTrigger) ++stops;
          output_quiet = !Nonzero(copied); last_refresh_tick = GetTickCount64();
          ReleaseSRWLockExclusive(&operation); SetLastError(incoming); return;
        }
      }
      ClearHeld(); phase = Phase::ClosingHid;
    } else if (phase == Phase::Hid) { ClearHeld(); phase = Phase::ClosingHid; }
    // A failed/closing raw writer is not an output-ownership barrier. Keep
    // this device quiet until Poll joins it and the native zero fence drains.
    ++quieted;
    ReleaseSRWLockExclusive(&operation); SetLastError(incoming); return;
  }
  ++native_forwards;
  ReleaseSRWLockExclusive(&operation);
  SetLastError(incoming); NativeScope scope; original(self, value);
}
DWORD Install(Device* device) noexcept {
  const auto caps = Method(device, caps_slot), rumble = Method(device, rumble_slot);
  if (!caps || !rumble || caps == rumble) return ERROR_REVISION_MISMATCH;
  if (installed_mask) return hooks_enabled && caps == caps_target && rumble == rumble_target ? ERROR_SUCCESS : ERROR_REVISION_MISMATCH;
  std::array<unsigned char, 32> profile{};
  if (!NativeAddress(image + kBasicRva, profile.size()) || !Read(image + kBasicRva, profile.data(), profile.size()) || profile != kBasicSignature)
    return ERROR_REVISION_MISMATCH;
  const auto initialization = MH_Initialize();
  if (initialization != MH_OK && initialization != MH_ERROR_ALREADY_INITIALIZED) return ERROR_NOT_SUPPORTED;
  caps_target = caps; rumble_target = rumble;
  void* trampoline = nullptr;
  if (MH_CreateHook(reinterpret_cast<void*>(caps), Bits(&SupportedMotors), &trampoline) != MH_OK) return ERROR_NOT_SUPPORTED;
  original_caps.store(Function<CapsFn>(trampoline), std::memory_order_release); installed_mask |= 1;
  if (MH_CreateHook(reinterpret_cast<void*>(rumble), Bits(&Rumble), &trampoline) != MH_OK) return ERROR_NOT_SUPPORTED;
  original_rumble.store(Function<RumbleFn>(trampoline), std::memory_order_release); installed_mask |= 2;
  if (MH_CreateHook(reinterpret_cast<void*>(image + kBasicRva), Bits(&BasicOutput), &trampoline) != MH_OK) return ERROR_NOT_SUPPORTED;
  original_basic.store(Function<BasicFn>(trampoline), std::memory_order_release); installed_mask |= 4;
  for (const auto address : {caps, rumble, image + kBasicRva})
    if (MH_QueueEnableHook(reinterpret_cast<void*>(address)) != MH_OK) return ERROR_NOT_SUPPORTED;
  hooks_enabled = MH_ApplyQueued() == MH_OK;
  return hooks_enabled ? ERROR_SUCCESS : ERROR_NOT_SUPPORTED;
}
void StopHid() noexcept {
  if (phase == Phase::Native) return;
  ClearHeld();
  phase = Phase::ClosingHid;
  GameInputCompatHidStop();
  if (GameInputCompatHidGetStats().started) { error = ERROR_TIMEOUT; return; }
  phase = Phase::AwaitNativeFence; NativeFence();
}
void ReleaseSelected() noexcept {
  ClearHeld();
  if (selected) { selected->Release(); selected = nullptr; }
  selected_id = {}; native_motors = 0;
  selected_vendor = 0; selected_product = 0; selected_alias = false;
}
void FinishInitialAbort() noexcept {
  GameInputCompatHidStop();
  if (GameInputCompatHidGetStats().started) { error = ERROR_TIMEOUT; return; }
  // The raw writer never owned a nonzero output. Once it has joined, native
  // jobs can pass directly; a return-from-HID fence would add no protection.
  phase = Phase::Native; error = initial_failure_reason; ReleaseSelected();
}
void RefreshHeld() noexcept {
  if (phase != Phase::Hid || stopping || !GameInputCompatHidReady()) return;
  const bool focused = Foreground();
  const auto now = GetTickCount64();
  if (!focused) {
    if (!output_quiet) {
      if (!GameInputCompatHidSubmit(0, 0, 0, 0)) { ClearHeld(); phase = Phase::ClosingHid; return; }
      ++focus_stops; output_quiet = true; last_refresh_tick = now;
    }
    return;
  }
  // The controller's finite 200ms report duration bounds abandoned output.
  // Keep GameInput's persistent state alive at 50ms, including after focus
  // returns, without pretending the caller submitted additional API requests.
  if (held_nonzero && (output_quiet || now - last_refresh_tick >= 50)) {
    if (!GameInputCompatHidSubmit(held.lowFrequency, held.highFrequency, held.leftTrigger, held.rightTrigger)) {
      ClearHeld(); phase = Phase::ClosingHid; return;
    }
    ++refreshes; output_quiet = false; last_refresh_tick = now;
  }
}
}}

namespace edtrace {
DWORD GameInputCompatStart(HMODULE native_runtime, void* v4_input) noexcept {
  const DWORD incoming = GetLastError();
  DWORD empty = 0;
  if (!attempted.compare_exchange_strong(empty, 1)) return ERROR_ALREADY_INITIALIZED;
  OperationGuard guard;
  if (!GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_host_version")) {
    error = ERROR_NOT_SUPPORTED; SetLastError(incoming); return error;
  }
  image = reinterpret_cast<std::uintptr_t>(native_runtime);
  IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
  if (!v4_input || !Read(image, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000 ||
      !Read(image + static_cast<std::size_t>(dos.e_lfanew), &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE ||
      nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
      nt.OptionalHeader.SizeOfImage <= kBasicRva + kBasicSignature.size() ||
      image > std::numeric_limits<std::uintptr_t>::max() - nt.OptionalHeader.SizeOfImage ||
      !VirtualSlot(&Device::GetSupportedRumbleMotors, 13, caps_slot) || !VirtualSlot(&Device::SetRumbleState, 47, rumble_slot)) {
    error = ERROR_REVISION_MISMATCH; SetLastError(incoming); return error;
  }
  image_size = nt.OptionalHeader.SizeOfImage;
  HMODULE pinned = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                         reinterpret_cast<LPCWSTR>(&GameInputCompatStart), &pinned)) {
    error = GetLastError(); SetLastError(incoming); return error;
  }
  fence_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!fence_event) { error = GetLastError(); SetLastError(incoming); return error; }
  input = static_cast<gi::IGameInput*>(v4_input); input->AddRef();
  accept_callbacks.store(true, std::memory_order_release);
  const HRESULT registered = input->RegisterDeviceCallback(nullptr, gi::GameInputKindGamepad, nullptr, DeviceChanged, &token);
  if (FAILED(registered)) {
    accept_callbacks.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&cache_lock);
    const auto released = devices; devices.fill(nullptr);
    ReleaseSRWLockExclusive(&cache_lock);
    for (auto* device : released) if (device) device->Release();
    input->Release(); input = nullptr; CloseHandle(fence_event); fence_event = nullptr;
    error = static_cast<DWORD>(registered); SetLastError(incoming); return error;
  }
  started = true; error = ERROR_SUCCESS;
  SetLastError(incoming); return ERROR_SUCCESS;
}
void GameInputCompatPoll() noexcept {
  const DWORD incoming = GetLastError();
  AcquireSRWLockExclusive(&operation);
  if (!started || stopping) { ReleaseSRWLockExclusive(&operation); SetLastError(incoming); return; }
  if (phase == Phase::AwaitHidFence && GetTickCount64() - initial_fence_tick >= 1000)
    AbortInitial(ERROR_TIMEOUT);
  if (phase == Phase::AbortingInitial) FinishInitialAbort();
  if (initial_failed) { ReleaseSRWLockExclusive(&operation); SetLastError(incoming); return; }
  Device* candidate = Unique();
  if (phase == Phase::ClosingHid) StopHid();
  if (phase == Phase::AwaitNativeFence || phase == Phase::ClosingHid) {
    if (candidate) candidate->Release();
    ReleaseSRWLockExclusive(&operation); SetLastError(incoming); return;
  }
  if (selected && (candidate != selected || !Identity(selected) ||
      ((phase == Phase::Hid || phase == Phase::AwaitHidFence) && !GameInputCompatHidReady()))) {
    if (phase != Phase::Native) StopHid();
    if (phase == Phase::Native) ReleaseSelected();
  }
  if (candidate == selected) selection_generation = generation.load(std::memory_order_acquire);
  if (phase == Phase::Native && candidate) {
    if (candidate != selected) { ReleaseSelected(); selected = candidate; candidate = nullptr; }
    APP_LOCAL_DEVICE_ID identity{};
    if (Identity(selected, &identity)) {
      error = Install(selected);
      if (!error) {
        selected_id = identity; native_motors = NativeMotors(selected);
        selected_vendor = selected->GetVendorId(); selected_product = selected->GetProductId();
        selected_alias = selected_vendor == kSteamVendor && selected_product == kSteamProduct;
        selection_generation = generation.load(std::memory_order_acquire);
        if (!GameInputCompatHidGetStats().started) GameInputCompatHidStart(kVendor, kProduct);
        GameInputCompatHidPoll();
        if (GameInputCompatHidReady()) {
          ClearHeld(); phase = Phase::AwaitHidFence; initial_fence_tick = GetTickCount64(); NativeFence();
        }
        else error = GameInputCompatHidGetStats().error;
      }
    } else error = ERROR_NOT_FOUND;
  } else if (phase == Phase::Hid || phase == Phase::AwaitHidFence) GameInputCompatHidPoll();
  RefreshHeld();
  if (candidate) candidate->Release();
  ReleaseSRWLockExclusive(&operation); SetLastError(incoming);
}
void GameInputCompatStop() noexcept {
  const DWORD incoming = GetLastError();
  AcquireSRWLockExclusive(&operation);
  if (!started) { ReleaseSRWLockExclusive(&operation); SetLastError(incoming); return; }
  stopping = true; accept_callbacks.store(false, std::memory_order_release);
  if (phase == Phase::AbortingInitial) FinishInitialAbort();
  else if (phase != Phase::Native) StopHid();
  else GameInputCompatHidStop();
  auto* runtime = input;
  const bool wait_for_fence = phase == Phase::AwaitNativeFence || phase == Phase::AwaitHidFence;
  ReleaseSRWLockExclusive(&operation);
  // Callback removal may wait for DeviceChanged, which takes cache_lock.
  const bool unregistered = !runtime || runtime->UnregisterCallback(token);
  if (wait_for_fence && fence_event) WaitForSingleObject(fence_event, 1000);
  AcquireSRWLockExclusive(&operation);
  AcquireSRWLockExclusive(&cache_lock);
  std::array<Device*, kCapacity> released = devices; devices.fill(nullptr);
  ReleaseSRWLockExclusive(&cache_lock);
  const bool complete = phase == Phase::Native && !GameInputCompatHidGetStats().started;
  if (complete) ReleaseSelected();
  // A timed-out HID worker/fence retains its selected reference and suppression
  // gate until process exit. Never release output ownership speculatively.
  if (!unregistered) error = ERROR_OPERATION_ABORTED;
  gi::IGameInput* released_input = nullptr;
  if (input && complete && unregistered) { released_input = input; input = nullptr; }
  if (complete && fence_event) { CloseHandle(fence_event); fence_event = nullptr; }
  started = false;
  ReleaseSRWLockExclusive(&operation);
  for (auto* device : released) if (device) device->Release();
  if (released_input) released_input->Release();
  SetLastError(incoming);
}
GameInputCompatStats GameInputCompatGetStats() noexcept {
  const DWORD incoming = GetLastError();
  AcquireSRWLockShared(&operation);
  GameInputCompatStats result{};
  result.error = error; result.started = started; result.hooks_installed = hooks_enabled;
  result.ready = started && !stopping && phase == Phase::Hid && GameInputCompatHidReady();
  result.output_owned = phase == Phase::Hid || phase == Phase::ClosingHid;
  result.waiting_native_zero = phase == Phase::AwaitHidFence || phase == Phase::AwaitNativeFence;
  result.pending_stop = phase == Phase::ClosingHid || phase == Phase::AwaitNativeFence || phase == Phase::AbortingInitial;
  result.initial_failure = initial_failed; result.initial_failure_reason = initial_failure_reason;
  result.matches = matches; result.connected_gamepads = connected_gamepads; result.native_motors = native_motors;
  if (selected) {
    result.vendor = selected_vendor; result.product = selected_product; result.slot = 0;
    result.steam_virtual_alias = selected_alias;
    std::memcpy(result.identity.data(), &selected_id, sizeof(selected_id));
  }
  AcquireSRWLockShared(&cache_lock);
  for (auto* device : devices) if (device) ++result.cached_devices;
  ReleaseSRWLockShared(&cache_lock);
  result.submissions = submissions; result.trigger_submissions = trigger_submissions; result.stops = stops;
  result.stale_native_suppressed = stale_suppressed; result.quieted_requests = quieted; result.native_forwards = native_forwards;
  result.native_events = native_events; result.native_identity_matches = native_identity_matches; result.native_fences = native_fences;
  result.refreshes = refreshes; result.focus_stops = focus_stops; result.held_nonzero = held_nonzero;
  result.caps_target_rva = caps_target ? caps_target - image : 0;
  result.rumble_target_rva = rumble_target ? rumble_target - image : 0;
  ReleaseSRWLockShared(&operation); SetLastError(incoming); return result;
}
}
