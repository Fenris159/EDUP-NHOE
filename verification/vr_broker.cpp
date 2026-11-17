#include <winsock2.h>
#include "edtrace/vr_output.hpp"
#include "edtrace/vr_backend.hpp"
#include <cstdio>
#include <cstdlib>

namespace {
void Check(bool ok,const char* text) {
  if(!ok) { std::fprintf(stderr,"FAIL: %s\n",text); std::exit(1); }
}
}
int main() {
  // Occupy only an owned loopback socket. A relay conflict must not disable
  // native in-session adapters, and never causes a second socket takeover.
  WSADATA data{}; Check(WSAStartup(MAKEWORD(2,2),&data)==0,"fixture winsock");
  const auto blocked=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP); Check(blocked!=INVALID_SOCKET,"fixture socket");
  sockaddr_in address{}; address.sin_family=AF_INET; address.sin_addr.s_addr=htonl(INADDR_LOOPBACK); address.sin_port=htons(26761);
  Check(bind(blocked,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"fixture owns relay port");
  Check(edtrace::VrOutputStart(true)!=ERROR_SUCCESS,"port conflict is reported");
  Check(edtrace::VrOutputGetStats().enabled&&!edtrace::VrOutputGetStats().listening,"native adapters survive relay conflict");
  edtrace::VrOutputPoll(true,true);
  edtrace::VrOutputPublish({.25f,.5f,.75f,1});
  edtrace::VrBridgeFrame frame{}; SetLastError(0x12345);
  Check(edtrace::VrOutputReadFrame(2,TRUE,&frame)&&GetLastError()==0x12345,"native session automatically acquires output without input hooks");
  Check(frame.frame.low==.25f&&frame.frame.high==.5f&&frame.frame.left==.75f&&frame.frame.right==1,"all four source channels reach native adapter");
  frame={};
  Check(!edtrace::VrOutputReadFrame(1,TRUE,&frame)&&!edtrace::HapticNonzero(frame.frame),"standby gets no output even with live source");
  Check(edtrace::VrOutputGetStats().backend==2,"standby cannot steal native route");
  frame={};
  Check(!edtrace::VrOutputReadFrame(3,TRUE,&frame)&&!edtrace::HapticNonzero(frame.frame),"higher priority waits for old finite pulses");
  frame={}; frame.version=2;
  Check(!edtrace::VrOutputReadFrame(3,TRUE,&frame)&&!edtrace::HapticNonzero(frame.frame),"unknown bridge ABI stays inactive");
  edtrace::VrOutputPoll(false,true);
  frame={}; Check(!edtrace::VrOutputReadFrame(3,TRUE,&frame)&&!edtrace::HapticNonzero(frame.frame),"disabled source gain cannot emit through VR");
  edtrace::VrOutputStop();
  frame={}; Check(!edtrace::VrOutputReadFrame(3,TRUE,&frame)&&!edtrace::HapticNonzero(frame.frame),"stopped bridge remains silent");
  closesocket(blocked); WSACleanup();
  std::puts("PASS native route ownership, relay conflict, four channels, ABI and source guards");
}
