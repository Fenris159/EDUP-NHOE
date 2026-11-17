#include "edtrace/gameinput_output.hpp"
#include <GameInput.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

// The real SDK interfaces are the only mocked seam. No production helper or
// cache is replaced, and this process never loads the Microsoft runtime or
// calls a hardware backend.
namespace gi = GameInput::v4;
namespace {
unsigned unexpected_calls = 0;
void Check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
bool WriteResult(const wchar_t* path, const std::string& value) noexcept {
  HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD count = 0;
  const bool ok = WriteFile(file, value.data(), static_cast<DWORD>(value.size()), &count, nullptr) && count == value.size();
  CloseHandle(file);
  return ok;
}

class Device final : public gi::IGameInputDevice {
 public:
  ULONG references = 1;
  unsigned adds = 0, releases = 0;
  bool connected = true, index_valid = true;
  std::uint32_t index = 0;
  gi::GameInputRumbleMotors motors = static_cast<gi::GameInputRumbleMotors>(3);
  std::array<gi::GameInputRumbleParams, 64> writes{};
  unsigned write_count = 0;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** result) override {
    ++unexpected_calls; *result = nullptr; return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { ++adds; return ++references; }
  ULONG STDMETHODCALLTYPE Release() override { ++releases; return --references; }
  bool STDMETHODCALLTYPE IsConnected() override { return connected; }
  bool STDMETHODCALLTYPE GetXInputDeviceIndex(std::uint32_t* value) override {
    // Reporting connection and slot separately also exercises a stale slot
    // report during disconnect: both public predicates must be checked.
    *value = index_valid ? index : 0;
    return index_valid;
  }
  gi::GameInputRumbleMotors STDMETHODCALLTYPE GetSupportedRumbleMotors() override { return motors; }
  void STDMETHODCALLTYPE SetRumbleState(const gi::GameInputRumbleParams* value) override {
    Check(write_count < writes.size(), "fixture output capacity");
    writes[write_count++] = value ? *value : gi::GameInputRumbleParams{};
  }
  const gi::GameInputRumbleParams& Last() const {
    Check(write_count != 0, "controller has an observed output");
    return writes[write_count - 1];
  }

  // Every unrelated SDK operation is observable as a forbidden call. These
  // implementations exist only to satisfy the actual v4 pure interfaces.
#define UNUSED_VALUE(type, method) type STDMETHODCALLTYPE method() override { ++unexpected_calls; return {}; }
#define UNUSED_INFO(type, method) bool STDMETHODCALLTYPE method(type*) override { ++unexpected_calls; return false; }
#define UNUSED_ARRAY(type, method) std::uint32_t STDMETHODCALLTYPE method(std::uint32_t, type*) override { ++unexpected_calls; return 0; }
  UNUSED_VALUE(std::uint16_t, GetVendorId)
  UNUSED_VALUE(std::uint16_t, GetProductId)
  UNUSED_VALUE(std::uint16_t, GetRevisionNumber)
  void STDMETHODCALLTYPE GetUsage(gi::GameInputUsage*) override { ++unexpected_calls; }
  void STDMETHODCALLTYPE GetHardwareVersion(gi::GameInputVersion*) override { ++unexpected_calls; }
  void STDMETHODCALLTYPE GetFirmwareVersion(gi::GameInputVersion*) override { ++unexpected_calls; }
  void STDMETHODCALLTYPE GetDeviceId(APP_LOCAL_DEVICE_ID*) override { ++unexpected_calls; }
  void STDMETHODCALLTYPE GetDeviceRootId(APP_LOCAL_DEVICE_ID*) override { ++unexpected_calls; }
  UNUSED_VALUE(gi::GameInputDeviceFamily, GetDeviceFamily)
  UNUSED_VALUE(gi::GameInputKind, GetSupportedInput)
  UNUSED_VALUE(gi::GameInputSystemButtons, GetSupportedSystemButtons)
  UNUSED_VALUE(gi::GameInputLightingGroups, GetSupportedLightingGroups)
  std::uint32_t STDMETHODCALLTYPE GetSupportedLightingGroupSize(gi::GameInputLightingGroups) override { ++unexpected_calls; return 0; }
  void STDMETHODCALLTYPE GetContainerId(GUID*) override { ++unexpected_calls; }
  UNUSED_ARRAY(char, GetDisplayName)
  UNUSED_ARRAY(char, GetPnpPath)
  UNUSED_INFO(gi::GameInputKeyboardInfo, GetKeyboardInfo)
  UNUSED_INFO(gi::GameInputMouseInfo, GetMouseInfo)
  UNUSED_INFO(gi::GameInputSensorsInfo, GetSensorsInfo)
  UNUSED_VALUE(std::uint32_t, GetControllerAxisCount)
  UNUSED_ARRAY(gi::GameInputLabel, GetControllerAxisLabels)
  UNUSED_VALUE(std::uint32_t, GetControllerButtonCount)
  UNUSED_ARRAY(gi::GameInputLabel, GetControllerButtonLabels)
  UNUSED_VALUE(std::uint32_t, GetControllerSwitchCount)
  UNUSED_ARRAY(gi::GameInputControllerSwitchInfo, GetControllerSwitchInfo)
  UNUSED_INFO(gi::GameInputArcadeStickInfo, GetArcadeStickInfo)
  UNUSED_INFO(gi::GameInputFlightStickInfo, GetFlightStickInfo)
  UNUSED_INFO(gi::GameInputGamepadInfo, GetGamepadInfo)
  UNUSED_INFO(gi::GameInputRacingWheelInfo, GetRacingWheelInfo)
  UNUSED_VALUE(std::uint32_t, GetForceFeedbackMotorCount)
  UNUSED_ARRAY(gi::GameInputForceFeedbackMotorInfo, GetForceFeedbackMotorInfo)
  UNUSED_VALUE(std::uint32_t, GetInputReportCount)
  UNUSED_ARRAY(gi::GameInputRawDeviceReportInfo, GetInputReportInfo)
  UNUSED_VALUE(std::uint32_t, GetOutputReportCount)
  UNUSED_ARRAY(gi::GameInputRawDeviceReportInfo, GetOutputReportInfo)
  UNUSED_VALUE(std::uint32_t, GetFeatureReportCount)
  UNUSED_ARRAY(gi::GameInputRawDeviceReportInfo, GetFeatureReportInfo)
  HRESULT STDMETHODCALLTYPE GetHapticInfo(gi::GameInputHapticInfo*) override { ++unexpected_calls; return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE CreateForceFeedbackEffect(std::uint32_t, const gi::GameInputForceFeedbackParams*, gi::IGameInputForceFeedbackEffect**) override { ++unexpected_calls; return E_NOTIMPL; }
  bool STDMETHODCALLTYPE IsForceFeedbackMotorPoweredOn(std::uint32_t) override { ++unexpected_calls; return false; }
  void STDMETHODCALLTYPE SetForceFeedbackMotorGain(std::uint32_t, float) override { ++unexpected_calls; }
  HRESULT STDMETHODCALLTYPE DirectInputEscape(std::uint32_t, const void*, std::uint32_t, void*, std::uint32_t, std::uint32_t*) override { ++unexpected_calls; return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE CreateInputMapper(gi::IGameInputMapper**) override { ++unexpected_calls; return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetExtraAxisCount(gi::GameInputKind, std::uint32_t*) override { ++unexpected_calls; return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetExtraButtonCount(gi::GameInputKind, std::uint32_t*) override { ++unexpected_calls; return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetExtraAxisIndexes(gi::GameInputKind, std::uint32_t, std::uint8_t*) override { ++unexpected_calls; return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetExtraButtonIndexes(gi::GameInputKind, std::uint32_t, std::uint8_t*) override { ++unexpected_calls; return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE CreateRawDeviceReport(std::uint32_t, gi::GameInputRawDeviceReportKind, gi::IGameInputRawDeviceReport**) override { ++unexpected_calls; return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetRawDeviceFeature(std::uint32_t, gi::IGameInputRawDeviceReport**) override { ++unexpected_calls; return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE SetRawDeviceFeature(gi::IGameInputRawDeviceReport*) override { ++unexpected_calls; return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE SendRawDeviceOutput(gi::IGameInputRawDeviceReport*) override { ++unexpected_calls; return E_NOTIMPL; }
  void STDMETHODCALLTYPE SetLightingColor(gi::GameInputColor, gi::GameInputLightingGroups, std::uint32_t) override { ++unexpected_calls; }
  void STDMETHODCALLTYPE ApplyLightingColors() override { ++unexpected_calls; }
  void STDMETHODCALLTYPE ApplyLightingSystemDefault() override { ++unexpected_calls; }
  UNUSED_VALUE(std::uint32_t, GetTouchSurfaceCount)
  HRESULT STDMETHODCALLTYPE GetTouchSurfaceInfo(std::uint32_t, gi::GameInputTouchSurfaceInfo*) override { ++unexpected_calls; return E_NOTIMPL; }
#undef UNUSED_VALUE
#undef UNUSED_INFO
#undef UNUSED_ARRAY
};

class Runtime final : public gi::IGameInput {
 public:
  ULONG references = 1;
  unsigned adds = 0, releases = 0, registrations = 0, unregistrations = 0;
  bool gamepad_only = false, shutdown_callback_completed = false;
  gi::GameInputDeviceCallback callback = nullptr;
  void* context = nullptr;
  Device* shutdown_device = nullptr;
  static constexpr gi::GameInputCallbackToken expected_token = 0x4433;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** value) override { ++unexpected_calls; *value = nullptr; return E_NOINTERFACE; }
  ULONG STDMETHODCALLTYPE AddRef() override { ++adds; return ++references; }
  ULONG STDMETHODCALLTYPE Release() override { ++releases; return --references; }
  HRESULT STDMETHODCALLTYPE RegisterDeviceCallback(gi::IGameInputDevice* device, gi::GameInputKind kind, void* value,
      gi::GameInputDeviceCallback function, gi::GameInputCallbackToken* result) override {
    ++registrations;
    gamepad_only = device == nullptr && kind == gi::GameInputKindGamepad;
    context = value; callback = function; *result = expected_token; return S_OK;
  }
  bool STDMETHODCALLTYPE UnregisterCallback(gi::GameInputCallbackToken value) override {
    ++unregistrations;
    Check(value == expected_token, "the registered callback is unregistered");
    // A boundary-controlled in-flight callback completes during unregister.
    // This deadlocks if the adapter waits while holding its cache lock.
    if (shutdown_device && callback) {
      Emit(gi::GameInputDeviceEventConnected, *shutdown_device);
      shutdown_callback_completed = true;
    }
    callback = nullptr;
    return true;
  }
  void Emit(gi::GameInputDeviceEvent event, Device& device) {
    Check(callback != nullptr, "callback remains registered for device events");
    callback(expected_token, context, event, &device);
  }
  std::uint64_t STDMETHODCALLTYPE GetCurrentTimestamp() override { ++unexpected_calls; return 0; }
  HRESULT STDMETHODCALLTYPE GetCurrentReading(gi::GameInputKind, gi::IGameInputDevice*, gi::IGameInputReading**) override { ++unexpected_calls; return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetNextReading(gi::IGameInputReading*, gi::GameInputKind, gi::IGameInputDevice*, gi::IGameInputReading**) override { ++unexpected_calls; return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetPreviousReading(gi::IGameInputReading*, gi::GameInputKind, gi::IGameInputDevice*, gi::IGameInputReading**) override { ++unexpected_calls; return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE RegisterReadingCallback(gi::IGameInputDevice*, gi::GameInputKind, void*, gi::GameInputReadingCallback, gi::GameInputCallbackToken*) override { ++unexpected_calls; return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE CreateDispatcher(gi::IGameInputDispatcher**) override { ++unexpected_calls; return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE FindDeviceFromId(const APP_LOCAL_DEVICE_ID*, gi::IGameInputDevice**) override { ++unexpected_calls; return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE FindDeviceFromPlatformString(LPCWSTR, gi::IGameInputDevice**) override { ++unexpected_calls; return E_NOTIMPL; }
  void STDMETHODCALLTYPE EnableBackgroundInput(bool) override { ++unexpected_calls; }
  HRESULT STDMETHODCALLTYPE CreateAggregateDevice(gi::GameInputKind, APP_LOCAL_DEVICE_ID*) override { ++unexpected_calls; return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE DisableAggregateDevice(const APP_LOCAL_DEVICE_ID*) override { ++unexpected_calls; return E_NOTIMPL; }
};

Runtime runtime;
unsigned factory_calls = 0;
bool factory_iid_matched = false;
HRESULT WINAPI Factory(REFIID iid, void** result) {
  // Literal from the approved v4 ABI, independent of the SDK's IID constant
  // used by the production adapter.
  const GUID expected{0xf2daae4c, 0x4d21, 0x4250, {0x89, 0x53, 0x4f, 0x79, 0x1e, 0x62, 0x79, 0xab}};
  ++factory_calls;
  factory_iid_matched = std::memcmp(&iid, &expected, sizeof(expected)) == 0;
  *result = nullptr;
  if (!factory_iid_matched) return E_NOINTERFACE;
  runtime.AddRef(); *result = static_cast<gi::IGameInput*>(&runtime); return S_OK;
}
bool Zero(const gi::GameInputRumbleParams& value) {
  return value.lowFrequency == 0.0f && value.highFrequency == 0.0f &&
      value.leftTrigger == 0.0f && value.rightTrigger == 0.0f;
}
void Verify() {
  using S = edtrace::GameInputSubmission;
  const XINPUT_VIBRATION effect{65535, 32768}, zero{};
  Device a, b, late;
  edtrace::GameInputOutputPoll(true, true);
  Check(edtrace::GameInputOutputSubmit(0, effect) == S::NativeFallback, "missing runtime preserves native output");
  Check(edtrace::GameInputOutputStartControlled(nullptr) == ERROR_PROC_NOT_FOUND, "missing factory is reported");
  Check(edtrace::GameInputOutputSubmit(0, effect) == S::NativeFallback, "failed initialization preserves native output");
  Check(edtrace::GameInputOutputStartControlled(Factory) == ERROR_SUCCESS && factory_calls == 1 && factory_iid_matched,
      "exact v4 factory IID creates the runtime");
  Check(runtime.registrations == 1 && runtime.gamepad_only, "only gamepad device events are subscribed");
  edtrace::GameInputOutputPoll(true, true);
  Check(edtrace::GameInputOutputSubmit(0, effect) == S::NativeFallback, "no matching device preserves native output");

  a.index_valid = false;
  runtime.Emit(gi::GameInputDeviceEventConnected, a);
  edtrace::GameInputOutputPoll(true, true);
  Check(edtrace::GameInputOutputSubmit(0, effect) == S::NativeFallback && a.write_count == 0,
      "false slot result writing zero never selects a controller");
  a.index_valid = true; a.index = 1;
  runtime.Emit(gi::GameInputDeviceEventXInputIndexUpdated, a);
  edtrace::GameInputOutputPoll(true, true);
  Check(edtrace::GameInputOutputSubmit(0, effect) == S::NativeFallback && a.write_count == 0,
      "a different XInput slot is not selected");
  a.index = 0; a.motors = gi::GameInputRumbleLowFrequency;
  edtrace::GameInputOutputPoll(true, true);
  Check(edtrace::GameInputOutputSubmit(0, effect) == S::NativeFallback && a.write_count == 0,
      "both main motors are required for output substitution");
  a.motors = static_cast<gi::GameInputRumbleMotors>(3); a.connected = false;
  edtrace::GameInputOutputPoll(true, true);
  Check(edtrace::GameInputOutputSubmit(0, effect) == S::NativeFallback && a.write_count == 0,
      "a disconnected device with a stale slot report is not selected");
  a.connected = true;
  runtime.Emit(gi::GameInputDeviceEventXInputIndexUpdated, a);
  runtime.Emit(gi::GameInputDeviceEventConnected, a);  // Duplicate event, same device.
  edtrace::GameInputOutputPoll(true, true);
  Check(edtrace::GameInputOutputSubmit(0, effect) == S::Submitted && a.write_count == 1,
      "one connected slot-zero controller receives authored rumble");
  const auto& first = a.Last();
  Check(first.lowFrequency == 1.0f && first.highFrequency == 0.5000076295f &&
      first.leftTrigger == 0.0f && first.rightTrigger == 0.0f,
      "main motor words map to independent exact floats without trigger effects");
  Check(edtrace::GameInputOutputSubmit(0, zero) == S::Submitted && Zero(a.Last()),
      "authored zero reaches both main motors and clears triggers");
  Check(edtrace::GameInputOutputSubmit(0, effect, .25f, .75f) == S::Submitted &&
      a.Last().leftTrigger == 0 && a.Last().rightTrigger == 0,
      "unsupported trigger motors never receive emulated authored channels");
  a.motors = static_cast<gi::GameInputRumbleMotors>(15);
  Check(edtrace::GameInputOutputSubmit(0, effect, .25f, .75f) == S::Submitted &&
      a.Last().lowFrequency == 1 && a.Last().highFrequency == 0.5000076295f &&
      a.Last().leftTrigger == .25f && a.Last().rightTrigger == .75f,
      "four-motor GameInput receives original distinct authored trigger amplitudes");
  const auto before_trigger_only = edtrace::GameInputOutputGetStats().stops;
  Check(edtrace::GameInputOutputSubmit(0, zero, .125f, .5f) == S::Submitted &&
      a.Last().lowFrequency == 0 && a.Last().highFrequency == 0 &&
      a.Last().leftTrigger == .125f && a.Last().rightTrigger == .5f &&
      edtrace::GameInputOutputGetStats().stops == before_trigger_only,
      "trigger-only authored output is active vibration rather than a stop");
  Check(edtrace::GameInputOutputSubmit(0, zero) == S::Submitted && Zero(a.Last()),
      "a following native stop clears all four motors");
  a.motors = static_cast<gi::GameInputRumbleMotors>(3);
  const auto before_other_slot = a.write_count;
  Check(edtrace::GameInputOutputSubmit(1, effect) == S::NativeFallback && a.write_count == before_other_slot,
      "other controllers retain native output");
  Check(edtrace::GameInputOutputSubmit(0, effect) == S::Submitted, "nonzero output precedes identity loss");
  runtime.Emit(gi::GameInputDeviceEventConnected, b);
  edtrace::GameInputOutputPoll(true, true);
  Check(edtrace::GameInputOutputSubmit(0, effect) == S::NativeFallback && Zero(a.Last()) && b.write_count == 0,
      "ambiguous duplicate slots stop old output and preserve native fallback");
  b.index = 1;
  runtime.Emit(gi::GameInputDeviceEventXInputIndexUpdated, b);
  edtrace::GameInputOutputPoll(true, true);
  Check(edtrace::GameInputOutputSubmit(0, effect) == S::Submitted && !Zero(a.Last()),
      "slot update restores a unique controller without input reads");
  a.index = 1;
  Check(edtrace::GameInputOutputSubmit(0, effect) == S::NativeFallback && Zero(a.Last()),
      "submission revalidates a changed slot before forwarding");
  a.index = 0;
  edtrace::GameInputOutputPoll(true, true);
  Check(edtrace::GameInputOutputSubmit(0, effect) == S::Submitted, "effect resumes after identity is restored");
  a.connected = false;
  Check(edtrace::GameInputOutputSubmit(0, effect) == S::NativeFallback && Zero(a.Last()),
      "disconnect stops stale selected output before another submission");
  runtime.Emit(gi::GameInputDeviceEventDisconnected, a);
  edtrace::GameInputOutputPoll(true, true);
  Check(edtrace::GameInputOutputSubmit(0, effect) == S::NativeFallback, "disconnect event removes cached identity");
  a.connected = true;
  runtime.Emit(gi::GameInputDeviceEventConnected, a);
  edtrace::GameInputOutputPoll(true, true);
  Check(edtrace::GameInputOutputSubmit(0, effect) == S::Submitted, "reconnection can restore unique output");
  edtrace::GameInputOutputPoll(true, false);
  Check(Zero(a.Last()) && edtrace::GameInputOutputSubmit(0, effect) == S::Quiet,
      "focus loss stops rumble and requests quiet output");
  edtrace::GameInputOutputPoll(true, true);
  Check(edtrace::GameInputOutputSubmit(0, effect) == S::Submitted && !Zero(a.Last()),
      "restored focus permits fresh authored rumble");
  edtrace::GameInputOutputPoll(false, true);
  Check(Zero(a.Last()) && edtrace::GameInputOutputSubmit(0, effect) == S::NativeFallback,
      "disabling substitution stops the old backend and restores native output");
  edtrace::GameInputOutputPoll(true, true);
  Check(edtrace::GameInputOutputSubmit(0, effect) == S::Submitted, "fresh effect is active before shutdown");
  runtime.shutdown_device = &late;
  edtrace::GameInputOutputStop();
  Check(Zero(a.Last()) && runtime.unregistrations == 1 && runtime.shutdown_callback_completed && runtime.callback == nullptr,
      "shutdown stops output and lets an in-flight callback complete before context release");
  Check(runtime.references == 1 && runtime.adds == runtime.releases &&
      a.references == 1 && a.adds == a.releases && b.references == 1 && b.adds == b.releases &&
      late.references == 1 && late.adds == late.releases,
      "runtime and all cached device references are balanced after shutdown");
  edtrace::GameInputOutputStop();
  edtrace::GameInputOutputPoll(true, true);
  Check(edtrace::GameInputOutputSubmit(0, effect) == S::NativeFallback && runtime.unregistrations == 1,
      "stopped output cannot resume through a stale poll");
  Check(unexpected_calls == 0, "rumble forwarding never reads input or changes background aggregate raw input or system buttons");
}

// This is a process-deadlock backstop, not a timing expectation for production
// behavior. Normal checks depend only on synchronous controlled outcomes.
HANDLE finished = nullptr;
const wchar_t* result_path = nullptr;
DWORD WINAPI Watchdog(void*) {
  if (WaitForSingleObject(finished, 10000) == WAIT_TIMEOUT) {
    WriteResult(result_path, "FAIL: callback shutdown completes without deadlock");
    TerminateProcess(GetCurrentProcess(), 9);
  }
  return 0;
}
}

int wmain(int argc, wchar_t** argv) {
  HANDLE watchdog = nullptr;
  try {
    Check(argc == 2, "result-file argument");
    result_path = argv[1];
    finished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(finished != nullptr, "watchdog completion event created");
    watchdog = CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
    Check(watchdog != nullptr, "deadlock backstop created");
    Verify();
    SetEvent(finished);
    WaitForSingleObject(watchdog, INFINITE);
    CloseHandle(watchdog); CloseHandle(finished);
    Check(WriteResult(argv[1], "PASS"), "result file written");
    std::puts("PASS GameInput adapter: v4 identity, output-only registration, unique slot, exact motor values, stops, focus, fallback and balanced callback shutdown");
    return 0;
  } catch (const std::exception& error) {
    if (finished) SetEvent(finished);
    if (watchdog) { WaitForSingleObject(watchdog, INFINITE); CloseHandle(watchdog); }
    if (finished) CloseHandle(finished);
    if (argc == 2) WriteResult(argv[1], std::string("FAIL: ") + error.what());
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
