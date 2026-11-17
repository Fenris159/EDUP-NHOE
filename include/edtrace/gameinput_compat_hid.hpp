#pragma once
#include <windows.h>
#include <array>
#include <cstdint>

namespace edtrace {
struct GameInputCompatHidStats {
  DWORD error = ERROR_NOT_READY, last_write_error = ERROR_NOT_READY;
  bool started = false, ready = false;
  std::uint64_t queued = 0, writes = 0, stops = 0, write_failures = 0;
  std::uint64_t identity_failures = 0, descriptor_failures = 0, reconnects = 0, invalid_channels = 0;
  std::array<char, 18> controller_address{};
  std::array<wchar_t, 64> device_path{};
  std::array<unsigned char, 9> last_report{};
};
// First supported endpoint: xpadneo Xbox Elite Series 2 Bluetooth 045e:0b22.
// Opens metadata/device handles only. No permission or configuration changes.
DWORD GameInputCompatHidStart(DWORD vendor, DWORD product) noexcept;
// Requests asynchronous rediscovery. Never reads controller input state.
void GameInputCompatHidPoll() noexcept;
bool GameInputCompatHidReady() noexcept;
// True means accepted by the native worker, not physical motor delivery.
// Latest frames coalesce; stats.writes counts completed 9-byte kernel writes.
bool GameInputCompatHidSubmit(float low, float high, float left_trigger, float right_trigger) noexcept;
// Sends a best-effort zero and joins the worker with a bounded cancellation.
// A completed barrier has stats.started=false. On timeout, started stays true,
// error=ERROR_TIMEOUT; retain this module and do not activate another output.
void GameInputCompatHidStop() noexcept;
GameInputCompatHidStats GameInputCompatHidGetStats() noexcept;
}
