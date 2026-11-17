#include "edtrace/vr_haptics.hpp"
#include "edtrace/vr_route_policy.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void Require(bool condition,const char* description) {
  if(!condition) { std::cerr<<"FAIL: "<<description<<'\n'; std::exit(1); }
}
void Pulse(edtrace::vrbridge::HandPulse pulse,float amplitude,float frequency) {
  Require(pulse.amplitude==amplitude&&pulse.frequency_hz==frequency,"hand amplitude and frequency");
}
}
int main() {
  using namespace edtrace;
  using namespace edtrace::vrbridge;
  auto hands=MapHands({.5f,0,0,0});
  Pulse(hands.left,.5f,80); Pulse(hands.right,.5f,80);
  hands=MapHands({0,.75f,0,0});
  Pulse(hands.left,.75f,160); Pulse(hands.right,.75f,160);
  hands=MapHands({0,0,0,.5f});
  Pulse(hands.left,0,0); Pulse(hands.right,.5f,160);
  hands=MapHands({.5f,.25f,.75f,0},.5f);
  Pulse(hands.left,.375f,160); Pulse(hands.right,.25f,80);
  hands=MapHands({std::numeric_limits<float>::quiet_NaN(),-1,2,0});
  Pulse(hands.left,1,160); Pulse(hands.right,0,0);
  hands=MapHands({1,1,1,1},0); Require(hands.left.amplitude==0&&hands.right.amplitude==0,"zero strength silences both hands");

  // Literal wire sample: Frame, active, nonce=1, sequence=2, low=.5,
  // high=.25, left=0, right=1. Expected bytes are hand-specified.
  const Packet wire{{'E','D','V','H',1,2,1,0,1,0,0,0,0,0,0,0,
                    2,0,0,0,0,0,0,0,0,0,0,0x3f,0,0,0x80,0x3e,
                    0,0,0,0,0,0,0x80,0x3f,0,0,0,0,0,0,0,0}};
  Message frame{};
  Require(Decode(wire.data(),wire.size(),frame)&&frame.kind==Kind::Frame&&frame.active&&frame.nonce==1&&frame.sequence==2&&
          frame.frame.low==.5f&&frame.frame.high==.25f&&frame.frame.left==0&&frame.frame.right==1,"decode independent wire sample");
  Require(Encode({Kind::Frame,true,1,2,{.5f,.25f,0,1}})==wire,"portable wire encoding");
  auto bad=wire; bad[4]=2; Require(!Decode(bad.data(),bad.size(),frame),"reject unknown protocol");
  bad=wire; bad[43]=1; Require(!Decode(bad.data(),bad.size(),frame),"reject reserved fields");
  bad=wire; bad[24]=0; bad[25]=0; bad[26]=0xc0; bad[27]=0x7f;
  Require(!Decode(bad.data(),bad.size(),frame),"reject nonfinite amplitude");
  bad=wire; bad[5]=1; Require(!Decode(bad.data(),bad.size(),frame),"poll cannot inject an output frame");
  Require(!Decode(wire.data(),47,frame),"reject truncated packet");
  const auto native_poll=Encode({Kind::NativeOpenXRPoll,true,1,2,{}});
  Require(Decode(native_poll.data(),native_poll.size(),frame)&&frame.kind==Kind::NativeOpenXRPoll,
          "native OpenXR has a separate output lease on the same relay");
  bad=wire; bad[5]=4; Require(!Decode(bad.data(),bad.size(),frame),"reject unknown relay request kind");
  Decode(wire.data(),wire.size(),frame);
  Require(MatchesPoll(frame,1,2,39),"accept current timely response");
  Require(!MatchesPoll(frame,1,2,40),"late packet must not renew output");
  Require(!MatchesPoll(frame,1,3,0),"reject old sequence");
  Require(!MatchesPoll(frame,3,2,0),"reject previous receiver session");
  Require(FreshFrame({.5f,0,0,0},true,1099,1000,true).low==.5f,"fresh source retained");
  Require(!HapticNonzero(FreshFrame({.5f,0,0,0},true,1100,1000,true)),"expired source silenced");
  Require(!HapticNonzero(FreshFrame({.5f,0,0,0},false,1001,1000,true)),"focus or gain loss silences source");
  Require(!HapticNonzero(FreshFrame({.5f,0,0,0},true,1001,1000,false)),"unknown source silenced");
  RoutePolicy routes;
  routes.Report(1,true,1000);
  Require(routes.Selected()==1&&routes.Owns(1,1000),"first ready route acquired automatically");
  routes.Report(2,true,1001);
  Require(routes.Selected()==2&&!routes.Owns(1,1001)&&!routes.Owns(2,1200),"handover silences both old and new routes");
  Require(routes.Owns(2,1201),"new route starts after finite old pulse expires");
  Require(!routes.Owns(1,1201),"standby cannot emit after another route completes handover");
  routes.Report(2,true,1201); routes.Report(3,true,1210);
  Require(routes.Selected()==3&&!routes.Owns(2,1210)&&routes.Owns(3,1410),"focused OpenXR is preferred to duplicate Meta route");
  const auto generation=routes.Generation();
  routes.Report(1,true,1300);
  Require(routes.Selected()==3&&routes.Generation()==generation,"standby route does not steal active output");
  RoutePolicy fallback;
  fallback.Report(1,true,1000); fallback.Report(3,true,1001); fallback.Report(1,true,1249);
  fallback.Refresh(1251);
  Require(fallback.Selected()==1&&!fallback.Owns(1,1251),"expired preferred routes fall back automatically");
  fallback.Report(1,true,1450);
  Require(!fallback.Owns(1,1450)&&fallback.Owns(1,1451),"fallback acquires output after drain");
  fallback.Refresh(1700);
  Require(fallback.Selected()==0&&!fallback.Owns(1,1700),"lost companion lease disables output at boundary");
  fallback.Report(3,false,1701);
  Require(fallback.Selected()==0,"unfocused session cannot acquire output");
  RoutePolicy native_loader;
  native_loader.Report(3,true,1000); native_loader.Report(4,true,1001);
  Require(native_loader.Selected()==4&&!native_loader.Owns(3,1201)&&native_loader.Owns(4,1201),"nested native and Windows layers never both own output");
  RoutePolicy native;
  native.Report(2,true,1000); native.NoteNonzero(2);
  native.Report(3,true,1010); native.Report(3,true,1300);
  Require(!native.Owns(3,1300),"Meta output must acknowledge stop before handover");
  native.AcknowledgeStop(1,1300);
  Require(!native.Owns(3,1300),"another backend cannot acknowledge Meta stop");
  native.AcknowledgeStop(2,1300);
  Require(!native.Owns(3,1499)&&native.Owns(3,1500),"Meta stop acknowledgment starts finite drain");
  RoutePolicy lost_native;
  lost_native.Report(2,true,1000); lost_native.NoteNonzero(2);
  lost_native.Report(3,true,1010); lost_native.Report(3,true,3509);
  Require(!lost_native.Owns(3,3509),"unacknowledged Meta output waits for SDK expiry");
  lost_native.Report(3,true,3510);
  Require(lost_native.Owns(3,3510),"SDK expiry allows automatic fallback without stale Meta lease");
  std::cout<<"PASS: hand translation, independent wire format, invalid/stale packets and source expiry\n";
}
