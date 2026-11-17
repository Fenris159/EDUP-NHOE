#include <windows.h>
#include <unknwn.h>
#include "edtrace/restoration_runtime.hpp"
#include <array>
#include <cstring>
#include <string>

#ifdef ED_DINPUT_EARLY_CHAIN
extern "C" {
__declspec(dllimport) HRESULT WINAPI EdPreviousDirectInput8Create(HINSTANCE, DWORD, REFIID, void**, IUnknown*);
__declspec(dllimport) HRESULT WINAPI EdPreviousDllGetClassObject(REFCLSID, REFIID, void**);
__declspec(dllimport) HRESULT WINAPI EdPreviousDllCanUnloadNow();
__declspec(dllimport) HRESULT WINAPI EdPreviousDllRegisterServer();
__declspec(dllimport) HRESULT WINAPI EdPreviousDllUnregisterServer();
}
#endif

namespace {
HMODULE self = nullptr;
INIT_ONCE initialized = INIT_ONCE_STATIC_INIT;
DWORD backend_error = ERROR_NOT_READY;
using CreateFn = HRESULT (WINAPI*)(HINSTANCE, DWORD, REFIID, void**, IUnknown*);
using ClassFn = HRESULT (WINAPI*)(REFCLSID, REFIID, void**);
using SimpleFn = HRESULT (WINAPI*)();
using FormatFn = const void* (WINAPI*)();
CreateFn create = nullptr;
ClassFn get_class = nullptr;
SimpleFn can_unload = nullptr, register_server = nullptr, unregister_server = nullptr;
FormatFn format = nullptr;

template <typename T> T Symbol(HMODULE module, const char* name) noexcept {
  const auto raw = GetProcAddress(module, name); T result{};
  static_assert(sizeof(raw) == sizeof(result)); std::memcpy(&result, &raw, sizeof(result)); return result;
}
BOOL CALLBACK Initialize(PINIT_ONCE, PVOID, PVOID*) noexcept {
  try {
#ifdef ED_DINPUT_EARLY_CHAIN
    // A static dependency preserves the previous host's PROCESS_ATTACH timing
    // and THREAD_ATTACH notifications. Do not defer loading it to this export.
    create = &EdPreviousDirectInput8Create;
    get_class = &EdPreviousDllGetClassObject;
    can_unload = &EdPreviousDllCanUnloadNow;
    register_server = &EdPreviousDllRegisterServer;
    unregister_server = &EdPreviousDllUnregisterServer;
    backend_error = ERROR_SUCCESS;
    return TRUE;
#else
    std::array<wchar_t, 32768> path{};
    std::wstring filename;
#ifdef ED_DINPUT_VERIFICATION_BACKEND
    const DWORD count = GetModuleFileNameW(self, path.data(), static_cast<DWORD>(path.size()));
    if (!count || count >= path.size()) { backend_error = ERROR_BAD_PATHNAME; return TRUE; }
    filename.assign(path.data(), count); filename.resize(filename.find_last_of(L"\\/"));
    filename += L"\\backend\\ed_real_dinput8.dll";
#else
    const UINT count = GetSystemDirectoryW(path.data(), static_cast<UINT>(path.size()));
    if (!count || count >= path.size()) { backend_error = ERROR_BAD_PATHNAME; return TRUE; }
    filename = std::wstring(path.data(), count) + L"\\dinput8.dll";
#endif
    HMODULE backend = LoadLibraryExW(filename.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
#ifndef ED_DINPUT_VERIFICATION_BACKEND
    if (backend == self) {
      // Some Wine loader combinations reuse a basename despite the full path.
      // Only that case needs a private byte-identical system-DLL copy.
      const DWORD count = GetTempPathW(static_cast<DWORD>(path.size()), path.data());
      if (!count || count >= path.size()) { backend_error = ERROR_BAD_PATHNAME; return TRUE; }
      FILETIME time{}; GetSystemTimeAsFileTime(&time);
      const auto stamp = (static_cast<ULONGLONG>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
      const auto folder = std::wstring(path.data(), count) + L"EDRumbleDInput-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(stamp);
      if (!CreateDirectoryW(folder.c_str(), nullptr)) { backend_error = GetLastError(); return TRUE; }
      const auto copy = folder + L"\\ed_real_dinput8.dll";
      if (!CopyFileW(filename.c_str(), copy.c_str(), TRUE)) { backend_error = GetLastError(); return TRUE; }
      backend = LoadLibraryExW(copy.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
#endif
    if (!backend || backend == self) { backend_error = backend ? ERROR_BAD_EXE_FORMAT : GetLastError(); return TRUE; }
    create = Symbol<CreateFn>(backend, "DirectInput8Create");
    get_class = Symbol<ClassFn>(backend, "DllGetClassObject");
    can_unload = Symbol<SimpleFn>(backend, "DllCanUnloadNow");
    register_server = Symbol<SimpleFn>(backend, "DllRegisterServer");
    unregister_server = Symbol<SimpleFn>(backend, "DllUnregisterServer");
    format = Symbol<FormatFn>(backend, "GetdfDIJoystick");
    backend_error = create ? ERROR_SUCCESS : ERROR_PROC_NOT_FOUND;
#endif
  } catch (...) { backend_error = ERROR_NOT_ENOUGH_MEMORY; }
  return TRUE;
}
void Ensure() noexcept { InitOnceExecuteOnce(&initialized, Initialize, nullptr, nullptr); }
HRESULT Missing() noexcept { return HRESULT_FROM_WIN32(backend_error ? backend_error : ERROR_PROC_NOT_FOUND); }
}

extern "C" BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) { self = module; DisableThreadLibraryCalls(module); }
  return TRUE;
}
extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version, REFIID iid, void** result, IUnknown* outer) {
  const DWORD incoming = GetLastError(); Ensure();
  edtrace::RequestRestorationStart(self);
  SetLastError(incoming);
  // The original interface pointer and every COM method remain unwrapped.
  return create ? create(instance, version, iid, result, outer) : Missing();
}
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** result) {
  const DWORD incoming = GetLastError(); Ensure(); edtrace::RequestRestorationStart(self); SetLastError(incoming);
  return get_class ? get_class(clsid, iid, result) : Missing();
}
extern "C" HRESULT WINAPI DllCanUnloadNow() {
  const DWORD incoming = GetLastError(); Ensure(); SetLastError(incoming); return can_unload ? can_unload() : Missing();
}
extern "C" HRESULT WINAPI DllRegisterServer() {
  const DWORD incoming = GetLastError(); Ensure(); SetLastError(incoming); return register_server ? register_server() : Missing();
}
extern "C" HRESULT WINAPI DllUnregisterServer() {
  const DWORD incoming = GetLastError(); Ensure(); SetLastError(incoming); return unregister_server ? unregister_server() : Missing();
}
extern "C" const void* WINAPI GetdfDIJoystick() {
  const DWORD incoming = GetLastError(); Ensure(); SetLastError(incoming); return format ? format() : nullptr;
}
extern "C" DWORD WINAPI EdRumbleRestorationGetStatus(edtrace::RestorationStatus* result, DWORD size) {
  if (!result || size != sizeof(edtrace::RestorationStatus)) return ERROR_INVALID_PARAMETER;
  *result = edtrace::RestorationGetStatus(); return ERROR_SUCCESS;
}
extern "C" DWORD WINAPI EdRumbleRestorationStop(DWORD timeout_ms) { return edtrace::RestorationStop(timeout_ms); }
