#include "edtrace/onfoot_haptics.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace edtrace;
namespace {
void Check(bool condition, const char* name) { if (!condition) { std::fprintf(stderr,"FAIL %s\n",name); std::exit(1); } }
bool Same(float first, float second) { return std::fabs(first-second) < .000001f; }
void Frame(HapticFrame actual, HapticFrame expected, const char* name) {
  Check(Same(actual.low,expected.low) && Same(actual.high,expected.high) && Same(actual.left,expected.left) && Same(actual.right,expected.right),name);
}
void Registered(OnFootHaptics& engine, std::uint64_t object) {
  engine.Accept({OnFootInputKind::Register,object,0,0,0,0,1,0});
}
void Parameter(OnFootHaptics& engine, std::uint64_t object, std::uint32_t id, float value, std::uint64_t now=0, int result=1) {
  engine.Accept({OnFootInputKind::Parameter,object,now,id,0,0,result,value});
}
void Local(OnFootHaptics& engine, std::uint64_t object, bool first=true) {
  Registered(engine,object);
  Parameter(engine,object,1497268741,1);
  Parameter(engine,object,103566125,1);
  if (first) engine.Accept({OnFootInputKind::Switch,object,0,1979902012,998496889,0,1,0});
}
void Post(OnFootHaptics& engine, std::uint64_t object, std::uint32_t id, std::uint64_t now, std::uint32_t playing=1) {
  engine.Accept({OnFootInputKind::Post,object,now,id,0,playing,0,0});
}
}
int main() {
  {
    OnFootHaptics engine; engine.SetActive(true);
    Post(engine,7,1697380579,1000); Frame(engine.Sample(1000),{},"unknown emitter stays quiet");
    Registered(engine,7); Post(engine,7,1697380579,1000); Frame(engine.Sample(1000),{},"defaults do not prove ownership");
    Parameter(engine,7,1497268741,1,0,0); Parameter(engine,7,103566125,1);
    engine.Accept({OnFootInputKind::Switch,7,0,1979902012,998496889,0,1,0});
    Post(engine,7,1697380579,1000); Frame(engine.Sample(1000),{},"failed attachment update stays untrusted");
    Local(engine,7,false); Post(engine,7,1697380579,1000); Frame(engine.Sample(1000),{},"unobserved first person stays quiet");
    Local(engine,7); Parameter(engine,7,1497268741,0);
    Post(engine,7,1697380579,1000); Frame(engine.Sample(1000),{},"explicit unattached emitter stays quiet");
    Local(engine,7); Post(engine,7,1697380579,1000,0); Frame(engine.Sample(1000),{},"failed post creates no effect");
  }
  {
    OnFootHaptics engine; Local(engine,7); engine.SetActive(true);
    Post(engine,7,1697380579,1000); Frame(engine.Sample(1000),{.12f,.18f,0,.65f},"rifle onset reaches intended motors");
    Frame(engine.Sample(1100),{.03f,.08f,0,.30f},"automatic fire sustains after onset");
    Post(engine,7,1119934880,1100); Frame(engine.Sample(1100),{},"native release stops sustained fire");
    Post(engine,7,1697380579,2000); Frame(engine.Sample(3499),{.03f,.08f,0,.30f},"held fire remains before watchdog boundary");
    Frame(engine.Sample(3500),{},"missing native release cannot hold forever");
    Post(engine,7,1697380579,4000); Parameter(engine,7,3661999171,1.2f,5000);
    Frame(engine.Sample(5499),{.03f,.08f,0,.30f},"native duration update renews a held effect");
    Frame(engine.Sample(5500),{},"stale native duration expires exactly");
  }
  {
    OnFootHaptics engine; Local(engine,7); engine.SetActive(true);
    Post(engine,7,451507206,1000); Frame(engine.Sample(1060),{.06f,.15f,0,.34f},"pistol impulse has a finite decay");
    Frame(engine.Sample(1120),{},"pistol impulse ends at its boundary");
    Post(engine,7,1697380579,2000); engine.SetActive(false); Frame(engine.Sample(2000),{},"focus or mode loss clears all output");
    engine.SetActive(true); Frame(engine.Sample(2010),{},"focus regain does not replay old fire");
    Post(engine,7,1697380579,3000); Registered(engine,7); Frame(engine.Sample(3010),{},"reused object loses old effects");
    Post(engine,7,1697380579,3100); Frame(engine.Sample(3100),{},"reused object needs new ownership evidence");
    Local(engine,7); Post(engine,7,1697380579,4000);
    engine.Accept({OnFootInputKind::Unregister,7,4010,0,0,0,1,0}); Frame(engine.Sample(4010),{},"unregister clears effects immediately");
  }
  {
    OnFootHaptics engine; Local(engine,7); Local(engine,8); engine.SetActive(true);
    Post(engine,7,3753075143,1000); Post(engine,8,3753075161,1015);
    Frame(engine.Sample(1075),{.15f,.08f,0,0},"paired landing creates one decay instead of restarting twice");
    Frame(engine.Sample(1150),{},"landing pulse ends normally");
  }
  {
    OnFootHaptics engine; Local(engine,7); Local(engine,8); engine.SetActive(true);
    Post(engine,7,63371630,1000); Frame(engine.Sample(1000),{},"unknown gait never enables walking vibration");
    Parameter(engine,7,2410709172,1.5f,1100);
    Post(engine,7,63371632,1100); Frame(engine.Sample(1100),{},"walking vibration is zero");
    Parameter(engine,7,2410709172,2.0f,1200);
    Post(engine,7,2344147335,1200); Frame(engine.Sample(1200),{},"walking boundary stays silent");
    Parameter(engine,8,2410709172,6.0f,1300);
    Post(engine,7,2344147353,1300); Frame(engine.Sample(1300),{},"another emitter cannot enable the local gait");
    Parameter(engine,7,2410709172,3.0f,1400,0);
    Post(engine,7,63371630,1400); Frame(engine.Sample(1400),{},"failed speed requests cannot enable running");
    Parameter(engine,7,2410709172,3.0f,1500);
    Post(engine,7,63371630,1500); Frame(engine.Sample(1500),{.08f,.06f,0,0},"running retains its contact pulse");
    Frame(engine.Sample(1555),{},"running contact ends at 55 ms");
    Parameter(engine,7,2410709172,6.0f,1600);
    Post(engine,7,2344147353,1600); Frame(engine.Sample(1600),{.08f,.06f,0,0},"sprinting retains its contact pulse");
    Post(engine,7,63371632,3000); Frame(engine.Sample(3000),{.08f,.06f,0,0},"applied native speed persists until changed");
    Parameter(engine,7,2410709172,1.5f,3100);
    Post(engine,7,63371630,3100); Frame(engine.Sample(3100),{},"returning to walking disables new contacts");
    Parameter(engine,7,2410709172,-1.0f,3200);
    Post(engine,7,63371630,3200); Frame(engine.Sample(3200),{},"negative movement metadata is silent");
    Parameter(engine,7,2410709172,6.0f,3300); engine.SetActive(false); engine.SetActive(true);
    Post(engine,7,63371630,3300); Frame(engine.Sample(3300),{},"focus regain needs a new native speed update");
    engine.SetActive(false); Parameter(engine,7,2410709172,6.0f,3350); engine.SetActive(true);
    Post(engine,7,63371630,3350); Frame(engine.Sample(3350),{},"inactive speed requests cannot restore an old running state");
    Parameter(engine,7,2410709172,6.0f,3400); Parameter(engine,7,103566125,0,3400);
    Parameter(engine,7,103566125,1,3500);
    Post(engine,7,63371630,3500); Frame(engine.Sample(3500),{},"ownership regain does not reuse an old running state");
    Parameter(engine,7,2410709172,6.0f,3600); Local(engine,7);
    Post(engine,7,63371630,3600); Frame(engine.Sample(3600),{},"reused emitter loses its old gait");
  }
  {
    OnFootHaptics engine; Local(engine,7,false); engine.SetActive(true);
    Post(engine,7,3666194431,900); Frame(engine.Sample(900),{},"orphan jetpack cutoff is silent");
    Post(engine,7,3666194424,1000); Frame(engine.Sample(1000),{},"jetpack begins at zero");
    Frame(engine.Sample(1012),{.60f,.40f,0,0},"jetpack ignition reaches its designed main-motor kick");
    Frame(engine.Sample(1065),{.45f,.28f,0,0},"ignition settles into thrust");
    Frame(engine.Sample(1090),{.30f,.16f,0,0},"jetpack sustain needs no invented third event");
    Post(engine,7,3666194424,2000); Frame(engine.Sample(2012),{.30f,.16f,0,0},"repeated jetpack start does not re-kick or stack");
    Local(engine,8,false); Post(engine,8,3666194431,3000);
    Frame(engine.Sample(3000),{.30f,.16f,0,0},"another emitter cannot stop local thrust");
    Post(engine,7,3666194431,5000); Frame(engine.Sample(5050),{.15f,.08f,0,0},"native cutoff fades current jetpack contribution");
    Frame(engine.Sample(5100),{},"cutoff reaches silence at exactly 100 ms");
    Post(engine,7,4217999457,5200); Post(engine,7,1053929888,5300);
    Frame(engine.Sample(5300),{},"recharge UI never produces propulsion rumble");
  }
  {
    OnFootHaptics engine; Local(engine,7,false); engine.SetActive(true);
    Post(engine,7,3666194424,1000); Post(engine,7,3666194424,10950);
    Frame(engine.Sample(10999),{.30f,.16f,0,0},"jetpack remains active until its missing-stop bound");
    Frame(engine.Sample(11000),{},"missing jetpack stop clears at ten seconds without restarting");
    Post(engine,7,3666194431,11001); Frame(engine.Sample(11001),{},"late cutoff after watchdog stays silent");
    Post(engine,7,3666194424,12000); engine.SetActive(false); engine.SetActive(true);
    Frame(engine.Sample(12012),{},"focus cancellation does not resume an old jetpack hold");
    Post(engine,7,3666194424,13000); Parameter(engine,7,103566125,0,13020);
    Frame(engine.Sample(13020),{},"ownership loss cancels jetpack immediately");
    Local(engine,7,false); Post(engine,7,3666194424,14000);
    engine.Accept({OnFootInputKind::Unregister,7,14020,0,0,0,1,0});
    Frame(engine.Sample(14020),{},"jetpack stops on emitter destruction");
    Local(engine,9,false); Parameter(engine,9,1497268741,0);
    Post(engine,9,3666194424,15000); Frame(engine.Sample(15012),{},"nonlocal jetpack stays silent");
    Local(engine,9,false); Post(engine,9,3666194424,16000,0);
    Frame(engine.Sample(16012),{},"failed jetpack post stays silent");
  }
  {
    struct Expected { unsigned id; float right; };
    const Expected samples[] = {{1697380579,.65f},{3630856506,.42f},{3755637326,.60f},{238052904,.98f},
      {2117087806,.95f},{451507206,.68f},{878784778,.45f},{2173789774,.75f},
      {2780503128,.90f},{1492093878,.86f},{1020436630,.48f},{3918225526,.38f}};
    for (const auto& item : samples) {
      OnFootHaptics engine; Local(engine,7); engine.SetActive(true); Post(engine,7,item.id,1000);
      Check(Same(engine.Sample(1000).right,item.right),"each weapon family has its own intended trigger strength");
    }
  }
  {
    OnFootHaptics engine; Registered(engine,7); Parameter(engine,7,1497268741,1); engine.SetActive(true);
    Post(engine,7,476288594,900); Frame(engine.Sample(900),{},"vehicle perspective must be explicitly observed");
    engine.Accept({OnFootInputKind::Switch,7,950,1979902012,931160808,0,1,0});
    Post(engine,7,476288594,1000); Frame(engine.Sample(1000),{.04f,.07f,0,.26f},"mounted turret has light body and right-trigger feedback without on-foot inside marker");
    Frame(engine.Sample(1030),{.02f,.035f,0,.13f},"turret pulse follows each actual shot with finite decay");
    Frame(engine.Sample(1060),{},"turret release needs no synthetic held-fire loop");
    Parameter(engine,7,103566125,0,1100); Post(engine,7,313889439,1200);
    Frame(engine.Sample(1200),{},"missile ejection begins with a ramp instead of an instantaneous impact");
    Frame(engine.Sample(1225),{.40f,.12f,.60f,0},"missile launch pushes through body and left trigger");
    Frame(engine.Sample(1260),{.40f,.12f,.60f,0},"missile ejection plateau lasts through its push");
    Frame(engine.Sample(1340),{.20f,.06f,.30f,0},"missile push has a longer controlled release");
    Frame(engine.Sample(1420),{},"missile launch finishes at 220 ms");
    for (unsigned id : {1591166666u,922802779u,1252404837u,1429625105u,1207267404u,1393800649u}) Post(engine,7,id,1500);
    Frame(engine.Sample(1500),{},"projectile flight, lock sounds, startup and mechanical spool do not impersonate a launch or shot");
    Post(engine,7,476288594,1600); Post(engine,7,313889439,1600);
    Frame(engine.Sample(1625),{.40f,.12f,.60f,.15166667f},"simultaneous turret and missile preserve separate trigger contributions");
    Parameter(engine,7,1497268741,0,1630); Frame(engine.Sample(1630),{},"detaching a vehicle weapon cancels its feedback");
    Post(engine,7,476288594,1640); Frame(engine.Sample(1640),{},"a nonlocal mounted weapon cannot start trigger feedback");
    Parameter(engine,7,1497268741,1,1700); Post(engine,7,476288594,1700,0);
    Frame(engine.Sample(1700),{},"failed mounted-weapon post stays silent");
    engine.Accept({OnFootInputKind::Switch,7,1800,1979902012,998496889,0,1,0});
    Post(engine,7,476288594,1800); Frame(engine.Sample(1800),{},"handheld first-person state cannot qualify a mounted-weapon cue");
    Post(engine,7,1697380579,1900); Frame(engine.Sample(1900),{},"vehicle eligibility does not weaken on-foot inside ownership");
  }
  Frame(HapticMaximum({.3f,.1f,.7f,0},{.2f,.4f,0,.8f}),{.3f,.4f,.7f,.8f},"new effects preserve stronger authored channels");
  Frame(HapticScale({std::numeric_limits<float>::quiet_NaN(),2,-1,.5f},.5f),{0,.5f,0,.25f},"invalid amplitudes cannot escape the unit range");
  std::puts("PASS on-foot actor gates, weapon families, native stops, finite impulses, focus and channel mixing");
}
