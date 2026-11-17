#pragma once
#include <cmath>
#include <cstdint>
#include "edtrace/motion_gain_bank.hpp"

namespace edtrace {
struct MotionGainState {
  std::uintptr_t identity = 0;
  std::uint32_t id = 0;
  float authored = 0;
  float global_delta = 0;
  bool unmodulated_root = false;
};
struct MotionGainApi {
  void* context = nullptr;
  bool (*read)(void*, MotionGainState&) noexcept = nullptr;
  bool (*collision_free)(void*) noexcept = nullptr;
  bool (*load)(void*) noexcept = nullptr;
  std::uint32_t (*post)(void*, std::uint32_t) noexcept = nullptr;
};
enum class GainPhase { Idle, AwaitEnable, Enabled, AwaitRestore, Restored, Failed };
enum class GainError { None, Profile, Api, Read, Root, ExistingOverride, Collision, Load, Post, Identity, ExternalChange };

// Coordinates only the native SDK boundary. It never writes a node or motor.
class MotionGainControl {
 public:
  MotionGainControl(bool verified, MotionGainApi api, bool allow_contextual_states = false) noexcept
      : api_(api), allow_contextual_states_(allow_contextual_states) {
    if (!verified) Fail(GainError::Profile);
    else if (!api.read || !api.collision_free || !api.load || !api.post) Fail(GainError::Api);
  }
  bool Prepare() noexcept {
    if (phase_ == GainPhase::Failed) return false;
    MotionGainState state{};
    if (!api_.read(api_.context, state)) return Fail(GainError::Read);
    if (!Valid(state)) return Fail(GainError::Root);
    if (identity_ && identity_ != state.identity) return Fail(GainError::Identity);
    identity_ = state.identity;
    if (!loaded_) {
      if (state.global_delta != 0.0f) return Fail(GainError::ExistingOverride);
      if (!api_.collision_free(api_.context)) return Fail(GainError::Collision);
      if (!api_.load(api_.context)) return Fail(GainError::Load);
      loaded_ = true;
    }
    return true;
  }
  bool Enable() noexcept {
    if (phase_ == GainPhase::Enabled || phase_ == GainPhase::AwaitEnable) return true;
    if (phase_ != GainPhase::Idle && phase_ != GainPhase::Restored) return false;
    MotionGainState state{};
    if (!api_.read(api_.context, state)) return Fail(GainError::Read);
    if (!Valid(state)) return Fail(GainError::Root);
    if (state.global_delta != 0.0f) return Fail(GainError::ExistingOverride);
    if (identity_ && identity_ != state.identity) return Fail(GainError::Identity);
    if (!Prepare()) return false;
    playing_id_ = api_.post(api_.context, gain_bank::enable_event);
    if (!playing_id_) return Fail(GainError::Post);
    owns_change_ = true; phase_ = GainPhase::AwaitEnable; return true;
  }
  bool Restore() noexcept {
    if (!owns_change_) return true;
    if (phase_ == GainPhase::AwaitRestore) return true;
    if (phase_ == GainPhase::AwaitEnable) {
      Observe();
      if (phase_ == GainPhase::AwaitEnable) return false;
    }
    MotionGainState state{};
    if (!api_.read(api_.context, state)) return Fail(GainError::Read);
    if (!Valid(state) || state.identity != identity_) return Fail(GainError::Identity);
    if (state.global_delta == 0.0f) {
      owns_change_ = false; phase_ = GainPhase::Restored; error_ = GainError::None; return true;
    }
    if (state.global_delta != 96.0f) return Fail(GainError::ExternalChange);
    playing_id_ = api_.post(api_.context, gain_bank::restore_event);
    if (!playing_id_) return Fail(GainError::Post);
    phase_ = GainPhase::AwaitRestore; return true;
  }
  // A queued playing ID proves admission; readback separately proves the value.
  bool Observe() noexcept {
    if (phase_ != GainPhase::AwaitEnable && phase_ != GainPhase::AwaitRestore) return true;
    MotionGainState state{};
    if (!api_.read(api_.context, state)) return false;
    if (!Valid(state) || state.identity != identity_) return Fail(GainError::Identity);
    const float expected = phase_ == GainPhase::AwaitEnable ? 96.0f : 0.0f;
    if (state.global_delta != expected) return false;
    if (phase_ == GainPhase::AwaitEnable) phase_ = GainPhase::Enabled;
    else { phase_ = GainPhase::Restored; owns_change_ = false; }
    error_ = GainError::None; return true;
  }
  GainPhase phase() const noexcept { return phase_; }
  GainError error() const noexcept { return error_; }
  bool owns_change() const noexcept { return owns_change_; }
  bool loaded() const noexcept { return loaded_; }
  std::uint32_t playing_id() const noexcept { return playing_id_; }
 private:
  bool Valid(const MotionGainState& state) const noexcept {
    return state.identity && state.id == 373542019U && state.authored == -96.0f &&
           std::isfinite(state.global_delta) && (state.unmodulated_root || allow_contextual_states_);
  }
  bool Fail(GainError error) noexcept { error_ = error; phase_ = GainPhase::Failed; return false; }
  MotionGainApi api_;
  GainPhase phase_ = GainPhase::Idle;
  GainError error_ = GainError::None;
  std::uintptr_t identity_ = 0;
  std::uint32_t playing_id_ = 0;
  bool loaded_ = false;
  bool owns_change_ = false;
  // Session restoration retains native state contributions. The diagnostic A/B
  // default still requires a neutral root to isolate its measured comparison.
  bool allow_contextual_states_ = false;
};
}
