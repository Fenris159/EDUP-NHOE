#pragma once

#include "edtrace/sco_haptics.hpp"
#include <string>

namespace edtrace {
// The restoration worker owns this reader. Status is read-only and has a
// separate polling cadence from the motor envelopes.
class ScoFeedback {
 public:
  bool Start(float strength) noexcept;
  HapticFrame Tick(bool ready, std::uint64_t now) noexcept;

 private:
  std::wstring path_;
  std::uint64_t earliest_write_ = 0;
  std::uint64_t next_poll_ = 0;
  std::optional<std::uint64_t> last_success_;
  float strength_ = 0;
  ScoHaptics haptics_;
};
}  // namespace edtrace
