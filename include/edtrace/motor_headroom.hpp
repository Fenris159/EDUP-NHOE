#pragma once

#include <windows.h>
#include <cstdint>

namespace edtrace {
struct MotorHeadroomStats {
  DWORD installation_error = ERROR_NOT_READY;
  bool active = false;
  bool controlled_fixture = false;
  std::uint64_t calls = 0;
  std::uint64_t capped_channels = 0;
  std::uint64_t invalid_channels = 0;
};

// Carries the verified sender's capped inputs across its synchronous XInput
// call. Owned fixtures use the same scope around their synthetic sender.
class MotorSenderScope {
 public:
  struct Amplitudes { float left = 0; float right = 0; };
  MotorSenderScope(float left, float right) noexcept
      : previous_(current_), amplitudes_{left, right} { current_ = this; }
  ~MotorSenderScope() { current_ = previous_; }
  MotorSenderScope(const MotorSenderScope&) = delete;
  MotorSenderScope& operator=(const MotorSenderScope&) = delete;

  static Amplitudes Current() noexcept { return current_ ? current_->amplitudes_ : Amplitudes{}; }

 private:
  inline static thread_local const MotorSenderScope* current_ = nullptr;
  const MotorSenderScope* previous_;
  Amplitudes amplitudes_;
};

// Outside DllMain. verified requires the complete known executable SHA-256.
// Independently verifies the entire sender and all eight conversion constants.
// The hook and its cap remain resident until process exit, including after capture.
DWORD MotorHeadroomStart(std::uintptr_t imageBase, bool verified) noexcept;
MotorHeadroomStats MotorHeadroomGetStats() noexcept;

// Isolated owned code only; never exported by the proxy.
DWORD MotorHeadroomStartControlledFixture(std::uintptr_t sender) noexcept;
}  // namespace edtrace
