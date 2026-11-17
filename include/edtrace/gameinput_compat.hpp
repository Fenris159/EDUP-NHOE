#pragma once
#include <windows.h>
#include <array>
#include <cstdint>

namespace edtrace {
struct GameInputCompatStats {
  DWORD error = ERROR_NOT_READY;
  bool started = false, hooks_installed = false, ready = false, output_owned = false;
  bool waiting_native_zero = false, pending_stop = false;
  bool initial_failure = false;
  DWORD initial_failure_reason = ERROR_SUCCESS;
  unsigned matches = 0, cached_devices = 0;
  unsigned connected_gamepads = 0;
  bool steam_virtual_alias = false;
  DWORD native_motors = 0, vendor = 0, product = 0, slot = 0;
  std::array<unsigned char, 32> identity{};
  std::uint64_t submissions = 0, trigger_submissions = 0, stops = 0;
  std::uint64_t stale_native_suppressed = 0, quieted_requests = 0, native_forwards = 0;
  std::uint64_t native_events = 0, native_identity_matches = 0, native_fences = 0;
  std::uint64_t refreshes = 0, focus_stops = 0;
  bool held_nonzero = false;
  std::uintptr_t caps_target_rva = 0, rumble_target_rva = 0;
};
// The host must verify the pinned Microsoft runtime hash before this call.
// v4_input must be the live IID_IGameInput(v4) instance created by that runtime.
// Native Windows returns ERROR_NOT_SUPPORTED and keeps all original methods.
DWORD GameInputCompatStart(HMODULE native_runtime, void* v4_input) noexcept;
// Called outside the output adapter's lock. Owns discovery/stop transitions.
void GameInputCompatPoll() noexcept;
// Unregisters outside locks. Installed trampolines remain resident/inactive.
void GameInputCompatStop() noexcept;
GameInputCompatStats GameInputCompatGetStats() noexcept;
}
