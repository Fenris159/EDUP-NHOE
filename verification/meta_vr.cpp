#define ED_META_CONTROLLED_CLOCK 1
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
namespace edtrace { namespace {
std::uint64_t fixture_now=1000;
std::uint64_t MetaControlledClock() noexcept { return fixture_now; }
}}
#ifndef ED_META_OUTPUT_SOURCE
#define ED_META_OUTPUT_SOURCE "../vr/meta_output_win.cpp"
#endif
#include ED_META_OUTPUT_SOURCE

namespace {
struct Snapshot {
  unsigned writes=0,status_reads=0,bad_session=0,input_reads=0,tracking_reads=0;
  unsigned left_writes=0,right_writes=0,destroys=0;
  float left_frequency=0,left_amplitude=0,right_frequency=0,right_amplitude=0;
  std::uintptr_t scale=0,layers=0; long long index=0; unsigned count=0;
  DWORD render_incoming=0,create_incoming=0,destroy_incoming=0;
};
bool allow=false,reported_ready=false;
unsigned acknowledgments=0;
void (*sdk_read)(Snapshot*)=nullptr;
edtrace::HapticFrame source{};
void Require(bool value,const char* message) {
  if(!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
template<class T> T Export(HMODULE module,const char* name) {
  const auto raw=GetProcAddress(module,name); T result{}; std::memcpy(&result,&raw,sizeof(result)); Require(result!=nullptr,name); return result;
}
bool Close(float a,float b) { return std::fabs(a-b)<.000001f; }
}
namespace edtrace {
void VrOutputAcknowledgeStop(DWORD backend) noexcept {
  Require(backend==static_cast<DWORD>(VrBackend::Meta),"only Meta stop acknowledged");
  Snapshot snapshot{}; Require(sdk_read!=nullptr,"SDK fixture bound before acknowledgment"); sdk_read(&snapshot);
  Require(snapshot.left_amplitude==0&&snapshot.right_amplitude==0&&snapshot.bad_session==0,
          "stop acknowledgment follows both successful SDK zero writes");
  ++acknowledgments;
}
BOOL WINAPI VrOutputReadFrame(DWORD backend,BOOL ready,VrBridgeFrame* frame) noexcept {
  Require(backend==static_cast<DWORD>(VrBackend::Meta),"only Meta output lease requested");
  reported_ready=ready!=FALSE; frame->frame=allow&&ready?source:HapticFrame{};
  frame->generation=1; return allow&&ready?TRUE:FALSE;
}
}
int main(int argc,char** argv) {
  Require(argc==2,"owned backend required");
  const auto library=LoadLibraryA(argv[1]); Require(library!=nullptr,"owned backend loads");
  auto reset=Export<void(*)()>(library,"MockMetaReset");
  auto read=Export<void(*)(Snapshot*)>(library,"MockMetaRead");
  sdk_read=read;
  auto focus=Export<void(*)(int)>(library,"MockMetaFocus");
  auto status_result=Export<void(*)(int)>(library,"MockMetaStatusResult");
  auto write_result=Export<void(*)(int)>(library,"MockMetaWriteResult");
  auto create=Export<int(*)(void**,void*)>(library,"ovr_Create");
  auto destroy=Export<void(*)(void*)>(library,"ovr_Destroy");
  auto shutdown=Export<void(*)()>(library,"ovr_Shutdown");
  auto render=Export<int(*)(void*,long long,const void*,const void* const*,unsigned)>(library,"ovr_EndFrame");
  const auto input=Export<int(*)(void*,unsigned,void*)>(library,"ovr_GetInputState");
  const auto tracking=Export<int(*)(void*,double,int)>(library,"ovr_GetTrackingState");
  unsigned char input_before[16]{},tracking_before[16]{};
  std::memcpy(input_before,reinterpret_cast<void*>(input),16); std::memcpy(tracking_before,reinterpret_cast<void*>(tracking),16);
  reset(); edtrace::MetaOutputPoll(); Require(edtrace::MetaOutputGetStats().installed,"loaded LibOVR acquired automatically");
  Require(!std::memcmp(input_before,reinterpret_cast<void*>(input),16)&&!std::memcmp(tracking_before,reinterpret_cast<void*>(tracking),16),"input and tracking entry points unchanged");
  void* session=nullptr;
  SetLastError(0x10203040); Require(create(&session,reinterpret_cast<void*>(0x23))==42&&GetLastError()==0xaabbcc01,"create result and LastError preserved");
  Snapshot state{}; read(&state); Require(state.create_incoming==0x10203040,"create incoming LastError preserved");
  edtrace::MetaOutputPoll(); read(&state); Require(state.writes==0&&state.status_reads==0,"session creation alone sends no haptics");
  const void* layers[2]{reinterpret_cast<void*>(0x78),reinterpret_cast<void*>(0x79)};
  SetLastError(0x31415926); Require(render(session,99,reinterpret_cast<void*>(0x45),layers,2)==73&&GetLastError()==0xaabbcc03,"render result and LastError preserved");
  read(&state); Require(state.index==99&&state.scale==0x45&&state.layers==reinterpret_cast<std::uintptr_t>(layers)&&state.count==2&&state.render_incoming==0x31415926,"render arguments forwarded untouched");
  edtrace::MetaOutputPoll(); read(&state); Require(reported_ready&&state.writes==0,"standby advertises readiness without zero writes");
  source={0,0,.75f,0}; allow=true; SetLastError(0x55667788); edtrace::MetaOutputPoll();
  Require(GetLastError()==0x55667788,"worker preserves LastError"); read(&state);
  Require(state.writes==2&&Close(state.left_amplitude,.45f)&&state.left_frequency==1&&state.right_amplitude==0&&state.right_frequency==0,"left trigger maps to left Touch only");
  source={.5f,0,0,0}; edtrace::MetaOutputPoll(); read(&state);
  Require(Close(state.left_amplitude,.3f)&&Close(state.right_amplitude,.3f)&&state.left_frequency==.5f&&state.right_frequency==.5f,"low main band reaches both Touch controllers");
  source={}; const auto zero_ack=acknowledgments; edtrace::MetaOutputPoll();
  Require(acknowledgments==zero_ack+1,"owned zero frame acknowledges completed stop");
  source={.5f,0,0,0}; edtrace::MetaOutputPoll();
  const auto loss_ack=acknowledgments;
  allow=false; edtrace::MetaOutputPoll(); read(&state); const auto stopped_writes=state.writes;
  Require(state.left_amplitude==0&&state.right_amplitude==0&&acknowledgments==loss_ack+1,"ownership loss stops previous Meta output");
  edtrace::MetaOutputPoll(); read(&state); Require(state.writes==stopped_writes&&reported_ready,"standby does not cancel another backend");
  allow=true; source={0,0,0,.5f}; edtrace::MetaOutputPoll(); read(&state);
  Require(state.left_amplitude==0&&Close(state.right_amplitude,.3f)&&state.right_frequency==1,"right trigger stays on right Touch");
  write_result(-1); const auto failed_ack=acknowledgments; allow=false; edtrace::MetaOutputPoll();
  Require(acknowledgments==failed_ack,"failed SDK zero cannot acknowledge successful stop");
  write_result(0); allow=true; edtrace::MetaOutputPoll();
  focus(0); edtrace::MetaOutputPoll(); read(&state);
  Require(state.left_amplitude==0&&state.right_amplitude==0&&!reported_ready,"native input focus loss stops vibration without taking focus");
  focus(1); edtrace::MetaOutputPoll();
  edtrace::fixture_now=1250; edtrace::MetaOutputPoll(); read(&state);
  Require(!reported_ready&&state.left_amplitude==0&&state.right_amplitude==0,"render watchdog stops at exact lease boundary");
  render(session,100,nullptr,nullptr,0); edtrace::MetaOutputPoll();
  status_result(-1); edtrace::MetaOutputPoll(); read(&state);
  Require(!reported_ready&&state.left_amplitude==0&&state.right_amplitude==0,"failed session status stops output");
  status_result(0); edtrace::MetaOutputPoll();
  SetLastError(0x87654321); destroy(session); Require(GetLastError()==0xaabbcc02,"destroy result LastError preserved");
  read(&state); Require(state.destroy_incoming==0x87654321&&state.bad_session==0,"stop precedes destroy and original LastError reaches runtime");
  const auto dead_writes=state.writes,dead_reads=state.status_reads;
  render(session,101,nullptr,nullptr,0); edtrace::MetaOutputPoll(); read(&state);
  Require(state.writes==dead_writes&&state.status_reads==dead_reads&&!edtrace::MetaOutputGetStats().session,"late render cannot resurrect destroyed session");
  create(&session,nullptr); render(session,102,nullptr,nullptr,0); edtrace::MetaOutputPoll();
  shutdown(); Require(GetLastError()==0xaabbcc04,"SDK shutdown LastError preserved"); read(&state);
  Require(state.left_amplitude==0&&state.right_amplitude==0&&state.bad_session==0,"SDK shutdown stops before invalidating session");
  const auto shutdown_writes=state.writes,shutdown_reads=state.status_reads;
  render(session,102,nullptr,nullptr,0); edtrace::MetaOutputPoll(); read(&state);
  Require(state.writes==shutdown_writes&&state.status_reads==shutdown_reads,"SDK shutdown blocks late render resurrection");
  create(&session,nullptr); render(session,102,nullptr,nullptr,0); edtrace::MetaOutputPoll();
  edtrace::MetaOutputStop(); read(&state);
  Require(state.left_amplitude==0&&state.right_amplitude==0,"shutdown sends final zero");
  const auto final_writes=state.writes; render(session,103,nullptr,nullptr,0); edtrace::MetaOutputPoll(); read(&state);
  Require(state.writes==final_writes,"stopped hooks only forward");
  Require(state.input_reads==0&&state.tracking_reads==0&&state.bad_session==0,"haptics never reads controls/tracking or uses dead session");
  Require(input(session,0,nullptr)==123&&tracking(session,0,0)==456,"original input and tracking remain callable");
  std::puts("PASS Meta auto acquisition, channels, focus, output arbitration, watchdog, lifetime and input isolation");
}
