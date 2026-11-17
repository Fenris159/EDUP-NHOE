#include "edtrace/vr_auto_launch.hpp"
#include <array>
#include <atomic>
#include <cstring>
#include <string>

namespace edtrace { namespace {
HANDLE child=nullptr,stop_event=nullptr;
ULONGLONG next_check=0;
std::atomic<DWORD> error{ERROR_NOT_READY};
std::atomic<bool> wine{false},child_running{false};
std::atomic<std::uint64_t> attempts{0};

bool HmdPresent() noexcept {
  HMODULE module=nullptr;
  // Add a temporary module reference so a normal runtime unload cannot race
  // this read-only query. Do not load a runtime that the game has not loaded.
  if(!GetModuleHandleExW(0,L"openvr_api.dll",&module)) return false;
  const auto raw=GetProcAddress(module,"VR_IsHmdPresent");
  using Present=bool (__cdecl*)(); Present present=nullptr;
  static_assert(sizeof(present)==sizeof(raw)); std::memcpy(&present,&raw,sizeof(raw));
  const bool result=present&&present(); FreeLibrary(module); return result;
}
bool PayloadPath(std::wstring& path,std::wstring& directory) {
  std::array<wchar_t,32768> text{};
  const DWORD size=GetModuleFileNameW(nullptr,text.data(),static_cast<DWORD>(text.size()));
  if(!size||size>=text.size()) return false;
  path.assign(text.data(),size);
  const auto slash=path.find_last_of(L"\\/"); if(slash==std::wstring::npos) return false;
  directory=path.substr(0,slash)+L"\\ed-vr-haptics";
  path=directory+L"\\ed-vr-haptics.exe";
  const DWORD attributes=GetFileAttributesW(path.c_str());
  return attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));
}
void ReleaseChild() noexcept {
  if(child) { CloseHandle(child); child=nullptr; }
  if(stop_event) { CloseHandle(stop_event); stop_event=nullptr; }
  child_running.store(false,std::memory_order_release);
}
}
void VrAutoLaunchPoll(bool steamvr_allowed) noexcept {
  const DWORD incoming=GetLastError();
  try {
    if(child) {
      if(WaitForSingleObject(child,0)==WAIT_TIMEOUT) { SetLastError(incoming); return; }
      ReleaseChild();
    }
    const auto now=GetTickCount64();
    if(now<next_check) { SetLastError(incoming); return; }
    next_check=now+5000;
    const HMODULE ntdll=GetModuleHandleW(L"ntdll.dll");
    wine.store(ntdll&&GetProcAddress(ntdll,"wine_get_version"),std::memory_order_release);
    // Proton uses the native Linux companion installed in its per-game launch
    // wrapper. A second Windows VR client would address the wrong runtime.
    if(wine.load()||!steamvr_allowed||!HmdPresent()) { SetLastError(incoming); return; }
    std::wstring executable,directory;
    if(!PayloadPath(executable,directory)) { error.store(ERROR_FILE_NOT_FOUND); SetLastError(incoming); return; }
    const auto pid=std::to_wstring(GetCurrentProcessId());
    const auto name=L"Local\\EDVRHapticsStop-"+pid+L"-"+std::to_wstring(now);
    stop_event=CreateEventW(nullptr,TRUE,FALSE,name.c_str());
    if(!stop_event) { error.store(GetLastError()); SetLastError(incoming); return; }
    std::wstring command=L"\""+executable+L"\" --auto --game-pid "+pid+L" --parent-pid "+pid+L" --stop-event \""+name+L"\"";
    STARTUPINFOW startup{}; startup.cb=sizeof(startup); PROCESS_INFORMATION process{};
    attempts.fetch_add(1,std::memory_order_relaxed);
    if(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,
                      nullptr,directory.c_str(),&startup,&process)) {
      child=process.hProcess; CloseHandle(process.hThread);
      child_running.store(true,std::memory_order_release); error.store(ERROR_SUCCESS);
    } else { error.store(GetLastError()); ReleaseChild(); }
  } catch(...) { error.store(ERROR_NOT_ENOUGH_MEMORY); }
  SetLastError(incoming);
}
void VrAutoLaunchStop() noexcept {
  const DWORD incoming=GetLastError();
  if(stop_event) SetEvent(stop_event);
  if(child&&WaitForSingleObject(child,1500)==WAIT_TIMEOUT) {
    // Only the process handle returned by our own CreateProcess is eligible.
    // The helper has finite-duration haptic requests and also watches parent
    // lifetime; force termination is a last resort after polite shutdown.
    TerminateProcess(child,ERROR_CANCELLED); WaitForSingleObject(child,500);
  }
  ReleaseChild(); SetLastError(incoming);
}
VrAutoLaunchStats VrAutoLaunchGetStats() noexcept {
  return {error.load(),wine.load(),child_running.load(),attempts.load()};
}
}
