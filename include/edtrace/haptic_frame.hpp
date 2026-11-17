#pragma once
#include <cmath>

namespace edtrace {
struct HapticFrame {
  float low = 0, high = 0, left = 0, right = 0;
};
inline float HapticUnit(float value) noexcept {
  return !std::isfinite(value) || value <= 0 ? 0 : value >= 1 ? 1 : value;
}
inline HapticFrame HapticScale(HapticFrame value, float scale) noexcept {
  scale = HapticUnit(scale);
  return {HapticUnit(value.low) * scale, HapticUnit(value.high) * scale,
          HapticUnit(value.left) * scale, HapticUnit(value.right) * scale};
}
inline HapticFrame HapticMaximum(HapticFrame first, HapticFrame second) noexcept {
  const auto maximum = [](float a, float b) { a = HapticUnit(a); b = HapticUnit(b); return a > b ? a : b; };
  return {maximum(first.low, second.low), maximum(first.high, second.high),
          maximum(first.left, second.left), maximum(first.right, second.right)};
}
inline bool HapticNonzero(HapticFrame value) noexcept {
  return HapticUnit(value.low) > 0 || HapticUnit(value.high) > 0 || HapticUnit(value.left) > 0 || HapticUnit(value.right) > 0;
}
}  // namespace edtrace
