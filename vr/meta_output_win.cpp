#include "edtrace/meta_output.hpp"
#include "edtrace/vr_backend.hpp"
#include "edtrace/vr_haptics.hpp"
#include "MinHook.h"
#include <tlhelp32.h>
#include <array>
#include <atomic>
#include <cstring>

namespace edtrace { namespace {
// Public LibOVR C ABI. Frame parameters stay opaque: no eye images, poses,
// input state, tracking data, or control bindings are read or changed.
using Session=void*;
using Result=std::int32_t;
using Frame=Result (__cdecl*)(Session,long long,const void*,const void* const*,unsigned);
using Create=Result (__cdecl*)(Session*,void*);
using Destroy=void (__cdecl*)(Session);
using Shutdown=void (__cdecl*)();
using GetStatus=Result (__cdecl*)(Session,void*);
using SetVibration=Result (__cdecl*)(Session,unsigned,float,float);
constexpr unsigned kLeftTouch=1,kRightTouch=2;
// Modern ovrSessionStatus has nine one-byte flags. Reserve append space for
// runtime ABI additions; fields through HasInputFocus are the SDK prefix.
struct Status {
  unsigned char visible=0,present=0,mounted=0,display_lost=0,quit=0,recenter=0;
  unsigned char input_focus=0,overlay=0,depth=0;
  std::array<unsigned char,247> reserved{};
};
static_assert(offsetof(Status,input_focus)==6&&sizeof(Status)==256);
constexpr std::uint64_t kRenderLease=250;
struct Lifetime { Session session=nullptr; bool destroyed=false; };
SRWLOCK session_lock=SRWLOCK_INIT;
std::array<Lifetime,64> lifetimes{};
Session session=nullptr;
bool render_known=false,was_owned=false,stopped=false,shutdown_seen=false;
std::uint64_t rendered=0,next_scan=0,retry_after=0;
GetStatus get_status=nullptr;
SetVibration set_vibration=nullptr;
std::array<Frame,3> original_frames{};
Create original_create=nullptr;
Destroy original_destroy=nullptr;
Shutdown original_shutdown=nullptr;
std::atomic<bool> observing{false},installed{false},has_session{false},focused{false},owned{false};
std::atomic<DWORD> error{ERROR_NOT_READY};
std::atomic<std::uint64_t> submissions{0},stops{0},sessions{0};

template<class T> T Symbol(HMODULE module,const char* name) noexcept {
  const auto raw=GetProcAddress(module,name); T result{};
  static_assert(sizeof(raw)==sizeof(result)); std::memcpy(&result,&raw,sizeof(result)); return result;
}
std::uint64_t Now() noexcept {
#ifdef ED_META_CONTROLLED_CLOCK
  extern std::uint64_t MetaControlledClock() noexcept;
  return MetaControlledClock();
#else
  return GetTickCount64();
#endif
}
Lifetime* Find(Session value,bool allocate) noexcept {
  for(auto& item:lifetimes) if(item.session==value) return &item;
  if(allocate) for(auto& item:lifetimes) if(!item.session) { item.session=value; return &item; }
  return nullptr;
}
void Relinquish(bool release=true) noexcept {
  // An inactive adapter cannot continually zero the selected adapter's motors.
  if(was_owned&&session&&set_vibration) {
    const auto left=set_vibration(session,kLeftTouch,0,0);
    const auto right=set_vibration(session,kRightTouch,0,0);
    if(left>=0&&right>=0) VrOutputAcknowledgeStop(static_cast<DWORD>(VrBackend::Meta));
    else error.store(ERROR_WRITE_FAULT);
    stops.fetch_add(1,std::memory_order_relaxed);
  }
  was_owned=false; owned.store(false,std::memory_order_release);
  if(release) { VrBridgeFrame frame{}; VrOutputReadFrame(static_cast<DWORD>(VrBackend::Meta),FALSE,&frame); }
}
void Observe(Session value,bool created) noexcept {
  if(!value||!observing.load(std::memory_order_acquire)) return;
  AcquireSRWLockExclusive(&session_lock);
  if(!observing.load(std::memory_order_acquire)) { ReleaseSRWLockExclusive(&session_lock); return; }
  if(shutdown_seen&&!created) { ReleaseSRWLockExclusive(&session_lock); return; }
  auto* lifetime=Find(value,true);
  if(!lifetime) { Relinquish(); error.store(ERROR_BUFFER_OVERFLOW); observing.store(false); }
  else if(created||!lifetime->destroyed) {
    if(created) { lifetime->destroyed=false; shutdown_seen=false; }
    if(session!=value) {
      Relinquish(); session=value; retry_after=0; render_known=false;
      has_session.store(true); sessions.fetch_add(1);
    }
    if(!created) { rendered=Now(); render_known=true; }
  }
  ReleaseSRWLockExclusive(&session_lock);
}
Result Render(unsigned slot,Session value,long long index,const void* scale,const void* const* layers,unsigned count) noexcept {
  const DWORD incoming=GetLastError(); SetLastError(incoming);
  const Result result=original_frames[slot](value,index,scale,layers,count);
  const DWORD outgoing=GetLastError();
  if(result>=0) Observe(value,false);
  SetLastError(outgoing); return result;
}
Result __cdecl Frame0(Session value,long long index,const void* scale,const void* const* layers,unsigned count) noexcept { return Render(0,value,index,scale,layers,count); }
Result __cdecl Frame1(Session value,long long index,const void* scale,const void* const* layers,unsigned count) noexcept { return Render(1,value,index,scale,layers,count); }
Result __cdecl Frame2(Session value,long long index,const void* scale,const void* const* layers,unsigned count) noexcept { return Render(2,value,index,scale,layers,count); }
Result __cdecl Created(Session* result,void* luid) noexcept {
  const DWORD incoming=GetLastError(); SetLastError(incoming);
  const Result code=original_create(result,luid); const DWORD outgoing=GetLastError();
  Session value=nullptr; SIZE_T bytes=0;
  if(code>=0&&result&&ReadProcessMemory(GetCurrentProcess(),result,&value,sizeof(value),&bytes)&&bytes==sizeof(value)) Observe(value,true);
  SetLastError(outgoing); return code;
}
void __cdecl Destroyed(Session value) noexcept {
  const DWORD incoming=GetLastError();
  AcquireSRWLockExclusive(&session_lock);
  if(observing.load(std::memory_order_acquire)) {
    if(auto* lifetime=Find(value,true)) lifetime->destroyed=true;
    else { error.store(ERROR_BUFFER_OVERFLOW); observing.store(false); }
    if(session==value) {
      Relinquish(); session=nullptr; render_known=false;
      has_session.store(false); focused.store(false);
    }
  }
  ReleaseSRWLockExclusive(&session_lock);
  SetLastError(incoming); original_destroy(value);
}
void __cdecl ShutdownRuntime() noexcept {
  const DWORD incoming=GetLastError();
  AcquireSRWLockExclusive(&session_lock);
  Relinquish();
  for(auto& lifetime:lifetimes) if(lifetime.session) lifetime.destroyed=true;
  shutdown_seen=true; session=nullptr; render_known=false;
  has_session.store(false); focused.store(false);
  ReleaseSRWLockExclusive(&session_lock);
  SetLastError(incoming); original_shutdown();
}
template<class T> bool Pin(T function) noexcept {
  if(!function) return true;
  void* address=nullptr; std::memcpy(&address,&function,sizeof(address)); HMODULE module=nullptr;
  return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
    reinterpret_cast<LPCWSTR>(address),&module)!=FALSE;
}
bool Install(HMODULE module) noexcept {
  const auto status=Symbol<GetStatus>(module,"ovr_GetSessionStatus");
  const auto vibration=Symbol<SetVibration>(module,"ovr_SetControllerVibration");
  const auto destroy=Symbol<Destroy>(module,"ovr_Destroy");
  const auto shutdown=Symbol<Shutdown>(module,"ovr_Shutdown");
  const auto create=Symbol<Create>(module,"ovr_Create");
  const std::array<Frame,3> frames{{Symbol<Frame>(module,"ovr_EndFrame"),Symbol<Frame>(module,"ovr_SubmitFrame"),Symbol<Frame>(module,"ovr_SubmitFrame2")}};
  if(!status||!vibration||!destroy||(!frames[0]&&!frames[1]&&!frames[2])) return false;
  bool pinned=Pin(status)&&Pin(vibration)&&Pin(destroy)&&Pin(shutdown)&&Pin(create);
  for(const auto frame:frames) pinned=pinned&&Pin(frame);
  if(!pinned) { error.store(GetLastError()); return false; }
  const auto initialization=MH_Initialize();
  if(initialization!=MH_OK&&initialization!=MH_ERROR_ALREADY_INITIALIZED) { error.store(ERROR_NOT_SUPPORTED); return false; }
  std::array<void*,6> targets{}; unsigned size=0;
  const auto add=[&](auto function,auto detour,auto& original) {
    if(!function) return true;
    void* target=nullptr; void* replacement=nullptr;
    std::memcpy(&target,&function,sizeof(target)); std::memcpy(&replacement,&detour,sizeof(replacement));
    for(unsigned i=0;i<size;++i) if(targets[i]==target) return true;
    void* trampoline=nullptr;
    if(MH_CreateHook(target,replacement,&trampoline)!=MH_OK) return false;
    std::memcpy(&original,&trampoline,sizeof(original)); targets[size++]=target; return true;
  };
  bool success=add(destroy,&Destroyed,original_destroy)&&add(shutdown,&ShutdownRuntime,original_shutdown)&&add(create,&Created,original_create);
  const std::array<Frame,3> detours{{&Frame0,&Frame1,&Frame2}};
  for(unsigned i=0;i<3&&success;++i) success=add(frames[i],detours[i],original_frames[i]);
  if(!success) {
    for(unsigned i=0;i<size;++i) MH_RemoveHook(targets[i]);
    error.store(ERROR_NOT_SUPPORTED); return false;
  }
  get_status=status; set_vibration=vibration;
  // Destroy is enabled first. Hooks remain resident and only forward after
  // stop; neither a runtime DLL nor a live trampoline is unloaded/removed.
  for(unsigned i=0;i<size;++i) if(MH_EnableHook(targets[i])!=MH_OK) {
    observing.store(false); stopped=true; error.store(ERROR_NOT_SUPPORTED); return false;
  }
  installed.store(true,std::memory_order_release); observing.store(true,std::memory_order_release);
  error.store(ERROR_SUCCESS); return true;
}
}

void MetaOutputPoll() noexcept {
  const DWORD incoming=GetLastError(); const auto now=Now();
  if(!installed.load(std::memory_order_acquire)&&!stopped&&now>=next_scan) {
    next_scan=now+500;
    const HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());
    if(snapshot!=INVALID_HANDLE_VALUE) {
      MODULEENTRY32W entry{}; entry.dwSize=sizeof(entry); unsigned count=0;
      if(Module32FirstW(snapshot,&entry)) do {
        if(Install(entry.hModule)) break;
      } while(++count<1024&&Module32NextW(snapshot,&entry));
      CloseHandle(snapshot);
    }
  }
  AcquireSRWLockExclusive(&session_lock);
  bool ready=false;
  if(observing.load(std::memory_order_acquire)&&session&&render_known&&now>=rendered&&now-rendered<kRenderLease&&now>=retry_after) {
    Status status{};
    const auto result=get_status(session,&status);
    ready=result>=0&&status.visible&&status.input_focus&&!status.display_lost&&!status.quit;
  }
  focused.store(ready,std::memory_order_release);
  VrBridgeFrame frame{};
  const bool selected=VrOutputReadFrame(static_cast<DWORD>(VrBackend::Meta),ready?TRUE:FALSE,&frame)!=FALSE;
  if(selected&&ready&&session) {
    const auto hands=vrbridge::MapHands(frame.frame,.6f);
    const auto frequency=[](vrbridge::HandPulse pulse) { return pulse.amplitude<=0?0.0f:pulse.frequency_hz>=120?1.0f:.5f; };
    const auto left=set_vibration(session,kLeftTouch,frequency(hands.left),hands.left.amplitude);
    const auto right=set_vibration(session,kRightTouch,frequency(hands.right),hands.right.amplitude);
    was_owned=true; owned.store(true,std::memory_order_release); submissions.fetch_add(1);
    if(left<0||right<0) { Relinquish(); retry_after=now+2000; error.store(ERROR_WRITE_FAULT); }
    else {
      if(hands.left.amplitude==0&&hands.right.amplitude==0) VrOutputAcknowledgeStop(static_cast<DWORD>(VrBackend::Meta));
      error.store(ERROR_SUCCESS);
    }
  } else Relinquish(!ready);
  ReleaseSRWLockExclusive(&session_lock); SetLastError(incoming);
}
void MetaOutputStop() noexcept {
  const DWORD incoming=GetLastError();
  AcquireSRWLockExclusive(&session_lock);
  stopped=true; observing.store(false,std::memory_order_release); Relinquish();
  focused.store(false); session=nullptr; render_known=false; has_session.store(false);
  ReleaseSRWLockExclusive(&session_lock); SetLastError(incoming);
}
MetaOutputStats MetaOutputGetStats() noexcept {
  return {error.load(),installed.load(),has_session.load(),focused.load(),owned.load(),submissions.load(),stops.load(),sessions.load()};
}
}
