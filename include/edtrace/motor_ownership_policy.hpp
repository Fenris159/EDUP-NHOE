#pragma once

#include <cstdint>

namespace edtrace {

enum class OwnershipSource { Wwise, WwiseRetry, Engine, Other, OnFoot };

struct MotorOwnershipRequest {
  bool enabled = false;
  bool verified = false;
  bool known_request = false;
  std::uint32_t controller = 0;
  OwnershipSource source = OwnershipSource::Other;
  std::uint16_t left = 0;
  std::uint16_t right = 0;
  bool authored_trigger_active = false;
};

// Pure decision state; never issues motor writes or changes request words.
// The caller serializes decision -> original backend -> Complete as one operation.
// Call Complete exactly once for forwarded requests; not for suppressed requests.
// Pass the same immutable requested-value snapshot, not backend-mutated values.
class MotorOwnershipPolicy {
 public:
  static constexpr std::uint64_t kLeaseMs = 100;

  explicit constexpr MotorOwnershipPolicy(std::uint32_t physical_controller) noexcept
      : controller_(physical_controller) {}

  bool ShouldSuppress(const MotorOwnershipRequest& request, std::uint64_t now_ms) noexcept {
    // Unrelated controllers do not change even the target's clock or pending state.
    if (request.controller != controller_) return false;
    pending_ = false;
    if (!ObserveClock(now_ms) || !Trusted(request)) {
      Release();
      return false;
    }
    if ((request.source == OwnershipSource::Wwise || request.source == OwnershipSource::OnFoot) && Nonzero(request)) {
      pending_request_ = request;
      pending_ = true;
      return false;
    }
    if (request.source == OwnershipSource::Engine && !Nonzero(request)) {
      if (leased_ && now_ms - successful_at_ms_ < kLeaseMs) return true;
    }
    // Stops, retries, other writers, nonzero engine output and expired leases yield.
    Release();
    return false;
  }

  void Complete(const MotorOwnershipRequest& request, std::uint32_t result,
                std::uint64_t now_ms) noexcept {
    if (request.controller != controller_) return;
    const bool may_acquire = pending_ && SameRequest(request, pending_request_);
    pending_ = false;
    if (!ObserveClock(now_ms) || !Trusted(request) || result != 0) {
      Release();
      return;
    }
    if ((request.source == OwnershipSource::Wwise || request.source == OwnershipSource::OnFoot) && Nonzero(request) && may_acquire) {
      successful_at_ms_ = now_ms;
      leased_ = true;
    } else {
      Release();
    }
  }

 private:
  static constexpr bool Nonzero(const MotorOwnershipRequest& request) noexcept {
    return request.left != 0 || request.right != 0 || request.authored_trigger_active;
  }
  static constexpr bool Trusted(const MotorOwnershipRequest& request) noexcept {
    return request.enabled && request.verified && request.known_request;
  }
  static constexpr bool SameRequest(const MotorOwnershipRequest& a,
                                    const MotorOwnershipRequest& b) noexcept {
    return a.enabled == b.enabled && a.verified == b.verified &&
           a.known_request == b.known_request && a.controller == b.controller &&
           a.source == b.source && a.left == b.left && a.right == b.right &&
           a.authored_trigger_active == b.authored_trigger_active;
  }
  bool ObserveClock(std::uint64_t now_ms) noexcept {
    if (clock_known_ && now_ms < last_clock_ms_) return false;
    clock_known_ = true;
    last_clock_ms_ = now_ms;
    return true;
  }
  void Release() noexcept {
    leased_ = false;
    pending_ = false;
  }

  const std::uint32_t controller_;
  bool leased_ = false;
  bool pending_ = false;
  bool clock_known_ = false;
  std::uint64_t successful_at_ms_ = 0;
  std::uint64_t last_clock_ms_ = 0;
  MotorOwnershipRequest pending_request_{};
};

}  // namespace edtrace
