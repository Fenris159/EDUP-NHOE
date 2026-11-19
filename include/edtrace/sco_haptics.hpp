#pragma once

#include "edtrace/haptic_frame.hpp"
#include <cstdint>
#include <optional>
#include <string_view>

namespace edtrace {
struct ScoStatus {
  bool in_supercruise = false;
  bool overcharged = false;
};
std::optional<ScoStatus> ParseScoStatus(std::string_view json) noexcept;

class ScoHaptics {
 public:
  void Observe(ScoStatus status, std::uint64_t now) noexcept;
  void Reset() noexcept;
  HapticFrame Sample(std::uint64_t now) const noexcept;

 private:
  enum class Pulse { None, Engage, Disengage };
  Pulse pulse_ = Pulse::None;
  std::uint64_t start_ = 0;
  std::optional<bool> previous_;
};
}  // namespace edtrace
