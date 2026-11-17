#pragma once
#include <windows.h>
#include <unknwn.h>
struct DInputSnapshot {
  HINSTANCE instance; DWORD version; GUID iid; void** output; IUnknown* outer; DWORD incoming_error;
  DWORD process_attach_thread; LONG thread_attach_count;
};
using DInputReadFn = void (WINAPI*)(DInputSnapshot*);
using DInputObjectFn = IUnknown* (WINAPI*)();
