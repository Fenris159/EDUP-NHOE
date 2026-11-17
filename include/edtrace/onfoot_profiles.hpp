#pragma once
#include "edtrace/haptic_frame.hpp"
#include <array>
#include <cstdint>
namespace edtrace {
// Added weapon feedback patterns and their native event routes.
struct OnFootWeaponPattern { HapticFrame onset, sustain; std::uint32_t duration; bool held; };
struct OnFootWeaponRoute { std::uint32_t id; std::uint8_t pattern; bool start; };
inline constexpr std::array<OnFootWeaponPattern, 12> kOnFootWeapons{{
  {{0.12f, 0.18f, 0.00f, 0.65f}, {0.03f, 0.08f, 0.00f, 0.30f}, 90, true}, // assaultrifle_kinetic
  {{0.05f, 0.16f, 0.00f, 0.42f}, {0.01f, 0.10f, 0.00f, 0.25f}, 100, true}, // assaultrifle_laser
  {{0.20f, 0.12f, 0.00f, 0.60f}, {0.10f, 0.05f, 0.00f, 0.34f}, 140, true}, // assaultrifle_plasma
  {{0.60f, 0.25f, 0.10f, 0.98f}, {0.00f, 0.00f, 0.00f, 0.00f}, 360, false}, // h_launcher_rocket
  {{0.50f, 0.24f, 0.08f, 0.95f}, {0.00f, 0.00f, 0.00f, 0.00f}, 320, false}, // m_launcher_rocket
  {{0.12f, 0.30f, 0.00f, 0.68f}, {0.00f, 0.00f, 0.00f, 0.00f}, 120, false}, // pistol_kinetic
  {{0.04f, 0.16f, 0.00f, 0.45f}, {0.00f, 0.00f, 0.00f, 0.00f}, 100, false}, // pistol_laser
  {{0.26f, 0.13f, 0.00f, 0.75f}, {0.00f, 0.00f, 0.00f, 0.00f}, 180, false}, // pistol_plasma
  {{0.45f, 0.24f, 0.05f, 0.90f}, {0.00f, 0.00f, 0.00f, 0.00f}, 220, false}, // shotgun_plasma_doublebarrel
  {{0.34f, 0.18f, 0.00f, 0.86f}, {0.00f, 0.00f, 0.00f, 0.00f}, 240, false}, // sniper_plasma_charged
  {{0.07f, 0.20f, 0.00f, 0.48f}, {0.02f, 0.12f, 0.00f, 0.24f}, 65, true}, // submachinegun_kinetic
  {{0.03f, 0.17f, 0.00f, 0.38f}, {0.00f, 0.09f, 0.00f, 0.20f}, 80, true}, // submachinegun_laser
}};
inline constexpr std::array<OnFootWeaponRoute, 51> kOnFootWeaponRoutes{{
  {77079964u, 1, false},
  {238052904u, 3, true},
  {238052905u, 3, true},
  {451507206u, 5, true},
  {451507207u, 5, true},
  {579494030u, 10, false},
  {579494031u, 10, false},
  {606994818u, 2, true},
  {878784778u, 6, true},
  {878784779u, 6, true},
  {1020436630u, 10, true},
  {1020436631u, 10, true},
  {1050844157u, 10, true},
  {1053920138u, 11, false},
  {1119934880u, 0, false},
  {1119934881u, 0, false},
  {1194474201u, 6, true},
  {1335403906u, 7, true},
  {1389563818u, 2, false},
  {1389563819u, 2, false},
  {1492093878u, 9, true},
  {1492093879u, 9, true},
  {1697380578u, 0, true},
  {1697380579u, 0, true},
  {2117087806u, 4, true},
  {2117087807u, 4, true},
  {2173789774u, 7, true},
  {2173789775u, 7, true},
  {2256903808u, 0, false},
  {2358356665u, 2, false},
  {2687056681u, 1, true},
  {2695090333u, 11, true},
  {2739481221u, 4, true},
  {2780503128u, 8, true},
  {2780503129u, 8, true},
  {2946414244u, 1, false},
  {2946414245u, 1, false},
  {3018713410u, 10, false},
  {3390963608u, 8, true},
  {3597391112u, 3, true},
  {3630856506u, 1, true},
  {3630856507u, 1, true},
  {3755637326u, 2, true},
  {3755637327u, 2, true},
  {3756884017u, 0, true},
  {3772557389u, 5, true},
  {3918225526u, 11, true},
  {3918225527u, 11, true},
  {3992912010u, 9, true},
  {4208749750u, 11, false},
  {4208749751u, 11, false},
}};
}  // namespace edtrace
