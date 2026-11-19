#include "edtrace/sco_haptics.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
using edtrace::HapticFrame;
void Check(bool value, const char* description) {
  if (!value) { std::fprintf(stderr, "FAIL: %s\n", description); std::exit(1); }
}
void Frame(HapticFrame value, float low, float high, const char* description) {
  Check(std::fabs(value.low - low) < .000001f && std::fabs(value.high - high) < .000001f &&
        value.left == 0 && value.right == 0, description);
}
}

int main() {
  using edtrace::ParseScoStatus;
  const auto off = ParseScoStatus(R"({"timestamp":"2026-11-15T19:25:00Z","event":"Status","Flags":16777240,"Flags2":0,"Fuel":{"FuelMain":-0.2e-1},"Pips":[4,4,4]})");
  const auto on = ParseScoStatus(R"({"event":"Status","Flags2":1048576,"Flags":16777240})");
  const auto jump = ParseScoStatus(R"({"event":"Status","Flags":1090519064,"Flags2":0})");
  const auto normal = ParseScoStatus(R"({"event":"Status","Flags":16777216,"Flags2":0})");
  Check(off && on && jump && normal, "complete flight snapshots parse");
  Check(!ParseScoStatus(R"({"event":"Status","Flags":16777240,"Flags2":1048576,"Fuel":{)"), "a partial write produces no state");
  Check(!ParseScoStatus(R"({"event":"Status","Flags":16777240,"Fuel":{"Flags2":1048576}})"), "nested Flags2 cannot impersonate the player's state");
  Check(!ParseScoStatus(R"({"event":"Status","Flags":16777240,"Flags2":0,"Flags2":1048576})"), "conflicting status flags are rejected");
  Check(!ParseScoStatus(R"({"event":"Status","Flags":16777240,"Flags2":18446744073709551616})"), "overflow is rejected");
  const auto escaped = ParseScoStatus(R"({"event":"\u0053tatus","Flags":16777240,"\u0046lags2":1048576})");
  Check(escaped && escaped->in_supercruise && escaped->overcharged, "escaped JSON keys and values have the same meaning");

  edtrace::ScoHaptics sco;
  sco.Observe(*on, 1000);
  Frame(sco.Sample(1020),0,0,"loading into active SCO initializes silently");
  sco.Reset();
  sco.Observe(*normal, 1100);
  sco.Observe(*off, 1200);
  Frame(sco.Sample(1220),0,0,"ordinary supercruise entry is quiet");
  sco.Observe(*on, 1300);
  const auto first_peak = sco.Sample(1330);
  const auto dip = sco.Sample(1390);
  const auto second_peak = sco.Sample(1450);
  Check(first_peak.low > .80f && dip.low < .6f && second_peak.low > dip.low + .25f,
        "engagement begins with distinct uneven jolts");
  const auto settling = sco.Sample(3400);
  Check(settling.low > 0 && settling.low < .20f && settling.high < .15f,
        "the ship still shakes gently after two seconds");
  Frame(sco.Sample(3800),0,0,"engagement settles at two and a half seconds");
  sco.Observe(*on, 3900);
  Frame(sco.Sample(3920),0,0,"repeated active snapshots cannot restart the surge");
  sco.Observe(*off, 4000);
  const auto slowdown_start = sco.Sample(4180);
  const auto slowdown_middle = sco.Sample(5150);
  const auto slowdown_end = sco.Sample(6300);
  Check(slowdown_start.low > slowdown_middle.low && slowdown_middle.low > slowdown_end.low &&
        slowdown_end.low > 0 && slowdown_start.high < first_peak.high,
        "disengagement feels like a gentle slowdown sustained for over two seconds");
  Frame(sco.Sample(6500),0,0,"the slowdown ends at two and a half seconds");
  sco.Observe(*on, 6600);
  sco.Observe(*jump, 6620);
  Frame(sco.Sample(6624),0,0,"a jump cancels the active transition");
  sco.Observe(*off, 6700);
  sco.Observe(*on, 6800);
  sco.Reset();
  sco.Observe(*off, 6900);
  Frame(sco.Sample(6920),0,0,"readiness loss cancels and regain cannot replay an edge");
}
