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

// Outside DllMain. verified requires the complete known executable SHA-256.
// Independently verifies the entire sender and all eight conversion constants.
// The hook and its cap remain resident until process exit, including after capture.
DWORD MotorHeadroomStart(std::uintptr_t imageBase, bool verified) noexcept;
MotorHeadroomStats MotorHeadroomGetStats() noexcept;

// Isolated owned code only; never exported by the proxy.
DWORD MotorHeadroomStartControlledFixture(std::uintptr_t sender) noexcept;
}  // namespace edtrace
