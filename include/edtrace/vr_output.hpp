#pragma once
#include "edtrace/haptic_frame.hpp"
#include <windows.h>
#include <cstdint>

namespace edtrace {
struct VrOutputStats {
  DWORD error=ERROR_NOT_READY;
  bool listening=false;
  std::uint64_t polls=0,replies=0,frames=0;
  bool enabled=false,focused=false;
  DWORD backend=0;
  std::uint64_t generation=0;
};
// Worker-owned start/poll/stop. Publish only updates a bounded latest value;
// audio callbacks never call sockets, initialize VR, or allocate memory.
DWORD VrOutputStart(bool enabled) noexcept;
bool VrOutputPoll(bool gain_enabled, bool focused) noexcept;
void VrOutputPublish(HapticFrame frame) noexcept;
void VrOutputStop() noexcept;
VrOutputStats VrOutputGetStats() noexcept;
}
