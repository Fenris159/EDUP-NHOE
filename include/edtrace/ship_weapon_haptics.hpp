#pragma once

#include "edtrace/onfoot_haptics.hpp"
#include "edtrace/ship_weapon_routes.hpp"

#include <array>
#include <cstdint>

namespace edtrace {
// Share the event ingress and output mixer with the on-foot effects. Ship
// ownership uses a fresh registration and an explicit player attachment.
class ShipWeaponHaptics {
 public:
  static bool Observes(OnFootInputKind kind, std::uint32_t id) noexcept;
  void SetActive(bool active) noexcept;
  void Accept(const OnFootInput& input, bool permitted, bool overlapping) noexcept;
  using PendingCall = bool (*)(std::uint64_t object) noexcept;
  HapticFrame Sample(std::uint64_t now, PendingCall pending = nullptr) noexcept;
  bool Overflowed() const noexcept { return overflowed_; }

 private:
  struct Voice {
    const ShipWeaponProfile* profile = nullptr;
    std::uint64_t cue = 0, deadline = 0;
    bool fired = false;
    std::uint32_t event = 0, playing = 0;
  };
  struct Actor {
    std::uint64_t object = 0;
    bool attached = false;
    Voice voice{};
  };
  Actor* Find(std::uint64_t object) noexcept;
  void Register(std::uint64_t object, Actor* existing) noexcept;
  static HapticFrame SampleVoice(Voice& voice, std::uint64_t now) noexcept;

  std::array<Actor, 1024> actors_{};
  bool active_ = false, overflowed_ = false;
};
}  // namespace edtrace
