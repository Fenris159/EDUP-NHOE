#include "edtrace/native_motion_routes.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

namespace {
void Check(bool condition, const char* message) {
  if (!condition) { std::fprintf(stderr,"FAIL native Motion metadata: %s\n",message); std::exit(1); }
}
std::string Describe(std::uint32_t event) {
  std::ostringstream output; edtrace::WriteNativeMotionRoute(output,event); return output.str();
}
}
int main() {
  const auto collision = Describe(2879602485u);
  Check(collision == "{\"name\":\"PhysicsShip_bs_Collision\",\"phase\":\"one_shot\",\"play_source_ids\":[886794084],\"stop_source_ids\":[],\"active_channels\":[0,1],\"source_execution_observed\":false,\"ownership\":\"unknown\"}",
        "collision remains a one-shot despite its bs token, without claiming execution or ownership");
  const auto end = Describe(1357267772u);
  Check(end == "{\"name\":\"ShipMetal_es_Impact\",\"phase\":\"end\",\"play_source_ids\":[15548333],\"stop_source_ids\":[942997056],\"active_channels\":[0,1],\"source_execution_observed\":false,\"ownership\":\"unknown\"}",
        "continuous impact end preserves its authored tail and sustained-source stop");
  Check(Describe(285258775u) == "null" && Describe(1482539131u) == "null" && Describe(0) == "null",
        "shield audio-only and unknown events never acquire a native Motion association");
  Check(Describe(3434299034u).find("\"play_source_ids\":[],\"stop_source_ids\":[591148932]") != std::string::npos,
        "friction end does not acquire a synthetic play source");
  Check(Describe(2854814051u) == "{\"name\":null,\"phase\":\"play\",\"play_source_ids\":[60810340],\"stop_source_ids\":[],\"active_channels\":[0],\"source_execution_observed\":false,\"ownership\":\"unknown\",\"bank_context\":\"federation_corvette\",\"gameplay_role\":\"unresolved\"}",
        "an unnamed Corvette route retains its single active channel and does not acquire a gameplay name");
  Check(Describe(3557926761u).find("\"play_source_ids\":[],\"stop_source_ids\":[786260738]") != std::string::npos,
        "the additional Corvette stop remains a stop rather than replaying a pattern");
  const char* shield = edtrace::ObservedTraceParameterName(1326349511u);
  Check(shield && std::strcmp(shield,"shield_strength_self") == 0 && !edtrace::ObservedTraceParameterName(123456789u),
        "named self-shield capture remains an explicit allowlist");
  std::puts("PASS native route metadata, stop/tail semantics, unknown ownership and RTPC allowlist");
}
