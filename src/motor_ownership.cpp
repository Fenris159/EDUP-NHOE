#include "edtrace/motor_ownership.hpp"
#include "edtrace/motor_ownership_policy.hpp"
#include <atomic>

namespace edtrace { namespace {
SRWLOCK target_lock = SRWLOCK_INIT;
MotorOwnershipPolicy policy(0);
thread_local bool inside_dispatch = false, nested_call = false;
std::atomic<std::uint64_t> forwarded{0}, suppressed{0}, reentrant{0};
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
OwnershipSource Source(Writer writer) noexcept {
  switch (writer) {
    case Writer::WwiseShared: return OwnershipSource::Wwise;
    case Writer::WwiseTerminationRetry: return OwnershipSource::WwiseRetry;
    case Writer::Engine: return OwnershipSource::Engine;
    case Writer::OnFoot: return OwnershipSource::OnFoot;
    default: return OwnershipSource::Other;
  }
}
std::uint64_t SystemClock(void*) noexcept { return GetTickCount64(); }
}}

namespace edtrace {
OwnedMotorResult DispatchOwnedMotor(MotorBackend backend, bool routing, bool verified,
                                   Writer writer, DWORD index, XINPUT_VIBRATION* vibration,
                                   bool snapshot_known, const XINPUT_VIBRATION& snapshot,
                                   OwnershipClock clock, void* clock_context, bool authored_trigger_active) noexcept {
  const DWORD incoming = GetLastError();
  const DWORD destination = RoutedIndex(routing, writer, index);
  auto forward = [&]() {
    SetLastError(incoming);
    const DWORD result = backend ? backend(destination, vibration) : ERROR_DEVICE_NOT_CONNECTED;
    const DWORD outgoing = GetLastError();
    if (backend) forwarded.fetch_add(1, std::memory_order_relaxed);
    SetLastError(outgoing);
    return OwnedMotorResult{result, backend != nullptr, false};
  };
  if (destination != 0) return forward();
  if (inside_dispatch) {
    // An unexpected recursive backend cannot deadlock or establish ownership.
    nested_call = true;
    reentrant.fetch_add(1, std::memory_order_relaxed);
    return forward();
  }
  AcquireSRWLockExclusive(&target_lock);
  inside_dispatch = true; nested_call = false;
  if (!clock) clock = SystemClock;
  MotorOwnershipRequest request{routing, verified, snapshot_known, destination, Source(writer),
                                snapshot.wLeftMotorSpeed, snapshot.wRightMotorSpeed, authored_trigger_active};
  if (policy.ShouldSuppress(request, clock(clock_context))) {
    suppressed.fetch_add(1, std::memory_order_relaxed);
    inside_dispatch = false;
    ReleaseSRWLockExclusive(&target_lock);
    SetLastError(incoming);
    return {ERROR_SUCCESS, false, true};
  }
  auto result = forward();
  const DWORD outgoing = GetLastError();
  if (nested_call) request.verified = false;
  policy.Complete(request, result.result, clock(clock_context));
  inside_dispatch = false;
  ReleaseSRWLockExclusive(&target_lock);
  SetLastError(outgoing);
  return result;
}
MotorOwnershipStats MotorOwnershipGetStats() noexcept {
  return {forwarded.load(), suppressed.load(), reentrant.load()};
}
}  // namespace edtrace
