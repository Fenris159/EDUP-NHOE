#pragma once
#include "edtrace/vr_backend.hpp"

namespace edtrace {
bool NativeXrRelayStart() noexcept;
void NativeXrRelayStop() noexcept;
VrBool ED_VR_CALL NativeXrRelayRead(VrDword backend,VrBool session_ready,VrBridgeFrame* frame) noexcept;
}
