#include "edtrace/xinput_output.hpp"
#include "edtrace/onfoot_runtime.hpp"
#include "mock_api.hpp"
#include "result_file.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

namespace {
void Check(bool ok, const char* message) {
  if (!ok) { VerificationResult(std::string("FAIL: ") + message); std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
template <typename T> T Symbol(HMODULE module, const char* name) {
  const auto raw = GetProcAddress(module, name); T value{}; std::memcpy(&value, &raw, sizeof(value)); Check(value != nullptr, name); return value;
}
using Set = DWORD (WINAPI*)(DWORD, XINPUT_VIBRATION*);
using Get = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);
using Caps = DWORD (WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
Set Stub(BYTE* image, std::uintptr_t return_rva, Set target) {
  // A real executable caller with the literal verified return RVA; no mock of
  // the internal classifier. The only external dependency is the DLL backend.
  BYTE* code = image + return_rva - 16;
  const BYTE prologue[]{0x48, 0x83, 0xec, 0x28, 0x48, 0xb8};
  std::memcpy(code, prologue, sizeof(prologue)); std::memcpy(code + 6, &target, 8);
  const BYTE tail[]{0xff, 0xd0, 0x48, 0x83, 0xc4, 0x28, 0xc3};
  std::memcpy(code + 14, tail, sizeof(tail));
  FlushInstructionCache(GetCurrentProcess(), code, 21);
  Set function{}; std::memcpy(&function, &code, sizeof(function)); return function;
}
}
int wmain(int argc, wchar_t** argv) {
  if (argc == 4) verification_result_path = argv[3];
  Check(argc == 4, "backend, mode and result file required");
  HMODULE library = LoadLibraryW(argv[1]); Check(library != nullptr, "controlled backend loads");
  const auto set = Symbol<Set>(library, "XInputSetState");
  const auto get = Symbol<Get>(library, "XInputGetState");
  const auto caps = Symbol<Caps>(library, "XInputGetCapabilities");
  const auto reset = Symbol<MockResetFn>(library, "MockReset");
  const auto read = Symbol<MockReadFn>(library, "MockRead");
  constexpr std::size_t size = 0x4d10000;
  auto* image = static_cast<BYTE*>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
  Check(image != nullptr, "owned caller image allocated");
  const auto engine = Stub(image, 0x5c8535, set);
  const auto wwise = Stub(image, 0x4d05312, set);
  const auto retry = Stub(image, 0x4d05395, set);
  std::array<BYTE, 32> get_before{}, caps_before{};
  std::memcpy(get_before.data(), reinterpret_cast<void*>(get), get_before.size());
  std::memcpy(caps_before.data(), reinterpret_cast<void*>(caps), caps_before.size());
  const bool verified = std::wcscmp(argv[2], L"unknown") != 0;
  const DWORD installed = edtrace::XInputOutputStart(set, reinterpret_cast<std::uintptr_t>(image), size, verified);
  Check(installed == (verified ? ERROR_SUCCESS : ERROR_REVISION_MISMATCH), "only verified profiles install motor hook");
  Check(!std::memcmp(get_before.data(), reinterpret_cast<void*>(get), get_before.size()) &&
        !std::memcmp(caps_before.data(), reinterpret_cast<void*>(caps), caps_before.size()), "detection and input entry points remain unchanged");
  reset();
  XINPUT_STATE state{}; SetLastError(0x31415926);
  Check(get(3, &state) == 0x10203040 && GetLastError() == 0x11223344 && state.dwPacketNumber == 0x13579bdf &&
        state.Gamepad.sThumbLX == -1234 && state.Gamepad.sThumbRY == 4567, "original input state, return and LastError preserved");
  XINPUT_CAPABILITIES capability{}; SetLastError(0x12344321);
  Check(caps(2, 0x3333, &capability) == 0x20304050 && GetLastError() == 0x22334455 && capability.Flags == 0x1357 &&
        capability.Vibration.wRightMotorSpeed == 0xabcd, "original detection capabilities preserved");
  XINPUT_VIBRATION pulse{12345, 54321}, zero{};
  MockSnapshot snapshot{};
  edtrace::XInputOutputSetReady(true, true);
  SetLastError(0x10293847);
  Check(wwise(1, &pulse) == 0 && GetLastError() == 0x88776655, "Wwise backend result preserved");
  read(&snapshot);
  Check(snapshot.index == (verified ? 0U : 1U) && snapshot.left == 12345 && snapshot.right == 54321 &&
        snapshot.pointer == reinterpret_cast<ULONG_PTR>(&pulse) && snapshot.incoming_error == 0x10293847,
        "only verified Wwise output routes to zero with original pointer and words");
  const auto calls = snapshot.set_calls;
  SetLastError(0x55554444); Check(engine(0, &zero) == 0, "engine stop returns success");
  if (verified) Check(GetLastError() == 0x55554444, "suppressed engine stop preserves incoming LastError");
  read(&snapshot); Check(snapshot.set_calls == calls + (verified ? 0 : 1), "fresh Wwise effect survives competing engine zero");
  Check(wwise(1, &zero) == 0, "authored Wwise stop accepted");
  read(&snapshot); Check(snapshot.left == 0 && snapshot.right == 0 && snapshot.set_calls == calls + (verified ? 1 : 2), "authored zero is forwarded");
  Check(engine(0, &zero) == 0, "engine stop after authored stop forwarded");
  read(&snapshot); Check(snapshot.set_calls == calls + (verified ? 2 : 3), "authored stop releases ownership");
  if (verified) {
    Check(wwise(1, &pulse) == 0, "authored frame before supplemental test");
    edtrace::XInputOutputSetOnFootFrame({.5f,.25f,.25f,.5f},true); read(&snapshot);
    Check(snapshot.left == 32767 && snapshot.right == 54321 && snapshot.index == 0,
          "supplemental main motors mix by maximum with exact authored words");
    const auto mixed_calls = snapshot.set_calls;
    Check(engine(0,&zero) == 0, "engine zero during supplemental pulse accepted"); read(&snapshot);
    Check(snapshot.set_calls == mixed_calls, "fresh supplemental effect survives competing engine zero");
    Check(wwise(1,&zero) == 0, "authored stop during supplemental effect accepted"); read(&snapshot);
    Check(snapshot.left == 32767 && snapshot.right == 16383,
          "authored zero does not erase an independent supplemental pulse");
    edtrace::XInputOutputSetOnFootFrame({},true); read(&snapshot);
    Check(snapshot.left == 0 && snapshot.right == 0, "supplemental expiry restores the authored zero");
    Check(wwise(1,&pulse) == 0, "authored frame before cancellation");
    edtrace::XInputOutputSetOnFootFrame({.5f,0,0,0},true);
    edtrace::XInputOutputSetOnFootFrame({},false); read(&snapshot);
    Check(snapshot.left == 12345 && snapshot.right == 54321,"supplemental disable preserves original authored output");
    edtrace::XInputOutputSetOnFootFrame({.5f,0,0,0},true);
    edtrace::XInputOutputSetReady(false,true,false);
    edtrace::XInputOutputSetOnFootFrame({},false); read(&snapshot);
    Check(snapshot.left == 0 && snapshot.right == 0, "context loss cancels without replaying cached authored motion");
    edtrace::XInputOutputSetReady(true,true,true);
    edtrace::XInputOutputSetOnFootFrame({.25f,0,0,0},true); read(&snapshot);
    Check(snapshot.left == 16383 && snapshot.right == 0, "resumed supplemental effect does not resurrect pre-focus authored motion");
    edtrace::XInputOutputSetOnFootFrame({},true);
    Check(wwise(1,&zero) == 0, "authored frame reset after supplemental test");
    Check(wwise(1,&pulse) == 0,"fallback authored effect before focus loss");
    edtrace::XInputOutputSetReady(false,true,false); read(&snapshot);
    Check(snapshot.index == 0 && snapshot.left == 0 && snapshot.right == 0,
          "focus loss explicitly stops fallback without a supplemental pulse");
    XINPUT_VIBRATION unrelated = pulse;
    Check(wwise(7,&unrelated) == 0x76543210 && GetLastError() == 0x4040 &&
          unrelated.wLeftMotorSpeed == 0xbeef && unrelated.wRightMotorSpeed == 0x1357,
          "background unrelated index preserves native errors and pointer mutation"); read(&snapshot);
    const auto quiet_calls = snapshot.set_calls;
    SetLastError(0x55667788);
    Check(wwise(0,&pulse) == 0 && GetLastError() == 0x55667788,"background authored request is quiet success"); read(&snapshot);
    Check(snapshot.set_calls == quiet_calls,"background authored frames cannot restart fallback vibration");
    // A focused VR session can remain ready while the desktop gamepad is quiet.
    edtrace::XInputOutputSetReady(true,true,false);
    Check(wwise(1,&pulse) == 0,"focused VR output can coexist with quiet desktop fallback"); read(&snapshot);
    Check(snapshot.set_calls == quiet_calls,"VR readiness never enables unfocused XInput fallback");
    unrelated = pulse;
    Check(wwise(7,&unrelated) == 0x76543210 && GetLastError() == 0x4040 &&
          unrelated.wLeftMotorSpeed == 0xbeef && unrelated.wRightMotorSpeed == 0x1357,
          "VR background quieting preserves unrelated index contract");
    edtrace::XInputOutputSetReady(true,true,true);
    Check(wwise(1,&pulse) == 0,"fresh foreground authored request resumes fallback");
    edtrace::XInputOutputSetReady(false,false,false); read(&snapshot);
    Check(snapshot.index == 0 && snapshot.left == 0 && snapshot.right == 0,"shutdown explicitly stops the last owned fallback frame");
    const auto shutdown_calls = snapshot.set_calls;
    Check(wwise(0,&pulse) == 0,"late authored request after shutdown stays quiet"); read(&snapshot);
    Check(snapshot.set_calls == shutdown_calls,"shutdown cannot replay late fallback output");
    edtrace::XInputOutputSetReady(true,true,true);
    edtrace::StartOnFootFeedback(.5f);
    edtrace::TickOnFootFeedback(true,900);
    edtrace::QueueOnFootInput({edtrace::OnFootInputKind::Register,1100,1000,0,0,0,1,0});
    edtrace::QueueOnFootInput({edtrace::OnFootInputKind::Parameter,1100,1000,1497268741,0,0,1,1});
    edtrace::QueueOnFootInput({edtrace::OnFootInputKind::Parameter,1100,1000,103566125,0,0,1,1});
    edtrace::QueueOnFootInput({edtrace::OnFootInputKind::Switch,1100,1000,1979902012,998496889,0,1,0});
    read(&snapshot); const auto queued_calls = snapshot.set_calls;
    edtrace::QueueOnFootInput({edtrace::OnFootInputKind::Post,1100,1000,1697380579,0,1,0,0});
    read(&snapshot); Check(snapshot.set_calls == queued_calls,"audio-side event admission issues no controller writes");
    edtrace::TickOnFootFeedback(true,1000); read(&snapshot);
    Check(snapshot.left == 3932 && snapshot.right == 5898,"queued local rifle onset reaches native fallback at configured strength");
    edtrace::TickOnFootFeedback(true,1100); read(&snapshot);
    Check(snapshot.left == 983 && snapshot.right == 2621,"queued held effect produces its intended sustained fallback");
    edtrace::QueueOnFootInput({edtrace::OnFootInputKind::Post,1100,1100,1119934880,0,1,0,0});
    edtrace::TickOnFootFeedback(true,1100); read(&snapshot);
    Check(snapshot.left == 0 && snapshot.right == 0,"queued native release reaches the physical-output boundary");
    edtrace::QueueOnFootInput({edtrace::OnFootInputKind::Post,1100,1200,3666194424,0,1,0,0});
    edtrace::TickOnFootFeedback(true,1212); read(&snapshot);
    Check(snapshot.left == 19660 && snapshot.right == 13107,"queued jetpack ignition reaches output at configured strength");
    edtrace::TickOnFootFeedback(true,1290); read(&snapshot);
    Check(snapshot.left == 9830 && snapshot.right == 5242,"queued jetpack sustained state reaches output");
    edtrace::QueueOnFootInput({edtrace::OnFootInputKind::Post,1100,1300,3666194431,0,1,0,0});
    edtrace::TickOnFootFeedback(true,1350); read(&snapshot);
    Check(snapshot.left == 4915 && snapshot.right == 2621,"queued jetpack cutoff fades at output");
    edtrace::TickOnFootFeedback(true,1400); read(&snapshot);
    Check(snapshot.left == 0 && snapshot.right == 0,"queued jetpack cutoff ends at output");
    edtrace::QueueOnFootInput({edtrace::OnFootInputKind::Register,1200,1500,0,0,0,1,0});
    edtrace::QueueOnFootInput({edtrace::OnFootInputKind::Parameter,1200,1500,1497268741,0,0,1,1});
    edtrace::QueueOnFootInput({edtrace::OnFootInputKind::Switch,1200,1500,1979902012,931160808,0,1,0});
    edtrace::QueueOnFootInput({edtrace::OnFootInputKind::Post,1200,1500,476288594,0,1,0,0});
    edtrace::TickOnFootFeedback(true,1500); read(&snapshot);
    Check(snapshot.left == 1310 && snapshot.right == 2293,"queued mounted shot reaches fallback without an on-foot inside marker");
    edtrace::TickOnFootFeedback(true,1560); read(&snapshot);
    Check(snapshot.left == 0 && snapshot.right == 0,"queued mounted shot expires without held-fire output");
    edtrace::QueueOnFootInput({edtrace::OnFootInputKind::Post,1200,1600,313889439,0,1,0,0});
    edtrace::TickOnFootFeedback(true,1625); read(&snapshot);
    Check(snapshot.left == 13107 && snapshot.right == 3932,"queued missile launch reaches its ejection peak");
    edtrace::TickOnFootFeedback(true,1820); read(&snapshot);
    Check(snapshot.left == 0 && snapshot.right == 0,"queued missile ejection ends at output");
    edtrace::QueueOnFootInput({edtrace::OnFootInputKind::Register,1400,1840,0,0,0,1,0});
    edtrace::QueueOnFootInput({edtrace::OnFootInputKind::Parameter,1400,1840,1497268741,0,0,1,1});
    edtrace::QueueOnFootInput({edtrace::OnFootInputKind::Parameter,1400,1840,103566125,0,0,1,1});
    edtrace::TickOnFootFeedback(false,1840);
    edtrace::QueueOnFootInput({edtrace::OnFootInputKind::Parameter,1400,1850,2410709172,0,0,1,6});
    edtrace::TickOnFootFeedback(true,1860);
    edtrace::QueueOnFootInput({edtrace::OnFootInputKind::Post,1400,1870,63371630,0,1,0,0});
    edtrace::TickOnFootFeedback(true,1870); read(&snapshot);
    Check(snapshot.left == 0 && snapshot.right == 0,"queued inactive speed cannot enable running after focus regain");
    edtrace::QueueOnFootInput({edtrace::OnFootInputKind::Parameter,1400,1880,2410709172,0,0,1,6});
    edtrace::QueueOnFootInput({edtrace::OnFootInputKind::Post,1400,1880,63371630,0,1,0,0});
    edtrace::TickOnFootFeedback(true,1880); read(&snapshot);
    Check(snapshot.left == 2621 && snapshot.right == 1966,"fresh foreground running metadata restores contact feedback");
    edtrace::TickOnFootFeedback(false,1940);
    edtrace::QueueOnFootInput({edtrace::OnFootInputKind::Post,1100,1950,1697380579,0,1,0,0});
    edtrace::TickOnFootFeedback(true,1960); read(&snapshot);
    Check(snapshot.left == 0 && snapshot.right == 0,"queued background weapon onset cannot replay after focus regain");
    edtrace::QueueOnFootInput({edtrace::OnFootInputKind::Post,1100,2000,1697380579,0,1,0,0});
    edtrace::TickOnFootFeedback(true,2000);
    for (unsigned i = 0; i < 4096; ++i)
      edtrace::QueueOnFootInput({edtrace::OnFootInputKind::Post,9000,2001,1697380579,0,1,0,0});
    edtrace::TickOnFootFeedback(true,2001); read(&snapshot);
    const auto feedback = edtrace::GetOnFootRuntimeStats();
    Check(feedback.faulted && !feedback.active && feedback.dropped == 2048 && snapshot.left == 0 && snapshot.right == 0,
          "input queue loss disables new effects and sends zero");
    edtrace::StopOnFootFeedback();
  }
  Check(retry(1, &pulse) == 0, "termination retry accepted");
  read(&snapshot); Check(snapshot.index == (verified ? 0U : 1U), "termination retry uses same verified controller");
  Check(set(7, &pulse) == 0x76543210 && GetLastError() == 0x4040 && pulse.wLeftMotorSpeed == 0xbeef && pulse.wRightMotorSpeed == 0x1357,
        "unknown callers retain backend errors and pointer mutations");
  Check(set(0, nullptr) == ERROR_INVALID_PARAMETER && GetLastError() == 0x55aa0000, "null request forwarded unchanged");
  Check(set(0, reinterpret_cast<XINPUT_VIBRATION*>(1)) == ERROR_NOACCESS && GetLastError() == 0x55aa0001, "unreadable request forwarded unchanged");
  edtrace::XInputOutputSetReady(true, false);
  Check(wwise(1, &zero) == 0, "disconnected target call remains native"); read(&snapshot);
  Check(snapshot.index == 1, "disconnect disables remapping without changing detection");
  edtrace::XInputOutputSetReady(false, true);
  Check(wwise(1, &zero) == 0, "disabled restoration remains native"); read(&snapshot);
  Check(snapshot.index == 1, "unverified gain disables remapping");
  std::puts("PASS output interception, authored stops, ownership, input/detection preservation and profile/readiness guards");
  Check(VerificationResult("PASS"), "result file written");
  return 0;
}
