#include "dinput_api.hpp"
#include <cstring>
namespace {
DInputSnapshot snapshot{};
DWORD attach_thread = 0;
volatile LONG thread_attaches = 0;
class Object final : public IUnknown {
 public:
  HRESULT WINAPI QueryInterface(REFIID iid, void** out) override {
    const GUID unknown{0, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
    if (!out) return E_POINTER;
    if (std::memcmp(&iid, &unknown, sizeof(iid))) { *out = nullptr; return E_NOINTERFACE; }
    *out = this; AddRef(); return S_OK;
  }
  ULONG WINAPI AddRef() override { return ++references; }
  ULONG WINAPI Release() override { return --references; }
  ULONG references = 1;
} object;
const unsigned char data_format[8]{1, 2, 3, 4, 5, 6, 7, 8};
}
extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) attach_thread = GetCurrentThreadId();
  if (reason == DLL_THREAD_ATTACH) InterlockedIncrement(&thread_attaches);
  return TRUE;
}
extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version, REFIID iid, void** out, IUnknown* outer) {
  snapshot = {instance, version, iid, out, outer, GetLastError(), attach_thread, 0};
  if (out) *out = &object;
  SetLastError(0x55667788); return version == 0x800 ? S_OK : E_INVALIDARG;
}
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID, REFIID, void** out) {
  if (out) *out = &object;
  SetLastError(0x12345678); return E_NOINTERFACE;
}
extern "C" HRESULT WINAPI DllCanUnloadNow() { SetLastError(0x11112222); return S_FALSE; }
extern "C" HRESULT WINAPI DllRegisterServer() { SetLastError(0x22223333); return E_ACCESSDENIED; }
extern "C" HRESULT WINAPI DllUnregisterServer() { SetLastError(0x33334444); return E_FAIL; }
extern "C" const void* WINAPI GetdfDIJoystick() { SetLastError(0x44445555); return data_format; }
extern "C" void WINAPI DInputRead(DInputSnapshot* out) {
  *out = snapshot; out->process_attach_thread = attach_thread;
  out->thread_attach_count = InterlockedCompareExchange(&thread_attaches, 0, 0);
}
extern "C" IUnknown* WINAPI DInputObject() { return &object; }
