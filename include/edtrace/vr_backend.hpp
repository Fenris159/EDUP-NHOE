#pragma once
#include "edtrace/haptic_frame.hpp"
#include "edtrace/vr_platform.hpp"
#include <cstdint>

namespace edtrace {
enum class VrBackend : VrDword { SteamVR=1, Meta=2, OpenXR=3, NativeOpenXR=4 };
struct VrBridgeFrame {
  VrDword size=sizeof(VrBridgeFrame), version=1;
  HapticFrame frame{};
  std::uint64_t generation=0;
};
static_assert(sizeof(VrBridgeFrame)==32);
using VrFrameRead=VrBool (ED_VR_CALL*)(VrDword backend,VrBool session_ready,VrBridgeFrame* frame);
// This is an output-only lease/read operation, not controller acquisition.
// TRUE means this backend currently owns output. FALSE always returns zero.
// Direct adapters call on their existing session/frame path, including ready=0
// on focus/session loss. Short leases and arbitration are owned by the proxy.
VrBool ED_VR_CALL VrOutputReadFrame(VrDword backend,VrBool session_ready,VrBridgeFrame* frame) noexcept;
// Native LibOVR has no finite-duration parameter. A successful explicit zero
// acknowledges its stop before arbitration hands output to another runtime.
void VrOutputAcknowledgeStop(VrDword backend) noexcept;
}
