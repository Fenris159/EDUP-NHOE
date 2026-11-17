#include "../vr/native_xr_relay.hpp"
#include "edtrace/vr_haptics.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace {
std::atomic<std::uint64_t> clock_value{1000};
std::mutex mutex;
std::condition_variable changed;
std::uint64_t completed=0,entered=0,permits=0,last_clock=0;
bool blocking=true,last_armed=false,owned=true;
void Check(bool value,const char* message) {
  if(!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::fflush(stderr); std::_Exit(1); }
}
template<class Predicate> void Wait(Predicate predicate) {
  std::unique_lock<std::mutex> lock(mutex);
  Check(changed.wait_for(lock,std::chrono::seconds(5),predicate),"owned worker synchronization completed");
}
void Permit() { std::lock_guard<std::mutex> lock(mutex); ++permits; changed.notify_all(); }
std::uint64_t Completed() { std::lock_guard<std::mutex> lock(mutex); return completed; }
void Cycle() { const auto before=Completed(); Permit(); Wait([&]{return completed>before;}); }
bool Read(bool ready,edtrace::VrBridgeFrame& output) {
  output={}; return edtrace::NativeXrRelayRead(4,ready?1:0,&output)!=0;
}
}
extern "C" std::uint64_t EDNativeXrFixtureClock() noexcept { return clock_value.load(); }
extern "C" bool EDNativeXrFixturePoll(std::uint64_t nonce,std::uint64_t sequence,bool armed,edtrace::vrbridge::Message& reply,edtrace::vrbridge::Kind kind) {
  Check(nonce==0x12345678&&sequence!=0&&kind==edtrace::vrbridge::Kind::NativeOpenXRPoll,"worker requests only native OpenXR route");
  std::unique_lock<std::mutex> lock(mutex);
  ++entered; last_armed=armed; last_clock=clock_value.load(); changed.notify_all();
  changed.wait(lock,[]{return !blocking||permits>0;});
  if(blocking) --permits;
  reply={edtrace::vrbridge::Kind::Frame,armed&&owned,nonce,sequence,armed&&owned?edtrace::HapticFrame{.25f,.5f,.75f,1}:edtrace::HapticFrame{}};
  return true;
}
extern "C" void EDNativeXrFixtureCycleDone() noexcept {
  std::lock_guard<std::mutex> lock(mutex); ++completed; changed.notify_all();
}
int main() {
  using namespace edtrace;
  Check(NativeXrRelayStart(),"native output worker starts independently of render reads");
  Wait([]{return entered>=1;});
  VrBridgeFrame output{};
  Check(!Read(true,output),"render read returns immediately while network boundary is blocked");
  Cycle();
  Wait([]{return entered>=2;});
  Cycle();
  Check(Read(true,output)&&output.frame.low==.25f&&output.frame.high==.5f&&output.frame.left==.75f&&output.frame.right==1,
        "owned worker response reaches nonblocking render cache");
  Wait([]{return entered>=3;});
  Check(!Read(false,output)&&!HapticNonzero(output.frame),"focus loss immediately silences local read");
  Check(!Read(true,output),"immediate focus recovery cannot replay prior readiness epoch");
  Cycle();
  Check(!Read(true,output),"in-flight old epoch response is discarded");
  Cycle();
  Check(Read(true,output),"fresh post-recovery response restores output automatically");
  // No render read renews the fake-clock heartbeat in this interval.
  clock_value.store(1100);
  Cycle(); Cycle();
  { std::lock_guard<std::mutex> lock(mutex);
    Check(last_clock==1100&&!last_armed,"render heartbeat expiration reaches network readiness"); }
  Check(!Read(true,output),"render stall disarms worker and cached ownership");
  Cycle(); Cycle();
  Check(Read(true,output),"fresh render heartbeat automatically reacquires output");
  {
    std::lock_guard<std::mutex> lock(mutex); owned=false;
  }
  Cycle(); Cycle();
  Check(!Read(true,output),"standby reply never sends zeros as owned output");
  {
    std::lock_guard<std::mutex> lock(mutex); blocking=false; changed.notify_all();
  }
  NativeXrRelayStop();
  Check(!Read(true,output),"stopped worker cannot replay cached output");
  std::puts("PASS native OpenXR worker: nonblocking render reads, four channels, stale epoch rejection, heartbeat expiry and reacquisition");
}
