#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include "dinput_api.hpp"
#include "result_file.hpp"
#include "edtrace/restoration_runtime.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

namespace {
void Check(bool ok, const char* message) { if (!ok) { VerificationResult(std::string("FAIL: ") + message); std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); } }
template <typename T> T Symbol(HMODULE module, const char* name) {
  const auto raw = GetProcAddress(module, name); T value{}; std::memcpy(&value, &raw, sizeof(value)); Check(value != nullptr, name); return value;
}
using Create = HRESULT (WINAPI*)(HINSTANCE, DWORD, REFIID, void**, IUnknown*);
using Simple = HRESULT (WINAPI*)();
using Class = HRESULT (WINAPI*)(REFCLSID, REFIID, void**);
using Format = const void* (WINAPI*)();
using Status = DWORD (WINAPI*)(edtrace::RestorationStatus*, DWORD);
}
int wmain(int argc, wchar_t** argv) {
  if (argc == 4) verification_result_path = argv[3];
  Check(argc == 4, "proxy, mode and result file required");
  const bool system = std::wcscmp(argv[2], L"system") == 0;
  const bool chain = std::wcscmp(argv[2], L"chain") == 0;
  HMODULE proxy = LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
  Check(proxy != nullptr, "dinput proxy loads");
  if (chain) {
    const auto dependency = GetModuleHandleW(L"ed_rumble_dinput8_previous.dll");
    Check(dependency != nullptr, "previous proxy is loaded before first forwarded export");
    DInputSnapshot before{}; Symbol<DInputReadFn>(dependency, "DInputRead")(&before);
    Check(before.process_attach_thread == GetCurrentThreadId(), "previous proxy attaches on the original loading thread");
  }
  const char* names[]{"DirectInput8Create", "DllCanUnloadNow", "DllGetClassObject", "DllRegisterServer", "DllUnregisterServer", "GetdfDIJoystick"};
  for (unsigned i = 0; i < (chain ? 5U : 6U); ++i) {
    Check(GetProcAddress(proxy, names[i]) != nullptr && GetProcAddress(proxy, names[i]) == GetProcAddress(proxy, MAKEINTRESOURCEA(i + 1)), "standard names and ordinals agree");
  }
  if (chain) Check(GetProcAddress(proxy, "GetdfDIJoystick") == nullptr, "chain retains prior optional-export surface");
  const auto create = Symbol<Create>(proxy, "DirectInput8Create");
  const GUID iid{0xbf798031, 0x483a, 0x4da2, {0xaa, 0x99, 0x5d, 0x64, 0xed, 0x36, 0x97, 0x00}};
  const GUID unknown{0, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
  void* object = nullptr;
  SetLastError(0x31415926);
  const auto instance = GetModuleHandleW(nullptr);
  Check(create(instance, 0x800, iid, &object, nullptr) == S_OK && object != nullptr, "original DirectInput create succeeds");
  if (!system) {
    Check(GetLastError() == 0x55667788, "DirectInput backend LastError preserved");
    const auto backend = GetModuleHandleW(chain ? L"ed_rumble_dinput8_previous.dll" : L"ed_real_dinput8.dll"); Check(backend != nullptr, "controlled backend loaded");
    const auto read = Symbol<DInputReadFn>(backend, "DInputRead");
    const auto actual = Symbol<DInputObjectFn>(backend, "DInputObject")();
    DInputSnapshot request{}; read(&request);
    Check(object == actual && request.instance == instance && request.version == 0x800 && !std::memcmp(&request.iid, &iid, sizeof(iid)) &&
          request.output == &object && request.outer == nullptr && request.incoming_error == 0x31415926,
          "DirectInput arguments and original COM identity forwarded unchanged");
    void* identity = nullptr;
    Check(actual->QueryInterface(unknown, &identity) == S_OK && identity == actual && actual->Release() == 1,
          "original COM methods remain callable without a wrapper");
    object = nullptr; SetLastError(0xabcdef01);
    Check(create(instance, 0xbeef, iid, &object, actual) == E_INVALIDARG && object == actual && GetLastError() == 0x55667788,
          "failure result and backend output remain unchanged");
    read(&request); Check(request.outer == actual && request.incoming_error == 0xabcdef01, "aggregation argument preserved");
    Check(Symbol<Simple>(proxy, "DllCanUnloadNow")() == S_FALSE && GetLastError() == 0x11112222, "unload contract forwarded");
    Check(Symbol<Simple>(proxy, "DllRegisterServer")() == E_ACCESSDENIED && GetLastError() == 0x22223333, "registration forwarded");
    Check(Symbol<Simple>(proxy, "DllUnregisterServer")() == E_FAIL && GetLastError() == 0x33334444, "unregistration forwarded");
    if (!chain) Check(Symbol<Format>(proxy, "GetdfDIJoystick")() == Symbol<Format>(backend, "GetdfDIJoystick")(), "data format pointer forwarded");
    object = nullptr;
    Check(Symbol<Class>(proxy, "DllGetClassObject")(iid, iid, &object) == E_NOINTERFACE && object == actual && GetLastError() == 0x12345678,
          "class factory result forwarded unchanged");
  } else {
    auto* input = static_cast<IDirectInput8W*>(object);
    void* identity = nullptr;
    Check(input->QueryInterface(unknown, &identity) == S_OK && identity != nullptr, "system DirectInput interface is functional");
    static_cast<IUnknown*>(identity)->Release(); input->Release();
  }
  const auto status = Symbol<Status>(proxy, "EdRumbleRestorationGetStatus");
  edtrace::RestorationStatus value{};
  const auto deadline = GetTickCount64() + 10000;
  do { Check(status(&value, sizeof(value)) == 0, "restoration status available"); if (value.stopped) break; Sleep(10); } while (GetTickCount64() < deadline);
  Check(value.stopped && value.start_error == ERROR_REVISION_MISMATCH && !value.profile_match && !value.output_hook && !value.headroom &&
        !value.gain_enabled && !value.routing_ready && !value.motor_calls, "unknown executable leaves all restoration and motor output disabled");
  if (chain) {
    DInputSnapshot after{};
    Symbol<DInputReadFn>(GetModuleHandleW(L"ed_rumble_dinput8_previous.dll"), "DInputRead")(&after);
    Check(after.thread_attach_count > 0, "previous proxy still receives new thread notifications");
  }
  std::puts("PASS dinput exports, original interfaces/arguments/results, and disabled restoration on unknown build");
  Check(VerificationResult("PASS"), "result file written");
  return 0;
}
