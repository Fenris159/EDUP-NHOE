#pragma once
#include <windows.h>
#include <cstdint>

namespace edtrace {
struct MetaOutputStats {
  DWORD error=ERROR_NOT_READY;
  bool installed=false,session=false,focused=false,owned=false;
  std::uint64_t submissions=0,stops=0,sessions=0;
};
// Poll on the existing proxy worker. Observe only an already-loaded LibOVR
// runtime and its render/session lifecycle; never initialize another session.
void MetaOutputPoll() noexcept;
void MetaOutputStop() noexcept;
MetaOutputStats MetaOutputGetStats() noexcept;
}
