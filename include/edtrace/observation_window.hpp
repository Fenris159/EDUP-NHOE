#pragma once
#include <atomic>
#include <cstdint>

namespace edtrace {
constexpr std::uint64_t kMaxLiveObservationMs = 600000;
using ObservationClock = std::uint64_t (*)() noexcept;

// Initialize before publishing hooks. Reset only with recording admission
// closed and the previous writer joined. Runtime configuration cannot extend
// the ceiling; the shorter duration and clock seam are for owned fixtures.
class ObservationWindow {
 public:
  void Initialize(ObservationClock clock, std::uint64_t duration_ms) noexcept {
    clock_ = clock;
    duration_ms_ = duration_ms > kMaxLiveObservationMs ? kMaxLiveObservationMs : duration_ms;
    Reset();
  }
  void Reset() noexcept {
    started_ms_.store(clock_(), std::memory_order_release);
    expired_.store(false, std::memory_order_release);
  }
  bool Expired() noexcept {
    if (expired_.load(std::memory_order_acquire)) return true;
    if (clock_() - started_ms_.load(std::memory_order_acquire) < duration_ms_) return false;
    expired_.store(true, std::memory_order_release);
    return true;
  }
  bool TimeLimitReached() const noexcept { return expired_.load(std::memory_order_acquire); }
  std::uint64_t DurationMs() const noexcept { return duration_ms_; }
 private:
  ObservationClock clock_ = nullptr;
  std::uint64_t duration_ms_ = kMaxLiveObservationMs;
  std::atomic<std::uint64_t> started_ms_{0};
  std::atomic<bool> expired_{false};
};
}  // namespace edtrace
