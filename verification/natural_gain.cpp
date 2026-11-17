#include "edtrace/natural_gain_control.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
void Check(bool condition, const char* message) {
  if (!condition) { std::fprintf(stderr, "FAIL natural gain: %s\n", message); std::exit(1); }
}
struct Engine {
  edtrace::MotionGainState state{0x12340000, 373542019U, -96.0f, 0.0f, true};
  unsigned loads = 0, posts = 0;
  bool collision = false, load_ok = true, queue_ok = true, defer = false;
  std::uint32_t pending = 0;
  float ordinary_audio_gain = 1.0f;
  static bool Read(void* context, edtrace::MotionGainState& state) noexcept { state = static_cast<Engine*>(context)->state; return true; }
  static bool Collision(void* context) noexcept { return !static_cast<Engine*>(context)->collision; }
  static bool Load(void* context) noexcept { auto& self = *static_cast<Engine*>(context); ++self.loads; return self.load_ok; }
  void Complete(std::uint32_t event) noexcept {
    if (event == 2923994614U) state.global_delta = 96.0f;
    else if (event == 158924609U) state.global_delta = 0.0f;
    else Check(false, "only the two diagnostic SDK events may be posted");
  }
  static std::uint32_t Post(void* context, std::uint32_t event) noexcept {
    auto& self = *static_cast<Engine*>(context); ++self.posts;
    if (!self.queue_ok) return 0;
    if (self.defer) self.pending = event; else self.Complete(event);
    return 23;
  }
  edtrace::MotionGainApi Api() { return {this, Read, Collision, Load, Post}; }
};
}
int main() {
  using namespace edtrace;
  { Engine engine; MotionGainControl control(true, engine.Api());
    Check(control.Prepare() && engine.loads == 1 && engine.posts == 0 && engine.state.global_delta == 0.0f,
          "preparing a diagnostic bank alone changes no volume and plays no sound"); }
  { Engine engine; MotionGainControl control(true, engine.Api());
    Check(control.Enable() && control.Observe(), "accepted native control reaches readback");
    Check(engine.state.authored == -96.0f && engine.state.global_delta == 96.0f && engine.ordinary_audio_gain == 1.0f,
          "native control adds96 only to Motion SIS without editing authored defaults or ordinary audio");
    Check(control.Enable() && engine.posts == 1, "enable is idempotent rather than accumulating offsets");
    Check(control.Restore() && control.Observe() && engine.state.global_delta == 0.0f && !control.owns_change(),
          "restore returns the exact initial Motion contribution");
    Check(control.Restore() && engine.posts == 2 && engine.loads == 1, "restored control posts no extra command"); }
  { Engine engine; engine.defer = true; MotionGainControl control(true, engine.Api());
    Check(control.Enable() && !control.Observe(), "accepted event is not mistaken for applied volume");
    Check(!control.Restore() && control.owns_change() && engine.posts == 1, "pending enable cannot be falsely declared restored before dispatch");
    engine.Complete(engine.pending); Check(control.Observe() && control.Restore(), "restore queues only after enable is observed");
    Check(!control.Observe() && control.owns_change(), "restore admission alone does not prove restored value");
    engine.Complete(engine.pending); Check(control.Observe() && !control.owns_change(), "deferred restore readback is required"); }
  { Engine engine; MotionGainControl unknown(false, engine.Api()); Check(!unknown.Enable() && engine.loads == 0 && engine.posts == 0,
      "unknown build cannot load a diagnostic bank or change volume"); }
  { Engine engine; engine.state.global_delta = 12.0f; MotionGainControl control(true, engine.Api());
    Check(!control.Enable() && engine.loads == 0 && engine.posts == 0, "preserve a pre-existing runtime override"); }
  { Engine engine; engine.collision = true; MotionGainControl control(true, engine.Api());
    Check(!control.Enable() && engine.loads == 0 && engine.posts == 0, "ID collision is rejected before any SDK mutation"); }
  { Engine engine; engine.load_ok = false; MotionGainControl control(true, engine.Api());
    Check(!control.Enable() && engine.posts == 0 && engine.state.global_delta == 0.0f, "failed bank load never posts a volume event"); }
  { Engine engine; engine.queue_ok = false; MotionGainControl control(true, engine.Api());
    Check(!control.Enable() && engine.state.global_delta == 0.0f && !control.owns_change(), "rejected event cannot be reported as enabled"); }
  { Engine engine; MotionGainControl control(true, engine.Api()); Check(control.Enable() && control.Observe(), "prepare external-change case");
    engine.state.global_delta = 7.0f;
    Check(!control.Restore() && engine.posts == 1 && engine.state.global_delta == 7.0f, "do not overwrite a later external volume change"); }
  { Engine engine; engine.state.unmodulated_root = false;
    MotionGainControl diagnostic(true, engine.Api());
    Check(!diagnostic.Enable() && engine.posts == 0, "diagnostic comparison still requires neutral contextual states");
    MotionGainControl session(true, engine.Api(), true);
    Check(session.Enable() && session.Observe() && engine.state.global_delta == 96.0f && !engine.state.unmodulated_root,
          "session gain preserves contextual native state gates");
    for (unsigned frame = 0; frame < 10000; ++frame) Check(session.Observe() && session.Enable(), "session enable remains idempotent");
    Check(engine.posts == 1 && session.owns_change(), "session holds one independent override until explicit restore");
    Check(session.Restore() && session.Observe() && engine.state.global_delta == 0.0f && !engine.state.unmodulated_root,
          "session restores only its SIS contribution with contextual gates retained"); }
  std::puts("PASS native gain enable/restore, deferred readback, profile/override/collision/load/queue guards and external ownership");
}
