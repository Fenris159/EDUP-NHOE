#pragma once
#include "edtrace/vr_platform.hpp"
#include <cstdint>

namespace edtrace {
struct OpenXRHapticsStatus {
  VrDword size=sizeof(OpenXRHapticsStatus),version=1;
  VrDword target=0,instance_ready=0,session_ready=0,focused=0;
  VrDword attached=0,synchronized=0,bound_sources=0,failed=0;
  std::uint64_t frames=0,submissions=0,stops=0,standby=0;
};
}
