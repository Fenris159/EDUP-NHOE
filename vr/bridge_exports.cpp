#include "edtrace/vr_backend.hpp"
extern "C" BOOL WINAPI EDVRReadFrame(DWORD backend,BOOL ready,edtrace::VrBridgeFrame* frame) noexcept {
  return edtrace::VrOutputReadFrame(backend,ready,frame);
}
