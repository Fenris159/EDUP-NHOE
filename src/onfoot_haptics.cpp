#include "edtrace/onfoot_haptics.hpp"
#include "edtrace/onfoot_profiles.hpp"
#include <limits>

namespace edtrace {
namespace {
constexpr std::uint32_t kAttached = 1497268741, kInside = 103566125, kFirstPerson = 1979902012;
constexpr std::uint32_t kFirst = 998496889, kThird = 931160808, kAmmo = 3377597109;
constexpr std::uint32_t kTriggerOn = 1459991788, kTriggerDuration = 3661999171;
// Humanoid_speed in the footstep bank. The walk/run boundary is inferred from
// its movement curves, pending a controlled gait capture. Unknown gait is quiet.
constexpr std::uint32_t kHumanoidSpeed = 2410709172;
constexpr float kMaxWalkingSpeed = 2.0f;
bool ValidObject(std::uint64_t object) noexcept { return object && object != std::numeric_limits<std::uint64_t>::max(); }
}

void OnFootHaptics::SetActive(bool active) noexcept {
  if (!active) {
    pulses_ = {}; held_ = {}; jetpacks_ = {}; cue_seen_ = {};
    for (auto& actor : actors_) actor.speed_known = false;
  }
  active_ = active;
}
void OnFootHaptics::Reset() noexcept {
  actors_ = {}; pulses_ = {}; held_ = {}; jetpacks_ = {}; cue_seen_ = {}; active_ = false;
}
OnFootHaptics::Actor* OnFootHaptics::Find(std::uint64_t object) noexcept {
  for (auto& actor : actors_) if (actor.registered && actor.object == object) return &actor;
  return nullptr;
}
void OnFootHaptics::StopHold(std::uint64_t object) noexcept {
  for (auto& value : held_) if (value.object == object) value = {};
}
void OnFootHaptics::ClearOwner(std::uint64_t object) noexcept {
  if (auto* actor = Find(object)) actor->speed_known = false;
  StopHold(object);
  for (auto& value : pulses_) if (value.object == object) value = {};
  for (auto& value : jetpacks_) if (value.object == object) value = {};
}
HapticFrame OnFootHaptics::JetpackValue(const Jetpack& jetpack, std::uint64_t now) noexcept {
  if (now < jetpack.start || now >= jetpack.deadline) return {};
  if (jetpack.stopping) {
    if (now < jetpack.cutoff || now - jetpack.cutoff >= 100) return {};
    return HapticScale(jetpack.cutoff_value, static_cast<float>(100 - (now - jetpack.cutoff)) / 100.0f);
  }
  const auto elapsed = now - jetpack.start;
  if (elapsed < 12) return HapticScale({.60f,.40f,0,0},static_cast<float>(elapsed)/12.0f);
  if (elapsed <= 40) return {.60f,.40f,0,0};
  if (elapsed < 90) {
    const float fraction = static_cast<float>(elapsed-40)/50.0f;
    return {.60f-.30f*fraction,.40f-.24f*fraction,0,0};
  }
  return {.30f,.16f,0,0};
}
void OnFootHaptics::StartJetpack(std::uint64_t object, std::uint64_t now) noexcept {
  for (auto& value : jetpacks_) if (value.object == object) {
    if (!value.stopping && now < value.deadline) { ++stats_.duplicate_cues; return; }
    value = {object,now,now+10000,0,false,{}}; return;
  }
  for (auto& value : jetpacks_) if (!value.object || now >= value.deadline ||
      (value.stopping && now >= value.cutoff && now-value.cutoff >= 100)) {
    value = {object,now,now+10000,0,false,{}}; return;
  }
  ++stats_.rejected;
}
void OnFootHaptics::StopJetpack(std::uint64_t object, std::uint64_t now) noexcept {
  for (auto& value : jetpacks_) if (value.object == object && !value.stopping) {
    const auto frame = JetpackValue(value,now);
    if (!HapticNonzero(frame)) { value = {}; return; }
    value.stopping = true; value.cutoff = now; value.cutoff_value = frame; return;
  }
}
void OnFootHaptics::PulseFor(std::uint64_t object, std::uint64_t now, std::uint64_t duration, HapticFrame peak,
                           std::uint64_t attack, std::uint64_t plateau) noexcept {
  auto* slot = &pulses_[0];
  for (auto& value : pulses_) {
    if (value.end <= now) { slot = &value; break; }
    if (value.end < slot->end) slot = &value;
  }
  *slot = {object, now, now + duration, peak, attack, plateau};
}
bool OnFootHaptics::Cue(std::uint64_t now, unsigned group) noexcept {
  if (cue_seen_[group] && (now < last_cue_[group] || now - last_cue_[group] < 30)) {
    ++stats_.duplicate_cues; return false;
  }
  cue_seen_[group] = true; last_cue_[group] = now; return true;
}
void OnFootHaptics::Accept(const OnFootInput& input, bool effects_permitted) noexcept {
  if (!ValidObject(input.object)) return;
  if (input.kind == OnFootInputKind::Post && !effects_permitted) { ++stats_.rejected; return; }
  auto* actor = Find(input.object);
  if (input.kind == OnFootInputKind::Register) {
    if (input.result != 1) return;
    if (actor) { ClearOwner(input.object); *actor = {}; }
    else for (auto& candidate : actors_) if (!candidate.registered) { actor = &candidate; break; }
    if (!actor) { ++stats_.actor_overflows; return; }
    *actor = {}; actor->object = input.object; actor->registered = true; return;
  }
  if (input.kind == OnFootInputKind::Unregister) {
    if (input.result == 1 && actor) { ClearOwner(input.object); *actor = {}; }
    return;
  }
  if (!actor) { if (input.kind == OnFootInputKind::Post) ++stats_.rejected; return; }
  if (input.kind == OnFootInputKind::Parameter) {
    if (input.result != 1 || !std::isfinite(input.value)) return;
    if (input.id == kAttached) actor->attached = input.value == 1;
    if (input.id == kInside) actor->inside = input.value == 1;
    if (!actor->attached || (!actor->inside && !actor->vehicle_view)) ClearOwner(input.object);
    if (input.id == kHumanoidSpeed) {
      actor->speed = input.value;
      actor->speed_known = active_ && effects_permitted && input.value >= 0 && actor->attached && actor->inside;
    }
    if (input.id == kAmmo && input.value >= 0) {
      if (active_ && effects_permitted && actor->attached && actor->inside && actor->first && actor->ammo_known && actor->fired && input.value > actor->ammo + .5f)
        PulseFor(input.object, input.milliseconds, 90, {.04f,.16f,.18f,0});
      actor->ammo = input.value; actor->ammo_known = true;
    }
    if (input.id == kTriggerOn && input.value == 0) StopHold(input.object);
    if (effects_permitted && ((input.id == kTriggerOn && input.value > 0) || (input.id == kTriggerDuration && input.value > 0)) && actor->attached && actor->inside)
      for (auto& hold : held_) if (hold.object == input.object) hold.deadline = input.milliseconds + 500;
    return;
  }
  if (input.kind == OnFootInputKind::Switch) {
    if (input.result == 1 && input.id == kFirstPerson) {
      const bool first = input.state == kFirst, vehicle = input.state == kThird;
      if (actor->first != first || actor->vehicle_view != vehicle) ClearOwner(input.object);
      actor->first = first; actor->vehicle_view = vehicle;
    }
    return;
  }
  // Mounted Scorpion weapons explicitly use Third and attached=1. The saved
  // gun/rack emitters do not receive an on-foot inside=1 update. Do not relax
  // the on-foot gate, or accept the separate projectile/flyby event stream.
  if (input.kind == OnFootInputKind::Post && (input.id == 476288594 || input.id == 313889439)) {
    if (!input.playing || !active_ || !actor->attached || !actor->vehicle_view) { ++stats_.rejected; return; }
    if (input.id == 476288594) PulseFor(input.object,input.milliseconds,60,{.04f,.07f,0,.26f});
    else if (Cue(input.milliseconds,7)) PulseFor(input.object,input.milliseconds,220,{.40f,.12f,.60f,0},25,35);
    ++stats_.accepted; return;
  }
  if (input.kind != OnFootInputKind::Post || !input.playing || !active_ || !actor->attached || !actor->inside) {
    if (input.kind == OnFootInputKind::Post) ++stats_.rejected;
    return;
  }
  const auto now = input.milliseconds;
  if (input.id == 3666194424) { StartJetpack(input.object,now); ++stats_.accepted; return; }
  if (input.id == 3666194431) { StopJetpack(input.object,now); ++stats_.accepted; return; }
  for (const auto& route : kOnFootWeaponRoutes) if (route.id == input.id) {
    if (!route.start) { StopHold(input.object); ++stats_.accepted; return; }
    if (!actor->first) { ++stats_.rejected; return; }
    if (actor->started && (now < actor->last_start || now - actor->last_start < 20)) { ++stats_.duplicate_cues; return; }
    actor->started = true; actor->last_start = now; actor->fired = true;
    const auto& pattern = kOnFootWeapons[route.pattern];
    PulseFor(input.object, now, pattern.duration, pattern.onset);
    if (pattern.held) for (auto& hold : held_) if (!hold.object || hold.object == input.object) {
      hold = {input.object, now + 1500, pattern.sustain}; break;
    }
    ++stats_.accepted; return;
  }
  HapticFrame value{}; unsigned group = 0; std::uint64_t duration = 0;
  switch (input.id) {
    case 63371630: case 63371632: case 2344147335: case 2344147353:
      if (!actor->speed_known || actor->speed <= kMaxWalkingSpeed) return;
      value = {.08f,.06f,0,0}; duration = 55; group = 0; break;
    case 520481603: case 520481629:
      value = {.08f,.12f,0,0}; duration = 70; group = 1; break;
    case 3753075143: case 3753075161:
      value = {.30f,.16f,0,0}; duration = 150; group = 2; break;
    case 2315600837:
      value = {.12f,.22f,0,.18f}; duration = 110; group = 3; break;
    case 3062143116:
      value = {.08f,.12f,.06f,.06f}; duration = 150; group = 4; break;
    case 2864559845:
      value = {.12f,.04f,0,0}; duration = 90; group = 4; break;
    case 3211071105:
      if (!actor->first) { ++stats_.rejected; return; }
      value = {.04f,.16f,.18f,0}; duration = 100; group = 5; break;
    case 3327649352:
      if (!actor->first) { ++stats_.rejected; return; }
      value = {.03f,.10f,.10f,0}; duration = 70; group = 5; break;
    case 2565929944: case 1130101521:
      value = {.05f,.12f,.08f,0}; duration = 120; group = 6; break;
    default: return;
  }
  if (Cue(now, group)) { PulseFor(input.object, now, duration, value); ++stats_.accepted; }
}
HapticFrame OnFootHaptics::Sample(std::uint64_t now) noexcept {
  if (!active_) return {};
  HapticFrame result{};
  for (auto& pulse : pulses_) {
    if (pulse.end <= now) { pulse = {}; continue; }
    if (now >= pulse.start && pulse.end > pulse.start) {
      const auto elapsed = now-pulse.start, duration = pulse.end-pulse.start;
      float gain = static_cast<float>(pulse.end-now)/static_cast<float>(duration);
      if (pulse.attack && pulse.attack+pulse.plateau < duration) {
        if (elapsed < pulse.attack) gain = static_cast<float>(elapsed)/static_cast<float>(pulse.attack);
        else if (elapsed < pulse.attack+pulse.plateau) gain = 1;
        else gain = static_cast<float>(pulse.end-now)/static_cast<float>(duration-pulse.attack-pulse.plateau);
      }
      result = HapticMaximum(result,HapticScale(pulse.peak,gain));
    }
  }
  for (auto& hold : held_) {
    if (hold.object && now >= hold.deadline) { hold = {}; ++stats_.expired_holds; }
    if (hold.object) result = HapticMaximum(result, hold.value);
  }
  for (auto& jetpack : jetpacks_) if (jetpack.object) {
    if (now >= jetpack.deadline) { jetpack = {}; ++stats_.expired_holds; continue; }
    if (jetpack.stopping && now >= jetpack.cutoff && now-jetpack.cutoff >= 100) { jetpack = {}; continue; }
    result = HapticMaximum(result,JetpackValue(jetpack,now));
  }
  return result;
}
}  // namespace edtrace
