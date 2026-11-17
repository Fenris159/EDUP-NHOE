#include "edtrace/motor_headroom.hpp"
#include "edtrace/motor_sender_profile.hpp"
#include "MinHook.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>

namespace edtrace { namespace {
using Sender = std::uint64_t (WINAPI*)(void*, float, float);
static_assert(sizeof(void*) == 8 && std::atomic<Sender>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
std::atomic<Sender> original{nullptr};
std::atomic<bool> active{false}, fixture_mode{false};
std::atomic<DWORD> started{0}, error{ERROR_NOT_READY};
std::atomic<std::uint64_t> calls{0}, capped{0}, invalid{0};

float Bound(float value) noexcept {
  if (!std::isfinite(value)) {
    invalid.fetch_add(1, std::memory_order_relaxed);
    return 0.0f;
  }
  if (value > 1.0f) {
    capped.fetch_add(1, std::memory_order_relaxed);
    return 1.0f;
  }
  // Preserve all finite in-range/quiet values, including signed zero.
  return value;
}
std::uint64_t WINAPI Limited(void* self, float left, float right) noexcept {
  const auto sender = original.load(std::memory_order_acquire);
  if (active.load(std::memory_order_acquire)) {
    calls.fetch_add(1, std::memory_order_relaxed);
    left = Bound(left);
    right = Bound(right);
  }
  // No OS calls, allocations or I/O before/after the native function. Native
  // results, LastError, device index, quiet threshold and transfer stay native.
  return sender(self, left, right);
}
bool Match(std::uintptr_t address, const unsigned char* expected, std::size_t length) noexcept {
  if (!address || length > motor_profile::sender_bytes.size() ||
      address > std::numeric_limits<std::uintptr_t>::max() - length) return false;
  std::array<unsigned char, motor_profile::sender_bytes.size()> bytes{};
  SIZE_T count = 0;
  return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), bytes.data(), length, &count) &&
         count == length && std::memcmp(bytes.data(), expected, length) == 0;
}
DWORD Fail(DWORD value) noexcept { error.store(value); return value; }
DWORD StartAt(std::uintptr_t sender, bool fixture) noexcept {
  DWORD expected = 0;
  if (!started.compare_exchange_strong(expected, 1)) return ERROR_ALREADY_INITIALIZED;
  fixture_mode.store(fixture);
  if (!Match(sender, motor_profile::sender_bytes.data(), motor_profile::sender_bytes.size()))
    return Fail(ERROR_REVISION_MISMATCH);
  for (const auto& constant : motor_profile::constants) {
    if (sender > std::numeric_limits<std::uintptr_t>::max() - constant.offset ||
        !Match(sender + constant.offset, constant.bytes.data(), constant.size)) return Fail(ERROR_REVISION_MISMATCH);
  }
  auto status = MH_Initialize();
  if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) return Fail(ERROR_NOT_SUPPORTED);
  void* trampoline = nullptr;
  if (MH_CreateHook(reinterpret_cast<void*>(sender), reinterpret_cast<void*>(&Limited), &trampoline) != MH_OK)
    return Fail(ERROR_NOT_SUPPORTED);
  Sender typed = nullptr;
  static_assert(sizeof(typed) == sizeof(trampoline));
  std::memcpy(&typed, &trampoline, sizeof(typed));
  original.store(typed, std::memory_order_release);
  active.store(true, std::memory_order_release);
  if (MH_EnableHook(reinterpret_cast<void*>(sender)) != MH_OK) {
    // Retain any potentially published trampoline until exit. Do not unhook
    // while a callback could be using it, and never initialize the gain probe.
    active.store(false, std::memory_order_release);
    return Fail(ERROR_NOT_SUPPORTED);
  }
  error.store(ERROR_SUCCESS);
  return ERROR_SUCCESS;
}
}}

namespace edtrace {
DWORD MotorHeadroomStart(std::uintptr_t imageBase, bool verified) noexcept {
  if (!verified || !imageBase || imageBase > std::numeric_limits<std::uintptr_t>::max() - motor_profile::sender_rva)
    return Fail(ERROR_REVISION_MISMATCH);
  return StartAt(imageBase + motor_profile::sender_rva, false);
}
DWORD MotorHeadroomStartControlledFixture(std::uintptr_t sender) noexcept { return StartAt(sender, true); }
MotorHeadroomStats MotorHeadroomGetStats() noexcept {
  MotorHeadroomStats stats;
  stats.installation_error = error.load(); stats.active = active.load(std::memory_order_acquire);
  stats.controlled_fixture = fixture_mode.load(); stats.calls = calls.load();
  stats.capped_channels = capped.load(); stats.invalid_channels = invalid.load();
  return stats;
}
}  // namespace edtrace
