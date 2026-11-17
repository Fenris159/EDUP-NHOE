#pragma once
#include "edtrace/haptic_frame.hpp"
#include <array>

namespace edtrace {
// Output order: left body, right body, left trigger, right trigger. A missing
// trigger falls back only to its own hand; thumbs have no source channel.
inline std::array<float,4> OpenXRMotorAmplitudes(HapticFrame frame,
    const std::array<bool,4>& available,float strength) noexcept {
  frame=HapticScale(frame,1); strength=HapticUnit(strength);
  const float body=frame.low>frame.high?frame.low:frame.high;
  const float triggers[]{frame.left,frame.right};
  std::array<float,4> result{};
  for(unsigned hand=0;hand<2;++hand) {
    const bool separate=available[hand+2];
    const float mixed=!separate&&triggers[hand]>body?triggers[hand]:body;
    if(available[hand]) result[hand]=mixed*strength;
    if(separate) result[hand+2]=triggers[hand]*strength;
  }
  return result;
}
}
