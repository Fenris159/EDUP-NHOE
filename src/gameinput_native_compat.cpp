#include "edtrace/gameinput_compat.hpp"
#include "edtrace/gameinput_compat_hid.hpp"

// CrossOver uses the capabilities and transport actually provided by its
// native GameInput/XInput path. No Linux endpoint discovery or capability
// interception is installed in this profile.
namespace edtrace {
DWORD GameInputCompatStart(HMODULE, void*) noexcept { return ERROR_NOT_SUPPORTED; }
void GameInputCompatPoll() noexcept {}
void GameInputCompatStop() noexcept {}
GameInputCompatStats GameInputCompatGetStats() noexcept {
  GameInputCompatStats value; value.error = ERROR_NOT_SUPPORTED; return value;
}
DWORD GameInputCompatHidStart(DWORD, DWORD) noexcept { return ERROR_NOT_SUPPORTED; }
void GameInputCompatHidPoll() noexcept {}
bool GameInputCompatHidReady() noexcept { return false; }
bool GameInputCompatHidSubmit(float, float, float, float) noexcept { return false; }
void GameInputCompatHidStop() noexcept {}
GameInputCompatHidStats GameInputCompatHidGetStats() noexcept {
  GameInputCompatHidStats value; value.error = ERROR_NOT_SUPPORTED; value.last_write_error = ERROR_NOT_SUPPORTED; return value;
}
}
