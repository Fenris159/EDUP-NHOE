#define XR_NO_PROTOTYPES
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
#include "edtrace/vr_backend.hpp"
#include "edtrace/openxr_haptics_status.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#ifndef _WIN32
#include <cerrno>
using DWORD=edtrace::VrDword;
using BOOL=edtrace::VrBool;
#define WINAPI ED_VR_CALL
constexpr BOOL FALSE=0,TRUE=1;
DWORD GetLastError() noexcept { return static_cast<DWORD>(errno); }
void SetLastError(DWORD value) noexcept { errno=static_cast<int>(value); }
#endif

extern "C" XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(const XrNegotiateLoaderInfo*,const char*,XrNegotiateApiLayerRequest*) noexcept;
extern "C" void EDOpenXRSetFixtureReader(edtrace::VrFrameRead,BOOL) noexcept;
extern "C" void EDOpenXRSetFixtureTime(std::uint64_t) noexcept;
extern "C" BOOL WINAPI EDOpenXRGetStatus(edtrace::OpenXRHapticsStatus*) noexcept;
namespace {
std::string result_path;
void Result(const char* text) {
  if(!result_path.empty()) { std::ofstream file(result_path); file<<text<<'\n'; }
}
void Check(bool value,const char* message) {
  if(!value) { const std::string text=std::string("FAIL: ")+message; Result(text.c_str()); std::fprintf(stderr,"%s\n",text.c_str()); std::exit(1); }
}
template<class T> T Handle(std::uintptr_t value) { return reinterpret_cast<T>(value); }
const XrActionSet own_set=Handle<XrActionSet>(0x300);
const std::array<XrAction,4> own_actions{{Handle<XrAction>(0x301),Handle<XrAction>(0x302),Handle<XrAction>(0x303),Handle<XrAction>(0x304)}};
const XrAction own_action=own_actions[0];
const XrActionSet app_set_a=Handle<XrActionSet>(0x400),app_set_b=Handle<XrActionSet>(0x401);
const XrAction app_action_a=Handle<XrAction>(0x500),app_action_b=Handle<XrAction>(0x501);
std::uintptr_t instance_number=0x100,session_number=0x200;
struct RuntimeSession { bool attached=false; std::vector<XrActionSet> sets; };
std::map<XrSession,RuntimeSession> runtime_sessions;
std::vector<XrActionSuggestedBinding> current_bindings;
std::map<XrPath,std::vector<XrActionSuggestedBinding>> profile_bindings;
std::vector<XrActiveActionSet> current_active;
std::vector<std::string> requested_functions;
struct Output { XrSession session; XrAction action; XrPath hand; XrDuration duration; float frequency,amplitude; };
std::vector<Output> output;
unsigned creates=0,attaches=0,syncs=0,polls=0,stops=0,releases=0;
bool reject_attach=false,reject_sync=false,own_output=true,outputs_bound=true;
bool reject_augmented_suggestion=false;
bool zero_frame=false;
edtrace::HapticFrame source_frame{.25f,.5f,1,.75f};
std::array<XrPath,2> active_profiles{{999,999}};
std::array<bool,4> endpoint_bound{{true,true,true,true}};
std::array<unsigned,4> attempts{};
std::vector<unsigned> stopped_outputs;
std::array<bool,2> reject_trigger_apply{};
bool runtime_supports_fb=false,reject_optional_create=false,reject_localized_suggestion=false,reject_trigger_creation=false;
bool instance_fb_enabled=false;
XrVersion instance_api_version=XR_MAKE_VERSION(1,0,0);
unsigned instance_requests=0;
XrSession event_session=XR_NULL_HANDLE;
XrSessionState event_state=XR_SESSION_STATE_UNKNOWN;
const XrInstanceCreateInfo* expected_create=nullptr;
const XrFrameEndInfo* expected_frame=nullptr;
DWORD frame_incoming_error=0;

BOOL WINAPI Read(DWORD backend,BOOL ready,edtrace::VrBridgeFrame* frame) {
#ifdef _WIN32
  Check(backend==3,"OpenXR uses only its output route");
#else
  Check(backend==4,"OpenXR uses only its output route");
#endif
  *frame={}; frame->generation=7;
  SetLastError(0xdeadbeef);
  if(!ready) { ++releases; return FALSE; }
  if(!own_output) return FALSE;
  frame->frame=zero_frame?edtrace::HapticFrame{}:source_frame;
  return TRUE;
}
XrResult XRAPI_CALL NextCreate(const XrInstanceCreateInfo* info,const XrApiLayerCreateInfo* layer,XrInstance* result) {
  ++instance_requests;
  Check(info==expected_create,"exact application instance request and extension list preserved");
  if(reject_optional_create) { SetLastError(0xbad); return XR_ERROR_EXTENSION_NOT_PRESENT; }
  Check(layer->nextInfo==nullptr&&layer->loaderInstance==reinterpret_cast<void*>(0x999),"loader chain advanced without overwriting caller chain");
  instance_api_version=info->applicationInfo.apiVersion; instance_fb_enabled=false;
  for(unsigned i=0;i<info->enabledExtensionCount;++i) instance_fb_enabled|=std::strcmp(info->enabledExtensionNames[i],"XR_FB_touch_controller_pro")==0;
  *result=Handle<XrInstance>(++instance_number); SetLastError(0x11223344); return XR_SUCCESS;
}
XrResult XRAPI_CALL DestroyInstance(XrInstance) { SetLastError(0x22334455); return XR_SUCCESS; }
XrResult XRAPI_CALL CreateSession(XrInstance,const XrSessionCreateInfo* info,XrSession* result) {
  Check(info&&info->next==reinterpret_cast<void*>(0x1111),"session graphics chain preserved");
  *result=Handle<XrSession>(++session_number); runtime_sessions[*result]={}; SetLastError(0x33445566); return XR_SUCCESS;
}
XrResult XRAPI_CALL DestroySession(XrSession handle) { runtime_sessions.erase(handle); SetLastError(0x44556677); return XR_SUCCESS; }
XrResult XRAPI_CALL EndSession(XrSession) { SetLastError(0x55667788); return XR_SUCCESS; }
XrResult XRAPI_CALL EndFrame(XrSession handle,const XrFrameEndInfo* info) {
  frame_incoming_error=GetLastError();
  Check(info==expected_frame,"render submission pointer preserved");
  SetLastError(0x98765432); return runtime_sessions.count(handle)?XR_SUCCESS:XR_ERROR_HANDLE_INVALID;
}
XrResult XRAPI_CALL PollEvent(XrInstance,XrEventDataBuffer* data) {
  ++polls;
  if(event_state==XR_SESSION_STATE_UNKNOWN) return XR_EVENT_UNAVAILABLE;
  XrEventDataSessionStateChanged event{}; event.type=XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED;
  event.session=event_session; event.state=event_state; event.time=123456789;
  std::memcpy(data,&event,sizeof(event)); event_state=XR_SESSION_STATE_UNKNOWN;
  SetLastError(0x6789abcd); return XR_SUCCESS;
}
XrResult XRAPI_CALL StringToPath(XrInstance,const char* string,XrPath* result) {
  const std::map<std::string,XrPath> paths{{"/user/hand/left",11},{"/user/hand/right",12},
    {"/user/hand/left/output/haptic",13},{"/user/hand/right/output/haptic",14},
    {"/interaction_profiles/meta/touch_pro_controller",1000},{"/interaction_profiles/facebook/touch_controller_pro",1001},
    {"/user/hand/left/output/haptic_trigger",15},{"/user/hand/right/output/haptic_trigger",16},
    {"/user/hand/left/output/haptic_trigger_fb",17},{"/user/hand/right/output/haptic_trigger_fb",18}};
  const auto found=paths.find(string);
  Check(found!=paths.end(),"layer creates only own hand/output paths, no input/tracking queries");
  if(found->second==1000||found->second==15||found->second==16)
    Check(instance_api_version>=XR_MAKE_VERSION(1,1,0),"promoted Touch Pro paths require negotiated 1.1");
  if(found->second==1001||found->second==17||found->second==18)
    Check(instance_fb_enabled,"FB Touch Pro paths require enabled extension");
  *result=found->second; return XR_SUCCESS;
}
XrResult XRAPI_CALL CurrentProfile(XrSession,XrPath hand,XrInteractionProfileState* result) {
  Check((hand==11||hand==12)&&result->type==XR_TYPE_INTERACTION_PROFILE_STATE,"profile query reads hand metadata only");
  result->interactionProfile=active_profiles[hand==11?0:1]; return XR_SUCCESS;
}
XrResult XRAPI_CALL EnumerateExtensions(const char* name,std::uint32_t capacity,std::uint32_t* count,XrExtensionProperties* values) {
  Check(name==nullptr,"only runtime extension availability queried");
  *count=runtime_supports_fb?1:0;
  if(capacity&&runtime_supports_fb) {
    Check(values&&values[0].type==XR_TYPE_EXTENSION_PROPERTIES,"extension property structure initialized");
    std::strcpy(values[0].extensionName,"XR_FB_touch_controller_pro"); values[0].extensionVersion=1;
  }
  return XR_SUCCESS;
}
XrResult XRAPI_CALL CreateActionSet(XrInstance,const XrActionSetCreateInfo* info,XrActionSet* result) {
  Check(info->priority==0,"output action set cannot claim input priority");
#ifdef _WIN32
  Check(std::strcmp(info->actionSetName,"ed_vr_proxy_haptics_v1")==0,"Windows output set has distinct nested-loader name");
#else
  Check(std::strcmp(info->actionSetName,"ed_vr_native_haptics_v1")==0,"native output set has distinct nested-loader name");
#endif
  ++creates; *result=own_set; return XR_SUCCESS;
}
XrResult XRAPI_CALL DestroyActionSet(XrActionSet set) { Check(set==own_set,"only own action set destroyed"); return XR_SUCCESS; }
XrResult XRAPI_CALL CreateAction(XrActionSet set,const XrActionCreateInfo* info,XrAction* result) {
  Check(set==own_set&&info->actionType==XR_ACTION_TYPE_VIBRATION_OUTPUT,"only vibration output action created");
#ifdef _WIN32
  const char* names[]{"ed_vr_left_body","ed_vr_right_body","ed_vr_left_trigger","ed_vr_right_trigger"};
#else
  const char* names[]{"ed_vr_native_left_body","ed_vr_native_right_body","ed_vr_native_left_trigger","ed_vr_native_right_trigger"};
#endif
  unsigned output=4;
  for(unsigned i=0;i<4;++i) if(std::strcmp(info->actionName,names[i])==0) output=i;
  Check(output<4,"output action names are isolated across nested loaders");
  Check(info->countSubactionPaths==1&&info->subactionPaths[0]==(output%2?12:11),"each output action belongs to exactly one hand");
  if(output==2&&reject_trigger_creation) return XR_ERROR_RUNTIME_FAILURE;
  *result=own_actions[output]; return XR_SUCCESS;
}
XrResult XRAPI_CALL Suggest(XrInstance,const XrInteractionProfileSuggestedBinding* info) {
  Check(info->next==reinterpret_cast<void*>(0x2222),"binding suggestion extension chain preserved");
  bool augmented=false,localized=false;
  for(unsigned i=0;i<info->countSuggestedBindings;++i) {
    const auto action=info->suggestedBindings[i].action;
    augmented|=std::find(own_actions.begin(),own_actions.end(),action)!=own_actions.end();
    localized|=action==own_actions[2]||action==own_actions[3];
    if(action==own_actions[2]||action==own_actions[3]) {
      const unsigned hand=action==own_actions[2]?0:1;
      Check((info->interactionProfile==1000&&info->suggestedBindings[i].binding==15+hand)||
        (info->interactionProfile==1001&&info->suggestedBindings[i].binding==17+hand),
        "localized outputs use exact documented profile-specific trigger paths");
    }
  }
  if((reject_augmented_suggestion&&augmented)||(reject_localized_suggestion&&localized)) {
    // A deliberately imperfect lower adapter mutates before rejecting. The
    // haptic layer must restore the caller's successful controls afterward.
    current_bindings.assign(info->suggestedBindings,info->suggestedBindings+info->countSuggestedBindings);
    profile_bindings[info->interactionProfile]=current_bindings;
    SetLastError(0x5555); return XR_ERROR_PATH_UNSUPPORTED;
  }
  current_bindings.assign(info->suggestedBindings,info->suggestedBindings+info->countSuggestedBindings);
  profile_bindings[info->interactionProfile]=current_bindings;
  SetLastError(0x76543210); return XR_SUCCESS;
}
XrResult XRAPI_CALL Attach(XrSession handle,const XrSessionActionSetsAttachInfo* info) {
  ++attaches; auto& state=runtime_sessions.at(handle);
  if(state.attached) return XR_ERROR_ACTIONSETS_ALREADY_ATTACHED;
  if(reject_attach&&std::find(info->actionSets,info->actionSets+info->countActionSets,own_set)!=info->actionSets+info->countActionSets)
    return XR_ERROR_RUNTIME_FAILURE;
  Check(info->next==reinterpret_cast<void*>(0x3333),"attachment extension chain preserved");
  state.sets.assign(info->actionSets,info->actionSets+info->countActionSets); state.attached=true;
  SetLastError(0x13572468); return XR_SUCCESS;
}
XrResult XRAPI_CALL Sync(XrSession handle,const XrActionsSyncInfo* info) {
  ++syncs;
  Check(info->next==reinterpret_cast<void*>(0x4444),"synchronization extension chain preserved");
  if(!runtime_sessions.at(handle).attached) return XR_ERROR_ACTIONSET_NOT_ATTACHED;
  for(unsigned i=0;i<info->countActiveActionSets;++i)
    if(reject_sync&&info->activeActionSets[i].actionSet==own_set) return XR_ERROR_RUNTIME_FAILURE;
  current_active.assign(info->activeActionSets,info->activeActionSets+info->countActiveActionSets);
  SetLastError(0x24681357); return XR_SUCCESS;
}
XrResult XRAPI_CALL EnumerateSources(XrSession,const XrBoundSourcesForActionEnumerateInfo* info,std::uint32_t capacity,std::uint32_t* count,XrPath* sources) {
  const auto found=std::find(own_actions.begin(),own_actions.end(),info->action);
  Check(found!=own_actions.end()&&capacity==0&&sources==nullptr,"bound-source discovery queries only own haptic output count");
  const auto output=static_cast<unsigned>(found-own_actions.begin());
  const auto& bindings=profile_bindings[active_profiles[output%2]];
  const bool bound=std::any_of(bindings.begin(),bindings.end(),[&](const auto& b){return b.action==info->action;});
  *count=outputs_bound&&endpoint_bound[output]&&bound?1:0; return XR_SUCCESS;
}
XrResult XRAPI_CALL Apply(XrSession session,const XrHapticActionInfo* info,const XrHapticBaseHeader* feedback) {
  const auto found=std::find(own_actions.begin(),own_actions.end(),info->action);
  Check(found!=own_actions.end(),"only own vibration action written");
  const auto index=static_cast<unsigned>(found-own_actions.begin()); ++attempts[index];
  Check(info->subactionPath==(index%2?12:11),"output cannot cross into the other hand");
  if(index>=2&&reject_trigger_apply[index-2]) return XR_ERROR_FEATURE_UNSUPPORTED;
  const auto* vibration=reinterpret_cast<const XrHapticVibration*>(feedback);
  Check(vibration->type==XR_TYPE_HAPTIC_VIBRATION,"finite vibration feedback type");
  output.push_back({session,info->action,info->subactionPath,vibration->duration,vibration->frequency,vibration->amplitude});
  SetLastError(0xabcdef01); return XR_SUCCESS;
}
XrResult XRAPI_CALL Stop(XrSession,const XrHapticActionInfo* info) {
  const auto found=std::find(own_actions.begin(),own_actions.end(),info->action);
  Check(found!=own_actions.end(),"only own vibration action stopped"); ++stops;
  stopped_outputs.push_back(static_cast<unsigned>(found-own_actions.begin()));
  SetLastError(0xcdef0123); return XR_SUCCESS;
}
XrResult XRAPI_CALL Gipa(XrInstance instance,const char* name,PFN_xrVoidFunction* function) {
  requested_functions.emplace_back(name); *function=nullptr;
#define PROVIDE(api,implementation) if(std::strcmp(name,#api)==0) { auto original=&implementation; std::memcpy(function,&original,sizeof(original)); SetLastError(0x543210fe); return XR_SUCCESS; }
  PROVIDE(xrEnumerateInstanceExtensionProperties,EnumerateExtensions)
  if(!instance) return XR_ERROR_HANDLE_INVALID;
  PROVIDE(xrGetInstanceProcAddr,Gipa)
  PROVIDE(xrDestroyInstance,DestroyInstance)
  PROVIDE(xrCreateSession,CreateSession)
  PROVIDE(xrDestroySession,DestroySession)
  PROVIDE(xrEndSession,EndSession)
  PROVIDE(xrEndFrame,EndFrame)
  PROVIDE(xrPollEvent,PollEvent)
  PROVIDE(xrStringToPath,StringToPath)
  PROVIDE(xrGetCurrentInteractionProfile,CurrentProfile)
  PROVIDE(xrCreateActionSet,CreateActionSet)
  PROVIDE(xrDestroyActionSet,DestroyActionSet)
  PROVIDE(xrCreateAction,CreateAction)
  PROVIDE(xrSuggestInteractionProfileBindings,Suggest)
  PROVIDE(xrAttachSessionActionSets,Attach)
  PROVIDE(xrSyncActions,Sync)
  PROVIDE(xrEnumerateBoundSourcesForAction,EnumerateSources)
  PROVIDE(xrApplyHapticFeedback,Apply)
  PROVIDE(xrStopHapticFeedback,Stop)
#undef PROVIDE
  return XR_ERROR_FUNCTION_UNSUPPORTED;
}
template<class T> T Function(const XrNegotiateApiLayerRequest& layer,XrInstance instance,const char* name) {
  PFN_xrVoidFunction raw=nullptr; SetLastError(0xaabbccdd);
  Check(layer.getInstanceProcAddr(instance,name,&raw)==XR_SUCCESS&&raw,"layer resolves runtime function");
  Check(GetLastError()==0x543210fe,"runtime procedure lookup LastError preserved");
  T function=nullptr; std::memcpy(&function,&raw,sizeof(function)); return function;
}
XrInstance Instance(const XrNegotiateApiLayerRequest& request,XrVersion version=XR_MAKE_VERSION(1,0,0),bool fb=false) {
  XrApiLayerNextInfo next{}; next.structType=XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO;
  next.structVersion=XR_API_LAYER_NEXT_INFO_STRUCT_VERSION; next.structSize=sizeof(next);
  std::strcpy(next.layerName,"XR_APILAYER_ED_haptics"); next.nextGetInstanceProcAddr=&Gipa; next.nextCreateApiLayerInstance=&NextCreate;
  XrApiLayerCreateInfo layer{}; layer.structType=XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO;
  layer.structVersion=XR_API_LAYER_CREATE_INFO_STRUCT_VERSION; layer.structSize=sizeof(layer);
  layer.nextInfo=&next; layer.loaderInstance=reinterpret_cast<void*>(0x999);
  const char* extensions[]{"XR_TEST_application_extension","XR_FB_touch_controller_pro"};
  XrInstanceCreateInfo info{}; info.type=XR_TYPE_INSTANCE_CREATE_INFO; info.applicationInfo.apiVersion=version;
  info.enabledExtensionCount=fb?2:1; info.enabledExtensionNames=extensions;
  expected_create=&info; XrInstance instance=XR_NULL_HANDLE;
  const auto result=request.createApiLayerInstance(&info,&layer,&instance);
  if(reject_optional_create)
    Check(result==XR_ERROR_EXTENSION_NOT_PRESENT&&GetLastError()==0xbad&&instance==XR_NULL_HANDLE,"failed application instance request is preserved without retry");
  else Check(result==XR_SUCCESS&&GetLastError()==0x11223344,"instance create result and LastError preserved");
  Check(layer.nextInfo==&next&&next.next==nullptr,"caller loader chain not mutated");
  return instance;
}
XrSession Session(const XrNegotiateApiLayerRequest& layer,XrInstance instance) {
  XrSessionCreateInfo info{}; info.type=XR_TYPE_SESSION_CREATE_INFO; info.next=reinterpret_cast<void*>(0x1111);
  XrSession session=XR_NULL_HANDLE;
  Check(Function<PFN_xrCreateSession>(layer,instance,"xrCreateSession")(instance,&info,&session)==XR_SUCCESS&&GetLastError()==0x33445566,"session create result and LastError preserved");
  return session;
}
void Focus(const XrNegotiateApiLayerRequest& layer,XrInstance instance,XrSession session,XrSessionState state) {
  event_session=session; event_state=state;
  XrEventDataBuffer buffer{}; buffer.type=XR_TYPE_EVENT_DATA_BUFFER;
  const auto before=polls;
  Check(Function<PFN_xrPollEvent>(layer,instance,"xrPollEvent")(instance,&buffer)==XR_SUCCESS&&GetLastError()==0x6789abcd,"event result and LastError preserved");
  const auto* value=reinterpret_cast<XrEventDataSessionStateChanged*>(&buffer);
  Check(polls==before+1&&value->state==state&&value->session==session&&value->time==123456789,"session event returned unchanged without extra polling");
}
void AttachSets(const XrNegotiateApiLayerRequest& layer,XrInstance instance,XrSession session) {
  const XrActionSet original[]{app_set_a,app_set_b};
  XrSessionActionSetsAttachInfo info{}; info.type=XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
  info.next=reinterpret_cast<void*>(0x3333); info.countActionSets=2; info.actionSets=original;
  Check(Function<PFN_xrAttachSessionActionSets>(layer,instance,"xrAttachSessionActionSets")(session,&info)==XR_SUCCESS&&GetLastError()==0x13572468,"attach result and LastError preserved");
  Check(info.countActionSets==2&&info.actionSets==original&&original[0]==app_set_a&&original[1]==app_set_b,"caller attachment array unchanged");
}
void SyncSets(const XrNegotiateApiLayerRequest& layer,XrInstance instance,XrSession session) {
  const XrActiveActionSet original[]{{app_set_a,41},{app_set_b,42}};
  XrActionsSyncInfo info{}; info.type=XR_TYPE_ACTIONS_SYNC_INFO;
  info.next=reinterpret_cast<void*>(0x4444); info.countActiveActionSets=2; info.activeActionSets=original;
  Check(Function<PFN_xrSyncActions>(layer,instance,"xrSyncActions")(session,&info)==XR_SUCCESS&&GetLastError()==0x24681357,"sync result and LastError preserved");
  Check(info.countActiveActionSets==2&&info.activeActionSets==original&&original[0].subactionPath==41&&original[1].subactionPath==42,"caller active-set array unchanged");
}
void Frame(const XrNegotiateApiLayerRequest& layer,XrInstance instance,XrSession session) {
  const auto function=Function<PFN_xrEndFrame>(layer,instance,"xrEndFrame");
  XrFrameEndInfo info{}; info.type=XR_TYPE_FRAME_END_INFO;
  info.displayTime=987654321; info.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  expected_frame=&info; SetLastError(0x12345678);
  Check(function(session,&info)==XR_SUCCESS&&GetLastError()==0x98765432&&frame_incoming_error==0x12345678,"render call result, pointer and LastError preserved");
}
void SuggestProfile(const XrNegotiateApiLayerRequest& layer,XrInstance instance,XrPath profile) {
  const XrActionSuggestedBinding original[]{{app_action_a,31},{app_action_b,32}};
  XrInteractionProfileSuggestedBinding info{}; info.type=XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
  info.next=reinterpret_cast<void*>(0x2222); info.interactionProfile=profile;
  info.countSuggestedBindings=2; info.suggestedBindings=original;
  Check(Function<PFN_xrSuggestInteractionProfileBindings>(layer,instance,"xrSuggestInteractionProfileBindings")(instance,&info)==XR_SUCCESS&&GetLastError()==0x76543210,
    "localized augmentation preserves original suggestion result and LastError");
  Check(current_bindings.size()>=2&&current_bindings[0].action==app_action_a&&current_bindings[0].binding==31&&
    current_bindings[1].action==app_action_b&&current_bindings[1].binding==32&&info.suggestedBindings==original&&info.countSuggestedBindings==2,
    "localized profiles retain all original controls and caller arrays");
}
void ExpectedOutputs(const XrNegotiateApiLayerRequest& layer,XrInstance instance,XrSession session,
    const std::array<float,4>& amplitudes,const char* message) {
  const auto before=output.size(); Frame(layer,instance,session);
  std::array<unsigned,4> counts{};
  for(std::size_t i=before;i<output.size();++i) {
    const auto found=std::find(own_actions.begin(),own_actions.end(),output[i].action);
    Check(found!=own_actions.end(),message);
    const auto index=static_cast<unsigned>(found-own_actions.begin()); ++counts[index];
    Check(output[i].hand==(index%2?12:11)&&output[i].duration==60000000&&output[i].frequency==0&&
      std::abs(output[i].amplitude-amplitudes[index])<.000001f,message);
  }
  for(unsigned i=0;i<4;++i) Check(counts[i]==(amplitudes[i]>0?1u:0u),message);
}
void Close(const XrNegotiateApiLayerRequest& layer,XrInstance instance,XrSession session) {
  Check(Function<PFN_xrDestroySession>(layer,instance,"xrDestroySession")(session)==XR_SUCCESS,"localized session closes");
  Check(Function<PFN_xrDestroyInstance>(layer,instance,"xrDestroyInstance")(instance)==XR_SUCCESS,"localized instance closes");
}
void LocalizedRouting(const XrNegotiateApiLayerRequest& request) {
  profile_bindings.clear(); active_profiles={1000,1000}; endpoint_bound={true,true,true,true};
  source_frame={.25f,.5f,1,.75f};
  auto instance=Instance(request,XR_MAKE_VERSION(1,1,0)); auto session=Session(request,instance);
  SuggestProfile(request,instance,1000);
  Check(current_bindings.size()==6&&current_bindings[4].binding==15&&current_bindings[5].binding==16,
    "1.1 profile adds only separate documented trigger outputs beside original controls");
  AttachSets(request,instance,session); Focus(request,instance,session,XR_SESSION_STATE_FOCUSED); SyncSets(request,instance,session);
  ExpectedOutputs(request,instance,session,{.3f,.3f,.6f,.45f},"localized triggers remain distinct from both main outputs");
  source_frame={.8f,.2f,.1f,.25f};
  ExpectedOutputs(request,instance,session,{.48f,.48f,.06f,.15f},"weaker localized triggers survive a stronger body effect");
  const auto standby_stops=stops; own_output=false;
  ExpectedOutputs(request,instance,session,{},"localized standby never emits or clears another route");
  Check(stops==standby_stops,"localized standby sends no stop calls"); own_output=true;
  source_frame={.25f,.5f,1,.75f};
  ExpectedOutputs(request,instance,session,{.3f,.3f,.6f,.45f},"localized output resumes with original channels");
  const auto focus_stops=stops; Focus(request,instance,session,XR_SESSION_STATE_VISIBLE);
  Check(stops==focus_stops+4,"focus loss stops all four owned endpoints once");
  Focus(request,instance,session,XR_SESSION_STATE_FOCUSED); SyncSets(request,instance,session);
  ExpectedOutputs(request,instance,session,{.3f,.3f,.6f,.45f},"localized output resumes after fresh app synchronization");
  endpoint_bound[3]=false; const auto lost_trigger_stops=stops; SyncSets(request,instance,session);
  Check(stops==lost_trigger_stops+1&&stopped_outputs.back()==3,"losing one trigger stops only that localized endpoint");
  ExpectedOutputs(request,instance,session,{.3f,.45f,.6f,0},"missing right trigger falls back only into the right hand");
  endpoint_bound[3]=true; SyncSets(request,instance,session);
  SuggestProfile(request,instance,1002); active_profiles[0]=1002; SyncSets(request,instance,session);
  ExpectedOutputs(request,instance,session,{.6f,.3f,0,.45f},"Touch Plus profile never inherits another profile's trigger capability");
  active_profiles={1000,1000}; SyncSets(request,instance,session);
  reject_localized_suggestion=true; SuggestProfile(request,instance,1000); reject_localized_suggestion=false;
  Check(current_bindings.size()==4,"unsupported localized binding retains body outputs and original controls");
  SyncSets(request,instance,session);
  ExpectedOutputs(request,instance,session,{.6f,.45f,0,0},"localized binding rejection falls back without losing controls");
  SuggestProfile(request,instance,1000); SyncSets(request,instance,session);
  ExpectedOutputs(request,instance,session,{.3f,.3f,.6f,.45f},"localized binding can recover on a later valid app suggestion");
  reject_trigger_apply[0]=true; const auto failed_attempts=attempts[2];
  ExpectedOutputs(request,instance,session,{.6f,.3f,0,.45f},"failed left trigger falls back in the same frame without affecting right trigger");
  Check(attempts[2]==failed_attempts+1,"one failed localized submission observed");
  SyncSets(request,instance,session);
  ExpectedOutputs(request,instance,session,{.6f,.3f,0,.45f},"failed trigger stays on fallback until bindings change");
  Check(attempts[2]==failed_attempts+1,"failed trigger is not retried every frame");
  endpoint_bound[2]=false; SyncSets(request,instance,session); endpoint_bound[2]=true; reject_trigger_apply[0]=false; SyncSets(request,instance,session);
  ExpectedOutputs(request,instance,session,{.3f,.3f,.6f,.45f},"new binding availability permits localized recovery");
  endpoint_bound[0]=false; SyncSets(request,instance,session);
  ExpectedOutputs(request,instance,session,{0,.3f,.6f,.45f},"unbound main output never suppresses an independently bound trigger");
  endpoint_bound[0]=true; SyncSets(request,instance,session);
  source_frame={.25f,.5f,0,.75f}; const auto zero_stops=stops;
  ExpectedOutputs(request,instance,session,{.3f,.3f,0,.45f},"trigger zero leaves body and opposite trigger running");
  Check(stops==zero_stops+1&&stopped_outputs.back()==2,"trigger zero stops only its own endpoint");
  zero_frame=true; const auto all_zero_stops=stops;
  ExpectedOutputs(request,instance,session,{},"all-channel zero sends no nonzero requests");
  Frame(request,instance,session); Check(stops==all_zero_stops+3,"remaining endpoints stop once on all-channel zero"); zero_frame=false;
  source_frame={.25f,.5f,1,.75f}; ExpectedOutputs(request,instance,session,{.3f,.3f,.6f,.45f},"all endpoints resume after authored zero");
  const auto expired=stops; EDOpenXRSetFixtureTime(1120); Focus(request,instance,session,XR_SESSION_STATE_VISIBLE);
  Check(stops==expired,"expired localized pulses never send a late stop");
  Focus(request,instance,session,XR_SESSION_STATE_FOCUSED); SyncSets(request,instance,session);
  ExpectedOutputs(request,instance,session,{.3f,.3f,.6f,.45f},"fresh localized requests survive expired output history");
  const auto shutdown_stops=stops; Close(request,instance,session);
  Check(stops==shutdown_stops+4,"session shutdown stops every owned localized and body endpoint");

  profile_bindings.clear(); active_profiles={1001,1001}; runtime_supports_fb=true;
  const auto before=instance_requests;
  instance=Instance(request,XR_MAKE_VERSION(1,0,0),true); Check(instance_requests==before+1&&instance_fb_enabled,"application-negotiated extension enables localized 1.0 output");
  session=Session(request,instance); SuggestProfile(request,instance,1001);
  Check(current_bindings.size()==6&&current_bindings[4].binding==17&&current_bindings[5].binding==18,"1.0 extension uses suffixed trigger paths");
  AttachSets(request,instance,session); Focus(request,instance,session,XR_SESSION_STATE_FOCUSED); SyncSets(request,instance,session);
  ExpectedOutputs(request,instance,session,{.3f,.3f,.6f,.45f},"extension-backed trigger amplitudes retain left and right identity"); Close(request,instance,session);

  profile_bindings.clear(); reject_trigger_creation=true;
  instance=Instance(request,XR_MAKE_VERSION(1,0,0),true); session=Session(request,instance);
  SuggestProfile(request,instance,1001); AttachSets(request,instance,session); Focus(request,instance,session,XR_SESSION_STATE_FOCUSED); SyncSets(request,instance,session);
  ExpectedOutputs(request,instance,session,{.6f,.3f,0,.45f},"one optional action creation failure preserves the other three outputs"); Close(request,instance,session); reject_trigger_creation=false;

  profile_bindings.clear(); active_profiles={999,999}; reject_optional_create=true;
  const auto previous_requests=instance_requests;
  instance=Instance(request); Check(instance_requests==previous_requests+1&&instance==XR_NULL_HANDLE,"failed application creation is forwarded exactly once");
  reject_optional_create=false; active_profiles={1001,1001}; instance=Instance(request);
  Check(!instance_fb_enabled,"runtime advertisement never enables an unrequested extension");
  session=Session(request,instance); SuggestProfile(request,instance,1001);
  AttachSets(request,instance,session); Focus(request,instance,session,XR_SESSION_STATE_FOCUSED); SyncSets(request,instance,session);
  ExpectedOutputs(request,instance,session,{.6f,.45f,0,0},"unnegotiated Touch Pro profile retains hand-level fallback"); Close(request,instance,session);
  runtime_supports_fb=false;
}
}
int main(int argc,char** argv) {
  if(argc>1) result_path=argv[1];
  XrNegotiateLoaderInfo loader{}; loader.structType=XR_LOADER_INTERFACE_STRUCT_LOADER_INFO;
  loader.structVersion=XR_LOADER_INFO_STRUCT_VERSION; loader.structSize=sizeof(loader);
  loader.minInterfaceVersion=1; loader.maxInterfaceVersion=1; loader.maxApiVersion=XR_MAKE_VERSION(1,1,0);
  XrNegotiateApiLayerRequest request{}; request.structType=XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST;
  request.structVersion=XR_API_LAYER_INFO_STRUCT_VERSION; request.structSize=sizeof(request);
  Check(xrNegotiateLoaderApiLayerInterface(&loader,"XR_APILAYER_ED_haptics",&request)==XR_SUCCESS&&request.layerInterfaceVersion==1,"loader negotiation succeeds");
  auto incompatible=loader; incompatible.maxInterfaceVersion=0;
  Check(xrNegotiateLoaderApiLayerInterface(&incompatible,"XR_APILAYER_ED_haptics",&request)==XR_ERROR_INITIALIZATION_FAILED,"unsupported loader interface rejected");
  EDOpenXRSetFixtureReader(&Read,TRUE); EDOpenXRSetFixtureTime(1000);
  auto instance=Instance(request); Check(creates==1,"target instance creates one output-only set");
  auto session=Session(request,instance);
  Frame(request,instance,session);
  Check(attaches==0&&syncs==0&&output.empty(),"no independent attachment or sync can steal controls");
  const auto suggest=Function<PFN_xrSuggestInteractionProfileBindings>(request,instance,"xrSuggestInteractionProfileBindings");
  const XrActionSuggestedBinding originals[]{{app_action_a,31},{app_action_b,32}};
  XrInteractionProfileSuggestedBinding info{}; info.type=XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
  info.next=reinterpret_cast<void*>(0x2222); info.interactionProfile=999;
  info.countSuggestedBindings=2; info.suggestedBindings=originals;
  Check(suggest(instance,&info)==XR_SUCCESS&&GetLastError()==0x76543210,"original suggestion result and LastError preserved");
  Check(current_bindings.size()==4&&current_bindings[0].action==app_action_a&&current_bindings[0].binding==31&&
    current_bindings[1].action==app_action_b&&current_bindings[1].binding==32&&current_bindings[2].action==own_action&&current_bindings[2].binding==13&&current_bindings[3].action==own_actions[1]&&current_bindings[3].binding==14,
    "all application controls retained beside both haptic bindings");
  Check(info.countSuggestedBindings==2&&info.suggestedBindings==originals,"caller binding array not mutated");
  const XrActionSuggestedBinding replacement[]{{app_action_b,33}};
  info.countSuggestedBindings=1; info.suggestedBindings=replacement;
  Check(suggest(instance,&info)==XR_SUCCESS&&current_bindings.size()==3&&current_bindings[0].action==app_action_b&&current_bindings[0].binding==33,
        "repeated suggestion preserves exactly the latest application bindings");
  reject_augmented_suggestion=true;
  Check(suggest(instance,&info)==XR_SUCCESS&&GetLastError()==0x76543210&&current_bindings.size()==1&&current_bindings[0].binding==33,
        "unsupported haptic binding never destroys successful application bindings");
  reject_augmented_suggestion=false;
  Check(suggest(instance,&info)==XR_SUCCESS,"haptic suggestions recover on later valid app call");
  AttachSets(request,instance,session);
  Check(runtime_sessions.at(session).sets==std::vector<XrActionSet>{app_set_a,app_set_b,own_set},"original attachment order retained with output set appended");
  Focus(request,instance,session,XR_SESSION_STATE_FOCUSED);
  Frame(request,instance,session); Check(output.empty()&&syncs==0,"focused session requires application's own synchronization");
  SyncSets(request,instance,session);
  Check(current_active.size()==3&&current_active[0].actionSet==app_set_a&&current_active[0].subactionPath==41&&
    current_active[1].actionSet==app_set_b&&current_active[1].subactionPath==42&&current_active[2].actionSet==own_set,
    "all original active action sets and subaction restrictions preserved");
  Frame(request,instance,session);
  Check(output.size()==2&&output[0].session==session&&output[0].hand==11&&output[1].hand==12,
        "haptics use existing session and correct left/right subactions");
  Check(output[0].duration==60000000&&output[1].duration==60000000&&output[0].frequency==0&&output[1].frequency==0&&
        std::abs(output[0].amplitude-.6f)<.000001f&&std::abs(output[1].amplitude-.45f)<.000001f,
        "four-channel source maps to finite left/right haptic requests");
  own_output=false; const auto emitted=output.size(); const auto stopped=stops;
  Frame(request,instance,session); Frame(request,instance,session);
  Check(output.size()==emitted&&stops==stopped,"standby adapter emits no stops or zeros that could cancel another route");
  own_output=true; Frame(request,instance,session);
  Focus(request,instance,session,XR_SESSION_STATE_VISIBLE);
  Check(stops==stopped+2,"focus loss stops only the layer's own recent effects");
  Frame(request,instance,session); Check(stops==stopped+2,"unfocused adapter does not repeat stop output");
  SyncSets(request,instance,session);
  const auto visible_output=output.size(); Frame(request,instance,session);
  Check(output.size()==visible_output,"session state independently forbids vibration outside focus");
  Focus(request,instance,session,XR_SESSION_STATE_FOCUSED);
  const auto before=output.size(); Frame(request,instance,session);
  Check(output.size()==before,"focus recovery waits for a fresh application sync");
  SyncSets(request,instance,session); Frame(request,instance,session);
  zero_frame=true; const auto previous=stops;
  Frame(request,instance,session); Frame(request,instance,session);
  Check(stops==previous+2,"authored zero stops prior effects once");
  zero_frame=false; Frame(request,instance,session); const auto expired_stops=stops;
  EDOpenXRSetFixtureTime(1060);
  Focus(request,instance,session,XR_SESSION_STATE_VISIBLE);
  Check(stops==expired_stops,"expired pulses never issue late stops against a new route");
  Focus(request,instance,session,XR_SESSION_STATE_FOCUSED); SyncSets(request,instance,session);
  outputs_bound=false; SyncSets(request,instance,session); const auto unbound=output.size(); Frame(request,instance,session);
  Check(output.size()==unbound,"no route acquired when own outputs are unbound");
  outputs_bound=true;
  reject_sync=true; SyncSets(request,instance,session); reject_sync=false;
  Check(current_active.size()==2&&current_active[0].actionSet==app_set_a&&current_active[1].actionSet==app_set_b,
        "optional output sync failure falls back to original controls");
  auto failed_session=Session(request,instance); reject_attach=true;
  AttachSets(request,instance,failed_session); reject_attach=false;
  Check(runtime_sessions.at(failed_session).sets==std::vector<XrActionSet>{app_set_a,app_set_b},"optional output attachment failure preserves original controls");
  auto late_session=Session(request,instance); Focus(request,instance,late_session,XR_SESSION_STATE_FOCUSED);
  const auto attach_count=attaches; Frame(request,instance,late_session);
  Check(attaches==attach_count,"frame-only application retains future attachment opportunity");
  AttachSets(request,instance,late_session); SyncSets(request,instance,late_session);
  const auto late_before=output.size(); Frame(request,instance,late_session);
  Check(output.size()==late_before+2,"late application input initialization activates haptics automatically");
  const auto destroy=Function<PFN_xrDestroySession>(request,instance,"xrDestroySession");
  Check(destroy(late_session)==XR_SUCCESS&&GetLastError()==0x44556677,"session destruction preserves result and LastError");
  Check(destroy(failed_session)==XR_SUCCESS&&destroy(session)==XR_SUCCESS,"all owned fixture sessions close");
  Check(Function<PFN_xrDestroyInstance>(request,instance,"xrDestroyInstance")(instance)==XR_SUCCESS,"instance destruction forwarded");
  LocalizedRouting(request);
  EDOpenXRSetFixtureReader(&Read,FALSE);
  const auto sets_before=creates; instance=Instance(request); session=Session(request,instance);
  Check(creates==sets_before,"non-Elite process creates no haptic actions");
  const auto raw_attach=Function<PFN_xrAttachSessionActionSets>(request,instance,"xrAttachSessionActionSets");
  Check(raw_attach==&Attach,"non-Elite input entry point is untouched");
  AttachSets(request,instance,session);
  Check(runtime_sessions.at(session).sets.size()==2,"non-Elite application attachment remains exact");
  Frame(request,instance,session);
  Check(Function<PFN_xrDestroySession>(request,instance,"xrDestroySession")(session)==XR_SUCCESS,"non-Elite session destruction passes through");
  Check(Function<PFN_xrDestroyInstance>(request,instance,"xrDestroyInstance")(instance)==XR_SUCCESS,"non-Elite instance closes");
  for(const auto& name:requested_functions)
    Check(name.find("GetActionState")==std::string::npos&&name.find("Locate")==std::string::npos&&name.find("Tracking")==std::string::npos,
          "no controller input, pose or tracking getters requested");
  const char* message="PASS OpenXR haptic-only layer: exact controls retained, negotiated localized triggers, per-hand fallback, finite outputs, focus/ownership/expiry and non-target pass-through";
  Result(message); std::puts(message);
}
