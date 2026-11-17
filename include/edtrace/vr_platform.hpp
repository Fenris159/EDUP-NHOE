#pragma once
#include <cstdint>
#ifdef _WIN32
#include <windows.h>
#define ED_VR_CALL WINAPI
#else
#define ED_VR_CALL
#endif

namespace edtrace {
#ifdef _WIN32
using VrDword=DWORD;
using VrBool=BOOL;
#else
using VrDword=std::uint32_t;
using VrBool=std::int32_t;
#endif
static_assert(sizeof(VrDword)==4&&sizeof(VrBool)==4);
}
