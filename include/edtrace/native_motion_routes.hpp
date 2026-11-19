#pragma once
#include <array>
#include <cstdint>
#include <ostream>

namespace edtrace {
// Diagnostic associations for native-dispatched effects. This table never
// posts an event, changes a gain, selects an owner or controls a motor.
struct NativeMotionRoute {
  std::uint32_t event;
  const char* name;
  const char* phase;
  std::array<std::uint32_t, 2> play;
  std::uint32_t stop;
  const char* bank_context = nullptr;
  std::uint8_t channels = 3;
};
inline constexpr std::array<NativeMotionRoute, 27> kNativeMotionRoutes{{
  {325819992u,nullptr,"play",{{689694108u,786260738u}},0,"federation_corvette",3},
  {685855539u,"ShipMetal_s_Impact","one_shot",{{192245096u,0}},0},
  {972714317u,"ShipCockpit_bs_Friction","begin",{{426995427u,0}},0},
  {998620921u,"ShipCockpit_bs_Collision","one_shot",{{869039904u,0}},0},
  {1052522342u,"ShipCockpit_es_Impact","end",{{1042650795u,0}},265834928u},
  {1357267772u,"ShipMetal_es_Impact","end",{{15548333u,0}},942997056u},
  {1419391917u,"PhysicsShip_s_Impact","one_shot",{{192245096u,0}},0},
  {1783476917u,"ShipCockpit_es_Impact_multibeam","end",{{1042650795u,0}},265834928u},
  {1851848329u,"ShipCockpit_s_Impact","one_shot",{{553243862u,0}},0},
  {1940740227u,"ShipMetal_es_Impact_multibeam","end",{{15548333u,0}},942997056u},
  {2040158284u,"ShipMetal_bs_Impact_multibeam","begin",{{478680721u,942997056u}},0},
  {2186794399u,"ShipMetal_bs_Friction","begin",{{836002623u,0}},0},
  {2315646605u,"PhysicsShip_bs_Impact","begin",{{478680721u,942997056u}},0},
  {2373023892u,"ShipMetal_es_Friction","end",{{0,0}},836002623u},
  {2571248154u,"PhysicsShip_es_Impact","end",{{15548333u,0}},942997056u},
  {2632285979u,"ShipMetal_bs_Impact","begin",{{478680721u,942997056u}},0},
  {2641317977u,"PhysicsShip_bs_Friction","begin",{{591148932u,0}},0},
  {2704045990u,"ShipCockpit_es_Friction","end",{{0,0}},426995427u},
  {2879602485u,"PhysicsShip_bs_Collision","one_shot",{{886794084u,0}},0},
  {2854814051u,nullptr,"play",{{60810340u,0}},0,"federation_corvette",1},
  {3007528978u,"ShipCockpit_bs_Impact_multibeam","begin",{{265834928u,270767332u}},0},
  {3166798545u,"PhysicsShip_es_Impact_multibeam","end",{{15548333u,0}},942997056u},
  {3434299034u,"PhysicsShip_es_Friction","end",{{0,0}},591148932u},
  {3557926761u,nullptr,"stop",{{0,0}},786260738u,"federation_corvette",3},
  {3597983935u,"ShipMetal_bs_Collision","one_shot",{{743280658u,0}},0},
  {3655933990u,"PhysicsShip_bs_Impact_multibeam","begin",{{478680721u,942997056u}},0},
  {4090524289u,"ShipCockpit_bs_Impact","begin",{{265834928u,270767332u}},0},
}};

inline const NativeMotionRoute* FindNativeMotionRoute(std::uint32_t event) noexcept {
  for (const auto& route : kNativeMotionRoutes) if (route.event == event) return &route;
  return nullptr;
}

inline void WriteNativeMotionRoute(std::ostream& output, std::uint32_t event) {
  const auto* route = FindNativeMotionRoute(event);
  if (!route) { output << "null"; return; }
  output << "{\"name\":";
  if (route->name) output << '"' << route->name << '"'; else output << "null";
  output << ",\"phase\":\"" << route->phase << "\",\"play_source_ids\":[";
  bool first = true;
  for (const auto source : route->play) if (source) {
    if (!first) output << ',';
    output << source; first = false;
  }
  output << "],\"stop_source_ids\":[";
  if (route->stop) output << route->stop;
  output << "],\"active_channels\":[";
  if (route->channels & 1) output << '0';
  if (route->channels & 2) { if (route->channels & 1) output << ','; output << '1'; }
  output << "],\"source_execution_observed\":false,\"ownership\":\"unknown\"";
  if (route->bank_context) output << ",\"bank_context\":\"" << route->bank_context << "\",\"gameplay_role\":\"unresolved\"";
  output << '}';
}

inline const char* ObservedTraceParameterName(std::uint32_t parameter) noexcept {
  switch (parameter) {
    case 1497268741u: return "attachedToPlayer";
    case 103566125u: return "PlayerInsideAndEmitterAttached";
    case 2410709172u: return "Humanoid_speed";
    case 1326349511u: return "shield_strength_self";
    case 2581359525u: return "hull_strength_self";
    case 1951144523u: return "collision_velocity";
    case 4274346379u: return "friction_velocity";
    default: return nullptr;
  }
}
}  // namespace edtrace
