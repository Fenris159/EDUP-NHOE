#include "edtrace/onfoot_runtime.hpp"
#include "edtrace/xinput_output.hpp"
#include <array>
#include <atomic>

namespace edtrace {
namespace {
constexpr std::size_t kCapacity = 2048;
struct Slot { std::atomic<std::size_t> sequence{0}; OnFootInput input{}; std::uint64_t activity = 0; };
std::array<Slot,kCapacity> queue{};
std::atomic<std::size_t> write_position{0};
std::size_t read_position = 0;
std::atomic<bool> configured{false}, accepting{false}, active{false}, faulted{false};
// One worker advances this epoch; callbacks tag admission before reserving a
// slot. A producer delayed across a focus/enable transition cannot revive cues.
std::atomic<std::uint64_t> activity{0};
std::atomic<std::uint64_t> queued{0}, dropped{0}, processed{0}, accepted{0}, rejected{0};
std::atomic<std::uint64_t> duplicate_cues{0}, expired_holds{0}, actor_overflows{0}, output_frames{0};
OnFootHaptics engine;
float strength = 0;
std::uint64_t SetActivity(bool ready) noexcept {
  auto value = activity.load(std::memory_order_relaxed);
  if (static_cast<bool>(value & 1) != ready) activity.store(++value,std::memory_order_release);
  return value;
}
bool Pop(OnFootInput& input, std::uint64_t& admitted) noexcept {
  auto& slot = queue[read_position % kCapacity];
  if (slot.sequence.load(std::memory_order_acquire) != read_position + 1) return false;
  input = slot.input; admitted = slot.activity;
  slot.sequence.store(read_position + kCapacity,std::memory_order_release); ++read_position;
  return true;
}
}
void StartOnFootFeedback(float value) noexcept {
  if (configured.load(std::memory_order_acquire)) return;
  strength = HapticUnit(value);
  for (std::size_t i = 0; i < queue.size(); ++i) queue[i].sequence.store(i,std::memory_order_relaxed);
  configured.store(true,std::memory_order_release); accepting.store(true,std::memory_order_release);
}
void QueueOnFootInput(const OnFootInput& input) noexcept {
  if (!accepting.load(std::memory_order_acquire)) return;
  const auto admitted = activity.load(std::memory_order_acquire);
  auto position = write_position.load(std::memory_order_relaxed);
  for (unsigned attempt = 0; attempt < 16; ++attempt) {
    auto& slot = queue[position % kCapacity];
    const auto difference = static_cast<std::intptr_t>(slot.sequence.load(std::memory_order_acquire)) - static_cast<std::intptr_t>(position);
    if (difference == 0) {
      if (write_position.compare_exchange_weak(position,position+1,std::memory_order_relaxed)) {
        slot.input = input; slot.activity = admitted; slot.sequence.store(position+1,std::memory_order_release);
        queued.fetch_add(1,std::memory_order_relaxed); return;
      }
    } else if (difference < 0) break;
    else position = write_position.load(std::memory_order_relaxed);
  }
  dropped.fetch_add(1,std::memory_order_release);
}
void SetOnFootFeedbackStrength(float value) noexcept { strength = HapticUnit(value); }
void TickOnFootFeedback(bool ready, std::uint64_t now) noexcept {
  if (!configured.load(std::memory_order_acquire)) return;
  if (dropped.load(std::memory_order_acquire)) faulted.store(true,std::memory_order_release);
  ready = ready && accepting.load(std::memory_order_acquire) && !faulted.load(std::memory_order_acquire);
  const auto current_activity = SetActivity(ready);
  engine.SetActive(ready);
  OnFootInput input{}; std::uint64_t admitted = 0;
  for (std::size_t consumed = 0; consumed < kCapacity && !dropped.load(std::memory_order_acquire) && Pop(input,admitted); ++consumed) {
    engine.Accept(input,ready && admitted == current_activity); processed.fetch_add(1,std::memory_order_relaxed);
  }
  if (dropped.load(std::memory_order_acquire)) {
    faulted.store(true,std::memory_order_release); accepting.store(false,std::memory_order_release);
    ready = false; engine.SetActive(false);
  }
  const auto policy = engine.Stats();
  accepted.store(policy.accepted); rejected.store(policy.rejected); duplicate_cues.store(policy.duplicate_cues);
  expired_holds.store(policy.expired_holds); actor_overflows.store(policy.actor_overflows);
  if (policy.actor_overflows) { faulted.store(true); accepting.store(false); ready = false; engine.SetActive(false); }
  if (!ready) SetActivity(false);
  const auto frame = HapticScale(engine.Sample(now),strength);
  if (HapticNonzero(frame)) output_frames.fetch_add(1,std::memory_order_relaxed);
  XInputOutputSetOnFootFrame(frame,ready); active.store(ready,std::memory_order_release);
}
void StopOnFootFeedback() noexcept {
  accepting.store(false,std::memory_order_release); active.store(false,std::memory_order_release);
  SetActivity(false);
  // Called on the policy worker after sources are detached. Storage remains
  // resident so an already-entered callback can safely observe accepting=false.
  engine.SetActive(false); XInputOutputSetOnFootFrame({},false);
}
OnFootRuntimeStats GetOnFootRuntimeStats() noexcept {
  return {configured.load(),active.load(),faulted.load(),queued.load(),processed.load(),dropped.load(),
          accepted.load(),rejected.load(),duplicate_cues.load(),expired_holds.load(),actor_overflows.load(),output_frames.load()};
}
}  // namespace edtrace
