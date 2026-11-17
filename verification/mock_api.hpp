#pragma once
#include <windows.h>
#include <xinput.h>

struct MockSnapshot {
  ULONGLONG set_calls;
  DWORD index;
  DWORD flags;
  ULONG_PTR pointer;
  ULONG_PTR second_pointer;
  DWORD incoming_error;
  WORD left;
  WORD right;
};
using MockResetFn = void (WINAPI*)();
using MockReadFn = void (WINAPI*)(MockSnapshot*);
using MockWaitFn = DWORD (WINAPI*)(DWORD);
using MockReleaseFn = void (WINAPI*)();
