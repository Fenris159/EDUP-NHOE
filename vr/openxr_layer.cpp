#define XR_NO_PROTOTYPES
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
#include "edtrace/vr_backend.hpp"
#include "edtrace/vr_haptics.hpp"
#include "edtrace/openxr_haptics_status.hpp"
#include "edtrace/openxr_haptic_routing.hpp"
#include <array>
#include <atomic>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <chrono>
#include <cerrno>
#ifndef _WIN32
#include "native_xr_relay.hpp"
#include "edtrace/openxr_native_identity.hpp"
#include <dlfcn.h>
#include <fstream>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#endif
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#define ED_XR_EXPORT
#else
#define ED_XR_EXPORT __attribute__((visibility("default")))
#endif
namespace {
using edtrace::VrDword;
using edtrace::VrBool;
constexpr char kLayerName[]="XR_APILAYER_ED_haptics";
constexpr std::size_t kMaximumCallerArray=4096;
constexpr char kTouchProExtension[]="XR_FB_touch_controller_pro";
struct LastError {
#ifdef _WIN32
  DWORD value=GetLastError();
  void Before() const noexcept { SetLastError(value); }
  void After() noexcept { value=GetLastError(); }
  ~LastError() { SetLastError(value); }
#else
  int value=errno;
  void Before() const noexcept { errno=value; }
  void After() noexcept { value=errno; }
  ~LastError() { errno=value; }
#endif
};
#ifdef _WIN32
constexpr edtrace::VrBackend kBackend=edtrace::VrBackend::OpenXR;
constexpr char kActionSetName[]="ed_vr_proxy_haptics_v1";
constexpr char kActionSetLocalized[]="Elite haptics output";
constexpr const char* kActionNames[]{"ed_vr_left_body","ed_vr_right_body","ed_vr_left_trigger","ed_vr_right_trigger"};
constexpr const char* kActionLabels[]{"Elite left hand vibration","Elite right hand vibration","Elite left trigger vibration","Elite right trigger vibration"};
#else
constexpr edtrace::VrBackend kBackend=edtrace::VrBackend::NativeOpenXR;
constexpr char kActionSetName[]="ed_vr_native_haptics_v1";
constexpr char kActionSetLocalized[]="Elite native haptics output";
constexpr const char* kActionNames[]{"ed_vr_native_left_body","ed_vr_native_right_body","ed_vr_native_left_trigger","ed_vr_native_right_trigger"};
constexpr const char* kActionLabels[]{"Elite native left hand vibration","Elite native right hand vibration","Elite native left trigger vibration","Elite native right trigger vibration"};
#endif
struct Dispatch {
  PFN_xrGetInstanceProcAddr GetInstanceProcAddr=nullptr;
  PFN_xrDestroyInstance DestroyInstance=nullptr;
  PFN_xrCreateSession CreateSession=nullptr;
  PFN_xrDestroySession DestroySession=nullptr;
  PFN_xrEndSession EndSession=nullptr;
  PFN_xrEndFrame EndFrame=nullptr;
  PFN_xrPollEvent PollEvent=nullptr;
  PFN_xrStringToPath StringToPath=nullptr;
  PFN_xrGetCurrentInteractionProfile GetCurrentInteractionProfile=nullptr;
  PFN_xrCreateActionSet CreateActionSet=nullptr;
  PFN_xrDestroyActionSet DestroyActionSet=nullptr;
  PFN_xrCreateAction CreateAction=nullptr;
  PFN_xrSuggestInteractionProfileBindings SuggestInteractionProfileBindings=nullptr;
  PFN_xrAttachSessionActionSets AttachSessionActionSets=nullptr;
  PFN_xrSyncActions SyncActions=nullptr;
  PFN_xrEnumerateBoundSourcesForAction EnumerateBoundSourcesForAction=nullptr;
  PFN_xrApplyHapticFeedback ApplyHapticFeedback=nullptr;
  PFN_xrStopHapticFeedback StopHapticFeedback=nullptr;
};
struct Instance {
  XrInstance handle=XR_NULL_HANDLE;
  Dispatch next{};
  std::recursive_mutex mutex;
  bool target=false,enabled=false;
  std::atomic<bool> closing{false};
  XrActionSet action_set=XR_NULL_HANDLE;
  std::array<XrAction,4> actions{};
  std::array<XrPath,2> hands{},haptics{};
  std::array<XrPath,2> pro_profiles{};
  std::array<std::array<XrPath,2>,2> trigger_paths{};
  std::atomic<unsigned> localized_suggested{0};
  bool core_pro=false,fb_pro=false;
};
struct Session {
  XrSession handle=XR_NULL_HANDLE;
  std::shared_ptr<Instance> instance;
  std::recursive_mutex mutex;
  bool attached=false,synchronized=false,bound=false,focused=false;
  bool closing=false,failed=false,claimed=false,reported=false;
  std::array<bool,4> emitting{},outputs{};
  std::array<bool,2> trigger_fault{};
  std::array<XrPath,2> profiles{};
  std::array<std::uint64_t,4> last_submission{};
};
std::mutex registry_mutex;
std::unordered_map<XrInstance,std::shared_ptr<Instance>> instances;
std::unordered_map<XrSession,std::shared_ptr<Session>> sessions;
std::atomic<PFN_xrGetInstanceProcAddr> global_next{nullptr};
std::atomic<edtrace::VrFrameRead> bridge_reader{nullptr};
std::atomic<VrDword> status_target{0},status_instance{0},status_session{0},status_focused{0};
std::atomic<VrDword> status_attached{0},status_sync{0},status_bound{0},status_failed{0};
std::atomic<std::uint64_t> status_frames{0},status_submissions{0},status_stops{0},status_standby{0};
#ifdef ED_OPENXR_FIXTURE
std::atomic<bool> fixture_target{false};
std::atomic<std::uint64_t> fixture_now{1000};
#endif

std::uint64_t Now() noexcept {
#ifdef ED_OPENXR_FIXTURE
  return fixture_now.load();
#elif defined(_WIN32)
  return GetTickCount64();
#else
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}

#ifdef _WIN32
bool SameFile(const std::filesystem::path& first,const std::filesystem::path& second) noexcept {
  const auto open=[](const std::filesystem::path& path) {
    return CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                       nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  };
  const HANDLE a=open(first);
  if(a==INVALID_HANDLE_VALUE) return false;
  const HANDLE b=open(second);
  if(b==INVALID_HANDLE_VALUE) { CloseHandle(a); return false; }
  BY_HANDLE_FILE_INFORMATION left{},right{};
  const bool read=GetFileInformationByHandle(a,&left)&&GetFileInformationByHandle(b,&right);
  CloseHandle(a); CloseHandle(b);
  return read&&(left.nFileIndexHigh||left.nFileIndexLow)&&left.dwVolumeSerialNumber==right.dwVolumeSerialNumber&&
         left.nFileIndexHigh==right.nFileIndexHigh&&left.nFileIndexLow==right.nFileIndexLow;
}

#endif
bool TargetProcess() noexcept {
#ifdef ED_OPENXR_FIXTURE
  if(fixture_target.load()) return true;
#endif
  try {
#ifdef _WIN32
    std::array<wchar_t,32768> executable{},library{};
    const auto count=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
    if(!count||count>=executable.size()) return false;
    const std::filesystem::path exe(std::wstring(executable.data(),count));
    if(_wcsicmp(exe.filename().c_str(),L"EliteDangerous64.exe")!=0) return false;
    HMODULE self=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&TargetProcess),&self)) return false;
    const auto length=GetModuleFileNameW(self,library.data(),static_cast<DWORD>(library.size()));
    if(!length||length>=library.size()) return false;
    const auto expected=(exe.parent_path()/L"ed-vr-haptics"/L"ed-vr-openxr.dll").lexically_normal();
    const auto actual=std::filesystem::path(std::wstring(library.data(),length)).lexically_normal();
    // Wine/Proton may launch the EXE through S: while the loader uses Z: for
    // the same installed DLL. Compare file identity rather than rejecting a
    // legitimate path alias (or accepting any same-basename external layer).
    return _wcsicmp(expected.c_str(),actual.c_str())==0||SameFile(expected,actual);
#else
    Dl_info library{};
    if(!dladdr(reinterpret_cast<const void*>(&TargetProcess),&library)||!library.dli_fname) return false;
    const auto installed=std::filesystem::canonical(library.dli_fname);
    if(installed.filename()!="libed-vr-openxr.so"||installed.parent_path().filename()!="ed-vr-haptics") return false;
    const auto process=std::filesystem::canonical("/proc/self/exe").filename().string();
    if(process!="wine64-preloader"&&process!="wine-preloader"&&process!="wine64"&&process!="wine") return false;
    const auto executable=installed.parent_path().parent_path()/"EliteDangerous64.exe";
    struct stat identity{};
    if(stat(executable.c_str(),&identity)!=0||!S_ISREG(identity.st_mode)||!identity.st_ino) return false;
    std::ifstream mappings("/proc/self/maps");
    std::string line;
    while(std::getline(mappings,line))
      if(edtrace::NativeXrMappedFile(line,major(identity.st_dev),minor(identity.st_dev),identity.st_ino)) return true;
    return false;
#endif
  } catch(...) { return false; }
}
edtrace::VrFrameRead Reader() noexcept {
  if(auto reader=bridge_reader.load(std::memory_order_acquire)) return reader;
#ifdef _WIN32
  const auto host=GetModuleHandleW(L"dinput8.dll");
  if(!host) return nullptr;
  const auto raw=GetProcAddress(host,"EDVRReadFrame");
  if(!raw) return nullptr;
  HMODULE pinned=nullptr;
  if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                        reinterpret_cast<LPCWSTR>(raw),&pinned)||pinned!=host) return nullptr;
  edtrace::VrFrameRead reader=nullptr;
  static_assert(sizeof(reader)==sizeof(raw)); std::memcpy(&reader,&raw,sizeof(reader));
  // Pin the existing host before caching its function; no controller interface
  // is loaded/acquired. Absence is retried after proxy hash validation.
  if(reader) bridge_reader.store(reader,std::memory_order_release);
  return reader;
#else
  return &edtrace::NativeXrRelayRead;
#endif
}
std::shared_ptr<Instance> Find(XrInstance handle) {
  std::lock_guard<std::mutex> lock(registry_mutex);
  const auto it=instances.find(handle); return it==instances.end()?nullptr:it->second;
}
std::shared_ptr<Session> Find(XrSession handle) {
  std::lock_guard<std::mutex> lock(registry_mutex);
  const auto it=sessions.find(handle); return it==sessions.end()?nullptr:it->second;
}
template<class T> T Resolve(PFN_xrGetInstanceProcAddr gipa,XrInstance handle,const char* name) noexcept {
  PFN_xrVoidFunction raw=nullptr;
  if(!gipa||XR_FAILED(gipa(handle,name,&raw))) return nullptr;
  T result=nullptr; static_assert(sizeof(result)==sizeof(raw)); std::memcpy(&result,&raw,sizeof(result)); return result;
}
template<class T> T Fallback(XrInstance handle,const char* name) noexcept {
  if(!handle) {
    std::shared_ptr<Instance> state;
    {
      std::lock_guard<std::mutex> lock(registry_mutex);
      if(!instances.empty()) state=instances.begin()->second;
    }
    if(state) return Resolve<T>(state->next.GetInstanceProcAddr,state->handle,name);
  }
  return Resolve<T>(global_next.load(std::memory_order_acquire),handle,name);
}
void Release(Session& session) noexcept {
  if(session.claimed||session.reported) {
    if(const auto reader=Reader()) {
      edtrace::VrBridgeFrame frame{};
      reader(static_cast<VrDword>(kBackend),0,&frame);
    }
    session.claimed=false;
    session.reported=false;
  }
}
void StopOutput(Session& session,unsigned output) noexcept {
  auto& instance=*session.instance;
  if(session.emitting[output]) {
    const auto now=Now();
    if(now>=session.last_submission[output]&&now-session.last_submission[output]<60) {
      XrHapticActionInfo info{}; info.type=XR_TYPE_HAPTIC_ACTION_INFO;
      info.action=instance.actions[output]; info.subactionPath=instance.hands[output%2];
      instance.next.StopHapticFeedback(session.handle,&info); status_stops.fetch_add(1);
    }
    session.emitting[output]=false;
  }
}
void StopOwn(Session& session) noexcept {
  // Only stop our own previously emitted vibration. Standby adapters must
  // never repeatedly zero another runtime route's physical output.
  for(unsigned output=0;output<4;++output) StopOutput(session,output);
}
bool ExtensionEnabled(const XrInstanceCreateInfo* info,const char* name) noexcept {
  if(!info||info->enabledExtensionCount>kMaximumCallerArray||
     (info->enabledExtensionCount&&!info->enabledExtensionNames)) return false;
  for(std::uint32_t i=0;i<info->enabledExtensionCount;++i)
    if(info->enabledExtensionNames[i]&&std::strcmp(info->enabledExtensionNames[i],name)==0) return true;
  return false;
}
bool Initialize(Instance& state) noexcept {
  auto& d=state.next;
  if(!d.StringToPath||!d.CreateActionSet||!d.DestroyActionSet||!d.CreateAction||
     !d.SuggestInteractionProfileBindings||!d.AttachSessionActionSets||!d.SyncActions||
     !d.EnumerateBoundSourcesForAction||!d.ApplyHapticFeedback||!d.StopHapticFeedback) return false;
  const char* hands[]{"/user/hand/left","/user/hand/right"};
  const char* haptics[]{"/user/hand/left/output/haptic","/user/hand/right/output/haptic"};
  for(unsigned i=0;i<2;++i)
    if(XR_FAILED(d.StringToPath(state.handle,hands[i],&state.hands[i]))||
       XR_FAILED(d.StringToPath(state.handle,haptics[i],&state.haptics[i]))) return false;
  XrActionSetCreateInfo set{}; set.type=XR_TYPE_ACTION_SET_CREATE_INFO;
  std::strcpy(set.actionSetName,kActionSetName); std::strcpy(set.localizedActionSetName,kActionSetLocalized);
  if(XR_FAILED(d.CreateActionSet(state.handle,&set,&state.action_set))) return false;
  const auto create=[&](unsigned output) {
    XrActionCreateInfo action{}; action.type=XR_TYPE_ACTION_CREATE_INFO;
    std::strcpy(action.actionName,kActionNames[output]); std::strcpy(action.localizedActionName,kActionLabels[output]);
    action.actionType=XR_ACTION_TYPE_VIBRATION_OUTPUT;
    action.countSubactionPaths=1; action.subactionPaths=&state.hands[output%2];
    XrAction handle=XR_NULL_HANDLE;
    if(XR_SUCCEEDED(d.CreateAction(state.action_set,&action,&handle))) state.actions[output]=handle;
    return state.actions[output]!=XR_NULL_HANDLE;
  };
  if(!create(0)||!create(1)) {
    d.DestroyActionSet(state.action_set); state.action_set=XR_NULL_HANDLE; return false;
  }
  // A valid path is not a capability probe. These paths are used only for a
  // negotiated profile that the application itself subsequently suggests.
  if(d.GetCurrentInteractionProfile&&(state.core_pro||state.fb_pro)) {
    const char* profiles[]{"/interaction_profiles/meta/touch_pro_controller","/interaction_profiles/facebook/touch_controller_pro"};
    const char* triggers[2][2]{{"/user/hand/left/output/haptic_trigger","/user/hand/right/output/haptic_trigger"},
      {"/user/hand/left/output/haptic_trigger_fb","/user/hand/right/output/haptic_trigger_fb"}};
    for(unsigned kind=0;kind<2;++kind) if(kind==0?state.core_pro:state.fb_pro) {
      XrPath profile=XR_NULL_PATH;
      if(XR_FAILED(d.StringToPath(state.handle,profiles[kind],&profile))) continue;
      for(unsigned hand=0;hand<2;++hand) {
        XrPath path=XR_NULL_PATH;
        if(XR_SUCCEEDED(d.StringToPath(state.handle,triggers[kind][hand],&path))) state.trigger_paths[kind][hand]=path;
      }
      state.pro_profiles[kind]=profile;
    }
    if(state.pro_profiles[0]||state.pro_profiles[1]) {
      create(2); create(3); // Optional failures retain the main hand outputs.
    }
  }
  // Do not suggest output-only defaults here: doing so could replace bindings
  // established by a lower layer. Append only to the application's own calls.
  return true;
}
void UpdateStatus(const Session& s) noexcept {
  status_session.store(!s.closing&&!s.failed&&s.attached&&s.synchronized&&s.bound&&s.focused); status_focused.store(s.focused);
  status_attached.store(s.attached); status_sync.store(s.synchronized); status_bound.store(s.bound);
  status_failed.store(s.failed);
}
int ProProfile(const Instance& state,XrPath profile) noexcept {
  if(profile) for(unsigned kind=0;kind<2;++kind)
    if(state.pro_profiles[kind]==profile) return static_cast<int>(kind);
  return -1;
}
bool OutputBound(Session& state,unsigned output) noexcept {
  if(!state.instance->actions[output]) return false;
  XrBoundSourcesForActionEnumerateInfo query{}; query.type=XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO;
  query.action=state.instance->actions[output]; std::uint32_t count=0;
  const auto bound=state.instance->next.EnumerateBoundSourcesForAction(state.handle,&query,0,&count,nullptr);
  return bound==XR_SUCCESS&&count>0;
}
void Submit(Session& s) noexcept {
  ++status_frames;
  auto& instance=*s.instance;
  const bool ready=instance.enabled&&!instance.closing&&!s.closing&&!s.failed&&s.attached&&s.synchronized&&s.bound&&s.focused;
  const auto reader=Reader();
  if(!reader) { UpdateStatus(s); return; }
  edtrace::VrBridgeFrame frame{};
  const bool owns=reader(static_cast<VrDword>(kBackend),ready?1:0,&frame)!=0;
  s.claimed=owns; s.reported=ready;
  if(!ready||!owns) {
    // Arbitration handles the finite-pulse handoff. Do not send standby zeros.
    s.emitting={}; status_standby.fetch_add(1); UpdateStatus(s); return;
  }
  if(frame.size!=sizeof(frame)||frame.version!=1) { Release(s); s.failed=true; UpdateStatus(s); return; }
  auto available=s.outputs;
  for(unsigned hand=0;hand<2;++hand) available[hand+2]=available[hand+2]&&!s.trigger_fault[hand];
  // Submit localized outputs first so a rejected trigger can fall back into
  // its main hand during this same frame, without changing other endpoints.
  constexpr unsigned order[]{2,3,0,1};
  for(const auto output:order) {
    if(!available[output]) { StopOutput(s,output); continue; }
    const auto amplitudes=edtrace::OpenXRMotorAmplitudes(frame.frame,available,.6f);
    XrHapticActionInfo info{}; info.type=XR_TYPE_HAPTIC_ACTION_INFO;
    info.action=instance.actions[output]; info.subactionPath=instance.hands[output%2];
    if(amplitudes[output]==0) { StopOutput(s,output); continue; }
    XrHapticVibration vibration{}; vibration.type=XR_TYPE_HAPTIC_VIBRATION;
    vibration.duration=60000000; vibration.frequency=XR_FREQUENCY_UNSPECIFIED;
    vibration.amplitude=amplitudes[output];
    const auto result=instance.next.ApplyHapticFeedback(s.handle,&info,reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
    if(result!=XR_SUCCESS) {
      if(output>=2&&result!=XR_SESSION_NOT_FOCUSED&&result!=XR_ERROR_SESSION_LOST&&
         result!=XR_ERROR_INSTANCE_LOST&&result!=XR_ERROR_HANDLE_INVALID&&result!=XR_SESSION_LOSS_PENDING) {
        StopOutput(s,output); s.trigger_fault[output-2]=true; available[output]=false; continue;
      }
      if(result==XR_SESSION_NOT_FOCUSED) { s.focused=false; s.synchronized=false; }
      else s.failed=true;
      StopOwn(s); Release(s); break;
    }
    s.emitting[output]=true; s.last_submission[output]=Now(); status_submissions.fetch_add(1);
  }
  if(!s.failed&&s.focused) {
    s.bound=available[0]||available[1]||available[2]||available[3];
    if(!s.bound) Release(s);
  }
  UpdateStatus(s);
}

XrResult XRAPI_CALL LayerDestroyInstance(XrInstance handle) noexcept {
  LastError error;
  auto state=Find(handle);
  const auto original=state?state->next.DestroyInstance:Fallback<PFN_xrDestroyInstance>(handle,"xrDestroyInstance");
  if(!original) return XR_ERROR_FUNCTION_UNSUPPORTED;
  error.Before(); const auto result=original(handle); error.After();
  if(XR_SUCCEEDED(result)&&state) {
    std::lock_guard<std::recursive_mutex> state_lock(state->mutex); state->closing=true;
    std::vector<std::shared_ptr<Session>> retired;
    try {
      std::lock_guard<std::mutex> lock(registry_mutex);
      for(auto it=sessions.begin();it!=sessions.end();) {
        if(it->second->instance==state) { retired.push_back(it->second); it=sessions.erase(it); } else ++it;
      }
      instances.erase(handle);
    } catch(...) { /* Runtime already destroyed instance; keep metadata disabled. */ }
    for(auto& session:retired) { std::lock_guard<std::recursive_mutex> lock(session->mutex); session->closing=true; Release(*session); }
    status_instance.store(0); status_session.store(0); status_focused.store(0);
#ifndef _WIN32
    bool remaining=false;
    {
      std::lock_guard<std::mutex> registry(registry_mutex);
      for(const auto& item:instances) remaining=remaining||item.second->enabled;
    }
    if(!remaining) edtrace::NativeXrRelayStop();
#endif
  }
  return result;
}
XrResult XRAPI_CALL LayerCreateSession(XrInstance handle,const XrSessionCreateInfo* info,XrSession* session) noexcept {
  LastError error; auto state=Find(handle);
  const auto original=state?state->next.CreateSession:Fallback<PFN_xrCreateSession>(handle,"xrCreateSession");
  if(!original) return XR_ERROR_FUNCTION_UNSUPPORTED;
  error.Before(); const auto result=original(handle,info,session); error.After();
  if(XR_SUCCEEDED(result)&&session&&*session&&state&&state->enabled) {
    try { auto entry=std::make_shared<Session>(); entry->handle=*session; entry->instance=state;
      std::lock_guard<std::mutex> lock(registry_mutex); sessions[*session]=std::move(entry);
    } catch(...) { /* Haptic metadata is optional. */ }
  }
  return result;
}
XrResult XRAPI_CALL LayerDestroySession(XrSession handle) noexcept {
  LastError error; auto state=Find(handle);
  if(!state) { const auto original=Fallback<PFN_xrDestroySession>(XR_NULL_HANDLE,"xrDestroySession");
    if(!original) return XR_ERROR_FUNCTION_UNSUPPORTED;
    error.Before(); const auto result=original(handle); error.After(); return result; }
  std::lock_guard<std::recursive_mutex> lock(state->mutex);
  StopOwn(*state); Release(*state); state->closing=true;
  error.Before(); const auto result=state->instance->next.DestroySession(handle); error.After();
  if(XR_SUCCEEDED(result)) { std::lock_guard<std::mutex> registry(registry_mutex); sessions.erase(handle); }
  else state->closing=false;
  UpdateStatus(*state); return result;
}
XrResult XRAPI_CALL LayerEndSession(XrSession handle) noexcept {
  LastError error; auto state=Find(handle);
  if(!state) { const auto original=Fallback<PFN_xrEndSession>(XR_NULL_HANDLE,"xrEndSession");
    if(!original) return XR_ERROR_FUNCTION_UNSUPPORTED;
    error.Before(); const auto result=original(handle); error.After(); return result; }
  std::lock_guard<std::recursive_mutex> lock(state->mutex); StopOwn(*state); Release(*state);
  error.Before(); const auto result=state->instance->next.EndSession(handle); error.After();
  if(XR_SUCCEEDED(result)) { state->focused=false; state->synchronized=false; state->bound=false; }
  UpdateStatus(*state); return result;
}
XrResult XRAPI_CALL LayerEndFrame(XrSession handle,const XrFrameEndInfo* info) noexcept {
  LastError error; auto state=Find(handle);
  if(!state) { const auto original=Fallback<PFN_xrEndFrame>(XR_NULL_HANDLE,"xrEndFrame");
    if(!original) return XR_ERROR_FUNCTION_UNSUPPORTED;
    error.Before(); const auto result=original(handle,info); error.After(); return result; }
  std::lock_guard<std::recursive_mutex> lock(state->mutex);
  error.Before(); const auto result=state->instance->next.EndFrame(handle,info); error.After();
  if(result==XR_SUCCESS) Submit(*state);
  else { Release(*state); state->synchronized=false; }
  return result;
}
XrResult XRAPI_CALL LayerPollEvent(XrInstance handle,XrEventDataBuffer* data) noexcept {
  LastError error; auto instance=Find(handle);
  const auto original=instance?instance->next.PollEvent:Fallback<PFN_xrPollEvent>(handle,"xrPollEvent");
  if(!original) return XR_ERROR_FUNCTION_UNSUPPORTED;
  error.Before(); const auto result=original(handle,data); error.After();
  if(result==XR_SUCCESS&&data&&data->type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
    const auto* change=reinterpret_cast<const XrEventDataSessionStateChanged*>(data);
    if(auto state=Find(change->session)) {
      std::lock_guard<std::recursive_mutex> lock(state->mutex);
      const bool focused=change->state==XR_SESSION_STATE_FOCUSED;
      if(state->focused!=focused) {
        if(state->focused) StopOwn(*state);
        Release(*state); state->synchronized=false; state->bound=false;
      }
      state->focused=focused; UpdateStatus(*state);
    }
  } else if(result==XR_SUCCESS&&data&&data->type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING&&instance) {
    std::lock_guard<std::recursive_mutex> lock(instance->mutex); instance->closing=true;
    // No additional event polling: the original event is returned unchanged.
  }
  return result;
}
XrResult XRAPI_CALL LayerSuggest(XrInstance handle,const XrInteractionProfileSuggestedBinding* info) noexcept {
  LastError error; auto state=Find(handle);
  const auto original=state?state->next.SuggestInteractionProfileBindings:Fallback<PFN_xrSuggestInteractionProfileBindings>(handle,"xrSuggestInteractionProfileBindings");
  if(!original) return XR_ERROR_FUNCTION_UNSUPPORTED;
  std::unique_lock<std::recursive_mutex> transaction;
  if(state) transaction=std::unique_lock<std::recursive_mutex>(state->mutex);
  error.Before(); const auto result=original(handle,info); error.After();
  if(result!=XR_SUCCESS||!state||!state->enabled||!info||info->countSuggestedBindings>kMaximumCallerArray||
     (info->countSuggestedBindings&&!info->suggestedBindings)) return result;
  try {
    std::vector<XrActionSuggestedBinding> bindings;
    if(info->countSuggestedBindings) bindings.assign(info->suggestedBindings,info->suggestedBindings+info->countSuggestedBindings);
    for(unsigned hand=0;hand<2;++hand) bindings.push_back({state->actions[hand],state->haptics[hand]});
    auto combined=*info; combined.countSuggestedBindings=static_cast<std::uint32_t>(bindings.size()); combined.suggestedBindings=bindings.data();
    const auto kind=ProProfile(*state,info->interactionProfile);
    if(kind>=0) {
      state->localized_suggested.fetch_and(~(1u<<kind),std::memory_order_release);
      auto localized=bindings;
      for(unsigned hand=0;hand<2;++hand)
        if(state->actions[hand+2]&&state->trigger_paths[kind][hand])
          localized.push_back({state->actions[hand+2],state->trigger_paths[kind][hand]});
      if(localized.size()>bindings.size()) {
        auto extended=combined; extended.countSuggestedBindings=static_cast<std::uint32_t>(localized.size()); extended.suggestedBindings=localized.data();
        if(original(handle,&extended)==XR_SUCCESS) { state->localized_suggested.fetch_or(1u<<kind,std::memory_order_release); return result; }
      }
    }
    // The original successful call first preserves all caller bindings. If
    // optional haptic augmentation fails, its result never replaces that call.
    if(XR_FAILED(original(handle,&combined))) original(handle,info);
  } catch(...) { /* Keep the application's original suggestions. */ }
  return result;
}
XrResult XRAPI_CALL LayerAttach(XrSession handle,const XrSessionActionSetsAttachInfo* info) noexcept {
  LastError error; auto state=Find(handle);
  const auto original=state?state->instance->next.AttachSessionActionSets:Fallback<PFN_xrAttachSessionActionSets>(XR_NULL_HANDLE,"xrAttachSessionActionSets");
  if(!original) return XR_ERROR_FUNCTION_UNSUPPORTED;
  if(!state||!info||info->countActionSets>kMaximumCallerArray||
     (info->countActionSets&&!info->actionSets)) {
    error.Before(); const auto result=original(handle,info); error.After(); return result;
  }
  std::lock_guard<std::recursive_mutex> lock(state->mutex);
  if(state->failed||state->closing) { error.Before(); const auto result=original(handle,info); error.After(); return result; }
  try {
    std::vector<XrActionSet> sets;
    if(info->countActionSets) sets.assign(info->actionSets,info->actionSets+info->countActionSets);
    sets.push_back(state->instance->action_set);
    auto combined=*info; combined.countActionSets=static_cast<std::uint32_t>(sets.size()); combined.actionSets=sets.data();
    error.Before(); auto result=original(handle,&combined); error.After();
    if(XR_FAILED(result)) {
      // Preserve the application's sole attach opportunity if the optional
      // output set cannot be accepted. Never attempt a second attach on success.
      error.Before(); result=original(handle,info); error.After(); state->failed=true;
    } else state->attached=true;
    UpdateStatus(*state); return result;
  } catch(...) { error.Before(); const auto result=original(handle,info); error.After(); state->failed=true; return result; }
}
XrResult XRAPI_CALL LayerSync(XrSession handle,const XrActionsSyncInfo* info) noexcept {
  LastError error; auto state=Find(handle);
  const auto original=state?state->instance->next.SyncActions:Fallback<PFN_xrSyncActions>(XR_NULL_HANDLE,"xrSyncActions");
  if(!original) return XR_ERROR_FUNCTION_UNSUPPORTED;
  if(!state||!info||info->countActiveActionSets>kMaximumCallerArray||
     (info->countActiveActionSets&&!info->activeActionSets)) {
    error.Before(); const auto result=original(handle,info); error.After(); return result;
  }
  std::lock_guard<std::recursive_mutex> lock(state->mutex);
  if(!state->attached||state->failed||state->closing) { error.Before(); const auto result=original(handle,info); error.After(); return result; }
  try {
    std::vector<XrActiveActionSet> sets;
    if(info->countActiveActionSets) sets.assign(info->activeActionSets,info->activeActionSets+info->countActiveActionSets);
    sets.push_back({state->instance->action_set,XR_NULL_PATH});
    auto combined=*info; combined.countActiveActionSets=static_cast<std::uint32_t>(sets.size()); combined.activeActionSets=sets.data();
    error.Before(); auto result=original(handle,&combined); error.After();
    if(XR_FAILED(result)) { error.Before(); result=original(handle,info); error.After(); state->synchronized=false; Release(*state); }
    else state->synchronized=result==XR_SUCCESS;
    std::array<bool,4> available{};
    if(state->synchronized) {
      for(unsigned hand=0;hand<2;++hand) {
        available[hand]=OutputBound(*state,hand);
        XrInteractionProfileState profile{}; profile.type=XR_TYPE_INTERACTION_PROFILE_STATE;
        const auto& instance=*state->instance;
        if(instance.next.GetCurrentInteractionProfile&&(instance.pro_profiles[0]||instance.pro_profiles[1]))
          if(instance.next.GetCurrentInteractionProfile(handle,instance.hands[hand],&profile)!=XR_SUCCESS) profile.interactionProfile=XR_NULL_PATH;
        const auto kind=ProProfile(instance,profile.interactionProfile);
        available[hand+2]=kind>=0&&(instance.localized_suggested.load(std::memory_order_acquire)&(1u<<kind))&&OutputBound(*state,hand+2);
        if(state->profiles[hand]!=profile.interactionProfile||state->outputs[hand+2]!=available[hand+2]) state->trigger_fault[hand]=false;
        state->profiles[hand]=profile.interactionProfile;
      }
    }
    for(unsigned output=0;output<4;++output) if(!available[output]) StopOutput(*state,output);
    state->outputs=available;
    state->bound=available[0]||available[1]||(available[2]&&!state->trigger_fault[0])||(available[3]&&!state->trigger_fault[1]);
    if(!state->synchronized||!state->bound) Release(*state);
    UpdateStatus(*state); return result;
  } catch(...) { error.Before(); const auto result=original(handle,info); error.After(); state->synchronized=false; Release(*state); return result; }
}

XrResult XRAPI_CALL LayerGetInstanceProcAddr(XrInstance handle,const char* name,PFN_xrVoidFunction* function) noexcept {
  LastError error;
  if(!name||!function) return XR_ERROR_VALIDATION_FAILURE;
  auto state=Find(handle);
  const auto next=state?state->next.GetInstanceProcAddr:global_next.load(std::memory_order_acquire);
  if(!next) { *function=nullptr; return XR_ERROR_FUNCTION_UNSUPPORTED; }
  error.Before(); const auto result=next(handle,name,function); error.After();
  if(XR_FAILED(result)||!state) return result;
#define ED_INTERCEPT(api,implementation) if(std::strcmp(name,#api)==0) { auto target=&implementation; std::memcpy(function,&target,sizeof(target)); return result; }
  ED_INTERCEPT(xrGetInstanceProcAddr,LayerGetInstanceProcAddr)
  ED_INTERCEPT(xrDestroyInstance,LayerDestroyInstance)
  if(state->enabled) {
    ED_INTERCEPT(xrCreateSession,LayerCreateSession)
    ED_INTERCEPT(xrDestroySession,LayerDestroySession)
    ED_INTERCEPT(xrEndSession,LayerEndSession)
    ED_INTERCEPT(xrEndFrame,LayerEndFrame)
    ED_INTERCEPT(xrPollEvent,LayerPollEvent)
    ED_INTERCEPT(xrSuggestInteractionProfileBindings,LayerSuggest)
    ED_INTERCEPT(xrAttachSessionActionSets,LayerAttach)
    ED_INTERCEPT(xrSyncActions,LayerSync)
  }
#undef ED_INTERCEPT
  return result;
}
XrResult XRAPI_CALL LayerCreateApiInstance(const XrInstanceCreateInfo* info,const XrApiLayerCreateInfo* layer,XrInstance* handle) noexcept {
  LastError error;
  if(!layer||layer->structType!=XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO||
     layer->structVersion!=XR_API_LAYER_CREATE_INFO_STRUCT_VERSION||layer->structSize<sizeof(*layer)||
     !layer->nextInfo||layer->nextInfo->structType!=XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO||
     layer->nextInfo->structVersion!=XR_API_LAYER_NEXT_INFO_STRUCT_VERSION||layer->nextInfo->structSize<sizeof(XrApiLayerNextInfo)||
     std::strcmp(layer->nextInfo->layerName,kLayerName)!=0||
     !layer->nextInfo->nextGetInstanceProcAddr||!layer->nextInfo->nextCreateApiLayerInstance) return XR_ERROR_INITIALIZATION_FAILED;
  const auto next_gipa=layer->nextInfo->nextGetInstanceProcAddr;
  const auto next_create=layer->nextInfo->nextCreateApiLayerInstance;
  auto next_layer=*layer; next_layer.nextInfo=layer->nextInfo->next;
  global_next.store(next_gipa,std::memory_order_release);
  const bool target=TargetProcess();
  // Use only the application's existing API/extension negotiation. An output
  // layer must not silently enable a new controller profile extension.
  error.Before(); const auto result=next_create(info,&next_layer,handle); error.After();
  if(XR_FAILED(result)||!handle||!*handle) return result;
  try {
    auto state=std::make_shared<Instance>(); state->handle=*handle; state->next.GetInstanceProcAddr=next_gipa;
#define ED_RESOLVE(name) state->next.name=Resolve<PFN_xr##name>(next_gipa,*handle,"xr" #name)
    ED_RESOLVE(DestroyInstance); ED_RESOLVE(CreateSession); ED_RESOLVE(DestroySession); ED_RESOLVE(EndSession);
    ED_RESOLVE(EndFrame); ED_RESOLVE(PollEvent); ED_RESOLVE(StringToPath); ED_RESOLVE(GetCurrentInteractionProfile); ED_RESOLVE(CreateActionSet);
    ED_RESOLVE(DestroyActionSet); ED_RESOLVE(CreateAction); ED_RESOLVE(SuggestInteractionProfileBindings);
    ED_RESOLVE(AttachSessionActionSets); ED_RESOLVE(SyncActions); ED_RESOLVE(EnumerateBoundSourcesForAction);
    ED_RESOLVE(ApplyHapticFeedback); ED_RESOLVE(StopHapticFeedback);
#undef ED_RESOLVE
    state->target=target;
    state->core_pro=info&&info->applicationInfo.apiVersion>=XR_MAKE_VERSION(1,1,0);
    state->fb_pro=ExtensionEnabled(info,kTouchProExtension);
    state->enabled=state->target&&Initialize(*state);
#ifndef _WIN32
    if(state->enabled&&!bridge_reader.load(std::memory_order_acquire)) edtrace::NativeXrRelayStart();
#endif
    status_target.store(state->target); status_instance.store(state->enabled);
    std::lock_guard<std::mutex> lock(registry_mutex); instances[*handle]=std::move(state);
  } catch(...) { /* Failed optional state must not fail instance creation. */ }
  return result;
}
}

extern "C" ED_XR_EXPORT XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(const XrNegotiateLoaderInfo* loader,const char* layer_name,XrNegotiateApiLayerRequest* request) noexcept {
  LastError error;
  if(!loader||!request||!layer_name||std::strcmp(layer_name,kLayerName)!=0||
     loader->structType!=XR_LOADER_INTERFACE_STRUCT_LOADER_INFO||loader->structVersion!=XR_LOADER_INFO_STRUCT_VERSION||loader->structSize<sizeof(*loader)||
     request->structType!=XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST||request->structVersion!=XR_API_LAYER_INFO_STRUCT_VERSION||request->structSize<sizeof(*request)||
     loader->minInterfaceVersion>XR_CURRENT_LOADER_API_LAYER_VERSION||loader->maxInterfaceVersion<XR_CURRENT_LOADER_API_LAYER_VERSION||
     loader->maxApiVersion<XR_MAKE_VERSION(1,0,0)||loader->minApiVersion>XR_MAKE_VERSION(1,1,0)) return XR_ERROR_INITIALIZATION_FAILED;
  request->layerInterfaceVersion=XR_CURRENT_LOADER_API_LAYER_VERSION;
  request->layerApiVersion=loader->maxApiVersion<XR_MAKE_VERSION(1,1,0)?loader->maxApiVersion:XR_MAKE_VERSION(1,1,0);
  request->getInstanceProcAddr=&LayerGetInstanceProcAddr;
  request->createApiLayerInstance=&LayerCreateApiInstance;
  return XR_SUCCESS;
}
extern "C" ED_XR_EXPORT VrBool ED_VR_CALL EDOpenXRGetStatus(edtrace::OpenXRHapticsStatus* status) noexcept {
  LastError error;
  if(!status||status->size!=sizeof(*status)||status->version!=1) return 0;
  *status={sizeof(*status),1,status_target.load(),status_instance.load(),status_session.load(),status_focused.load(),
    status_attached.load(),status_sync.load(),status_bound.load(),status_failed.load(),
    status_frames.load(),status_submissions.load(),status_stops.load(),status_standby.load()};
  return 1;
}
#ifdef ED_OPENXR_FIXTURE
extern "C" void EDOpenXRSetFixtureReader(edtrace::VrFrameRead reader,VrBool target) noexcept {
  bridge_reader.store(reader); fixture_target.store(target!=0);
}
extern "C" void EDOpenXRSetFixtureTime(std::uint64_t now) noexcept { fixture_now.store(now); }
#endif
