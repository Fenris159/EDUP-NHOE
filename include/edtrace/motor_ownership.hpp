#pragma once
#include "edtrace/motor_dispatch.hpp"
#include <cstdint>

namespace edtrace {
using OwnershipClock = std::uint64_t (*)(void*) noexcept;
struct OwnedMotorResult { DWORD result; bool backend_forwarded; bool suppressed; };
struct MotorOwnershipStats { std::uint64_t forwarded = 0, suppressed = 0, reentrant_passthrough = 0; };

// Serializes target-controller decision, original backend and ownership commit.
// The snapshot is read-only; the original pointer reaches every forwarded call.
// A suppressed zero returns success and retains incoming LastError; logs must
// distinguish it from a real backend call. Other indices always pass through.
OwnedMotorResult DispatchOwnedMotor(MotorBackend backend, bool routing, bool verified,
                                   Writer writer, DWORD index, XINPUT_VIBRATION* vibration,
                                   bool snapshot_known, const XINPUT_VIBRATION& snapshot,
                                   OwnershipClock clock = nullptr, void* clock_context = nullptr,
                                   bool authored_trigger_active = false) noexcept;
MotorOwnershipStats MotorOwnershipGetStats() noexcept;
}  // namespace edtrace
