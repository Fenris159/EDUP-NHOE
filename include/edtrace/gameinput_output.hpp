#pragma once
#include <windows.h>
#include <xinput.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include "edtrace/motor_dispatch.hpp"

namespace edtrace {
enum class GameInputSubmission { NativeFallback, Submitted, Quiet };
using GameInputFactory = HRESULT (WINAPI*)(REFIID, void**);
struct GameInputOutputStats {
  DWORD error = ERROR_NOT_READY;
  bool initialized = false, selected = false, foreground = false;
  unsigned matches = 0;
  std::uint64_t submissions = 0, stops = 0;
  DWORD supported_motors = 0;
  std::uint64_t trigger_submissions = 0;
};
DWORD GameInputOutputStart(const std::wstring& path, MotorBackend native_output) noexcept;
void GameInputOutputPoll(bool enabled, bool foreground) noexcept;
GameInputSubmission GameInputOutputSubmit(DWORD index, const XINPUT_VIBRATION& vibration,
                                          float left_trigger = 0, float right_trigger = 0) noexcept;
bool GameInputOutputOwnsCaller(std::uintptr_t caller) noexcept;
void GameInputOutputStop() noexcept;
GameInputOutputStats GameInputOutputGetStats() noexcept;
// Owned test factory only. Not exported by the production proxy.
DWORD GameInputOutputStartControlled(GameInputFactory factory) noexcept;
}
