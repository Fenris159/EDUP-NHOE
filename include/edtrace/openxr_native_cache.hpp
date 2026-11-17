#pragma once
#include "edtrace/vr_backend.hpp"
#include <cstdint>

namespace edtrace {
constexpr std::uint64_t kNativeXrReadyLeaseMs=100,kNativeXrFrameLeaseMs=100;
inline bool NativeXrRecentlyReady(bool ready,std::uint64_t now,std::uint64_t reported) noexcept {
  return ready&&now>=reported&&now-reported<kNativeXrReadyLeaseMs;
}
struct NativeXrCachedFrame {
  HapticFrame frame{};
  std::uint64_t received=0,epoch=0;
  bool known=false,owned=false;
  bool Sample(std::uint64_t now,std::uint64_t expected_epoch,VrBridgeFrame& output) const noexcept {
    output={};
    if(!known||!owned||epoch!=expected_epoch||now<received||now-received>=kNativeXrFrameLeaseMs) return false;
    output.frame=HapticScale(frame,1); return true;
  }
};
}
