#pragma once

#include <windows.h>
#include <cstddef>
#include <cwchar>

struct EdRumbleTraceStatus {
  DWORD size;
  DWORD version;
  DWORD backend_error;
  DWORD logging_error;
  DWORD accepting_records;
  DWORD completed;
  ULONGLONG captured_calls;
  ULONGLONG completed_calls;
  ULONGLONG dropped_records;
  DWORD verification_backend;
};

using EdTraceStopFn = DWORD (WINAPI*)(DWORD timeout_ms);
using EdTraceStatusFn = DWORD (WINAPI*)(EdRumbleTraceStatus*, DWORD);

inline void EdTraceEventName(wchar_t* destination, std::size_t count, DWORD pid, const wchar_t* suffix) {
  swprintf(destination, count, L"Local\\EDRumbleTrace-%lu-%ls", static_cast<unsigned long>(pid), suffix);
}
