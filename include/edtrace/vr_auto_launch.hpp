#pragma once
#include <windows.h>
#include <cstdint>

namespace edtrace {
struct VrAutoLaunchStats {
  DWORD error=ERROR_NOT_READY;
  bool wine=false, child_running=false;
  std::uint64_t attempts=0;
};
// Called only by the existing output worker. Never initializes OpenVR in the
// game, reads controller state, modifies controls, or asks for input focus.
void VrAutoLaunchPoll(bool steamvr_allowed=true) noexcept;
void VrAutoLaunchStop() noexcept;
VrAutoLaunchStats VrAutoLaunchGetStats() noexcept;
}
