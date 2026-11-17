// Reuse the existing behavior harness: the only additional mock is the VR
// transport seam, never the mixer, ownership classifier, or event policy.
#define wmain ExistingOutputVerification
#include "xinput_output.cpp"
#undef wmain
#include "edtrace/vr_output.hpp"

namespace {
edtrace::HapticFrame mirrored{};
bool mixed=false,turret=false,missile=false;
}
namespace edtrace {
void VrOutputPublish(HapticFrame f) noexcept {
  mirrored=f;
  if(f.low==.49999237f&&f.high==.8288853f&&f.left==.25f&&f.right==.5f) mixed=true;
  if(f.left==0&&f.right==.13f) turret=true;
  if(f.left==.3f&&f.right==0) missile=true;
}
VrOutputStats VrOutputGetStats() noexcept { return {ERROR_SUCCESS,true,0,0,0}; }
}
int wmain(int argc,wchar_t** argv) {
  const int result=ExistingOutputVerification(argc,argv);
  Check(mixed,"VR receives authored and supplemental mixed main/trigger channels");
  Check(turret&&missile,"VR receives distinct turret and missile hand channels");
  edtrace::XInputOutputSetOnFootFrame({},false);
  edtrace::XInputOutputSetReady(true,true);
  edtrace::XInputOutputSetOnFootFrame({0,0,0,.75f},true);
  Check(mirrored.low==0&&mirrored.high==0&&mirrored.left==0&&mirrored.right==.75f,
        "trigger-only VR output survives a two-motor fallback");
  edtrace::XInputOutputSetOnFootFrame({},false);
  Check(!edtrace::HapticNonzero(mirrored),"VR cancellation sends zero");
  return result;
}
