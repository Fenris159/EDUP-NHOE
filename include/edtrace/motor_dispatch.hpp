#pragma once

#include <windows.h>
#include <xinput.h>
#include "edtrace/profile.hpp"

namespace edtrace {

using MotorBackend = DWORD (WINAPI*)(DWORD, XINPUT_VIBRATION*);

inline DWORD DispatchMotor(MotorBackend backend, bool enabled, Writer writer, DWORD index, XINPUT_VIBRATION* vibration) noexcept {
  return backend ? backend(RoutedIndex(enabled, writer, index), vibration) : ERROR_DEVICE_NOT_CONNECTED;
}

}  // namespace edtrace
