#include <windows.h>
#include <cstdint>
#include <cstring>
struct Snapshot {
  unsigned writes=0,status_reads=0,bad_session=0,input_reads=0,tracking_reads=0;
  unsigned left_writes=0,right_writes=0,destroys=0;
  float left_frequency=0,left_amplitude=0,right_frequency=0,right_amplitude=0;
  std::uintptr_t scale=0,layers=0; long long index=0; unsigned count=0;
  DWORD render_incoming=0,create_incoming=0,destroy_incoming=0;
};
namespace {
Snapshot snapshot;
void* expected=reinterpret_cast<void*>(0x12345678);
bool active=false,focus=true; int status_result=0,write_result=0;
}
extern "C" {
void MockMetaReset() { snapshot={}; active=false; focus=true; status_result=0; write_result=0; }
void MockMetaRead(Snapshot* result) { *result=snapshot; }
void MockMetaFocus(int value) { focus=value!=0; }
void MockMetaStatusResult(int value) { status_result=value; }
void MockMetaWriteResult(int value) { write_result=value; }
int ovr_Create(void** session,void*) {
  snapshot.create_incoming=GetLastError(); *session=expected; active=true; SetLastError(0xaabbcc01); return 42;
}
void ovr_Destroy(void* session) {
  snapshot.destroy_incoming=GetLastError(); if(session!=expected||!active) ++snapshot.bad_session;
  active=false; ++snapshot.destroys; SetLastError(0xaabbcc02);
}
void ovr_Shutdown() { active=false; SetLastError(0xaabbcc04); }
int ovr_EndFrame(void*,long long index,const void* scale,const void* const* layers,unsigned count) {
  snapshot.render_incoming=GetLastError(); snapshot.index=index; snapshot.scale=reinterpret_cast<std::uintptr_t>(scale);
  snapshot.layers=reinterpret_cast<std::uintptr_t>(layers); snapshot.count=count; SetLastError(0xaabbcc03); return 73;
}
int ovr_GetSessionStatus(void* session,void* result) {
  ++snapshot.status_reads; if(session!=expected||!active) ++snapshot.bad_session;
  const unsigned char status[9]{1,1,1,0,0,0,static_cast<unsigned char>(focus),0,0};
  std::memcpy(result,status,sizeof(status)); return status_result;
}
int ovr_SetControllerVibration(void* session,unsigned hand,float frequency,float amplitude) {
  ++snapshot.writes; if(session!=expected||!active) ++snapshot.bad_session;
  if(hand==1) { ++snapshot.left_writes; snapshot.left_frequency=frequency; snapshot.left_amplitude=amplitude; }
  else if(hand==2) { ++snapshot.right_writes; snapshot.right_frequency=frequency; snapshot.right_amplitude=amplitude; }
  else ++snapshot.bad_session;
  return write_result;
}
__attribute__((aligned(64),noinline)) int ovr_GetInputState(void*,unsigned,void*) { ++snapshot.input_reads; return 123; }
__attribute__((aligned(64),noinline)) int ovr_GetTrackingState(void*,double,int) { ++snapshot.tracking_reads; return 456; }
}
