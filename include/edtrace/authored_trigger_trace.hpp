#pragma once
#include <windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include "edtrace/observation_window.hpp"

namespace edtrace {
struct AuthoredTriggerTraceStats {
  DWORD installation_error = 0;
  DWORD logging_error = 0;
  DWORD hook_mask = 0;
  bool started = false;
  bool stopped = false;
  bool controlled_fixture = false;
  bool time_limit_reached = false;
  std::uint64_t post_events = 0;
  std::uint64_t state_requests = 0;
  std::uint64_t state_manager_calls = 0;
  std::uint64_t source_initializations = 0;
  std::uint64_t source_executions = 0;
  std::uint64_t unknown_source_audio = 0;
  std::uint64_t unknown_source_identity = 0;
  std::uint64_t written = 0;
  std::uint64_t queue_drops = 0;
  std::uint64_t storage_drops = 0;
};

// Start/Stop belong outside DllMain and Wwise callbacks. The host must have
// verified the complete executable hash, in addition to the entry-byte checks.
// One session per process. Disabled hooks retain their trampolines until exit.
DWORD StartAuthoredTriggerTrace(std::uintptr_t image, std::size_t image_size,
                               bool verified, const wchar_t* log_directory) noexcept;
bool StopAuthoredTriggerTrace() noexcept;
AuthoredTriggerTraceStats GetAuthoredTriggerTraceStats() noexcept;

// Owned C++ verification only; never export from a production .def file.
// Fixture signatures identify fake hook targets, not a verified game profile.
struct AuthoredTriggerTraceFixtureTargets {
  std::uintptr_t image = 0;
  std::array<std::uintptr_t, 5> addresses{};
  std::array<std::array<unsigned char, 32>, 5> signatures{};
  std::uint64_t max_capture_ms = kMaxLiveObservationMs;
  ObservationClock observation_clock = nullptr;
};
DWORD StartAuthoredTriggerTraceControlledFixture(const AuthoredTriggerTraceFixtureTargets& targets,
                                                const wchar_t* log_directory) noexcept;
}  // namespace edtrace
