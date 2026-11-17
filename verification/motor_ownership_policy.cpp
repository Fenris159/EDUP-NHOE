#include "edtrace/motor_ownership_policy.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
using edtrace::MotorOwnershipPolicy;
using edtrace::MotorOwnershipRequest;
using edtrace::OwnershipSource;
void Check(bool condition, const char* reason) {
  if (!condition) { std::cerr << "FAIL ownership: " << reason << '\n'; std::exit(1); }
}
MotorOwnershipRequest Request(OwnershipSource source, std::uint16_t left = 0,
                              std::uint16_t right = 0) {
  return {true, true, true, 0, source, left, right};
}
void Forward(MotorOwnershipPolicy& policy, const MotorOwnershipRequest& request,
             std::uint64_t start, std::uint32_t result, std::uint64_t end) {
  Check(!policy.ShouldSuppress(request, start), "non-eligible request must forward");
  policy.Complete(request, result, end);
}
void Acquire(MotorOwnershipPolicy& policy, std::uint64_t time = 100) {
  Forward(policy, Request(OwnershipSource::Wwise, 1234, 5678), time, 0, time);
}
void LeaseAndWords() {
  MotorOwnershipPolicy policy(0);
  const auto maximum = Request(OwnershipSource::Wwise, 65535, 65535);
  Forward(policy, maximum, 100, 0, 104);
  const auto zero = Request(OwnershipSource::Engine);
  Check(policy.ShouldSuppress(zero, 104), "successful Wwise nonzero begins lease on completion");
  Check(policy.ShouldSuppress(zero, 203), "lease remains fresh at99ms");
  Check(!policy.ShouldSuppress(zero, 204), "lease expires exactly at100ms without engine-zero renewal");
  Check(!policy.ShouldSuppress(zero, 205), "expired ownership does not return without a successful effect");
  Check(maximum.left == 65535 && maximum.right == 65535, "in-range motor words remain unchanged");
  Forward(policy, Request(OwnershipSource::Wwise, 0, 1), 210, 0, 210);
  Check(policy.ShouldSuppress(zero, 211), "right motor alone can acquire ownership");
  Forward(policy, Request(OwnershipSource::Wwise, 1, 0), 220, 0, 220);
  Check(policy.ShouldSuppress(zero, 319) && !policy.ShouldSuppress(zero, 320), "later success renews the bounded lease");
}
void StopsAndErrors() {
  const std::array<MotorOwnershipRequest, 3> releases{{Request(OwnershipSource::Wwise),
      Request(OwnershipSource::WwiseRetry), Request(OwnershipSource::WwiseRetry, 12, 34)}};
  for (const auto& release : releases) {
    MotorOwnershipPolicy policy(0); Acquire(policy);
    Forward(policy, release, 110, 0, 110);
    Check(!policy.ShouldSuppress(Request(OwnershipSource::Engine), 111), "Wwise zero and every retry forward and release ownership");
  }
  MotorOwnershipPolicy policy(0); Acquire(policy);
  Forward(policy, Request(OwnershipSource::Wwise, 1, 2), 120, 1167, 121);
  Check(!policy.ShouldSuppress(Request(OwnershipSource::Engine), 122), "failed Wwise call releases the previous lease");
  MotorOwnershipPolicy failed(0);
  Forward(failed, Request(OwnershipSource::Wwise, 1, 2), 10, 170, 11);
  Check(!failed.ShouldSuppress(Request(OwnershipSource::Engine), 12), "failed effect cannot acquire ownership");
}
void GuardsAndEngineOutput() {
  auto disabled = Request(OwnershipSource::Engine); disabled.enabled = false;
  auto unverified = Request(OwnershipSource::Engine); unverified.verified = false;
  auto unreadable = Request(OwnershipSource::Engine); unreadable.known_request = false;
  const std::array<MotorOwnershipRequest, 5> releases{{disabled, unverified, unreadable,
      Request(OwnershipSource::Other), Request(OwnershipSource::Engine, 65535, 17)}};
  for (const auto& release : releases) {
    MotorOwnershipPolicy policy(0); Acquire(policy);
    Forward(policy, release, 110, 0, 110);
    Check(!policy.ShouldSuppress(Request(OwnershipSource::Engine), 111), "guards, unknown writers and engine nonzero release and forward");
  }
  for (auto denied : std::array<MotorOwnershipRequest, 3>{{disabled, unverified, unreadable}}) {
    denied.source = OwnershipSource::Wwise; denied.left = 1;
    MotorOwnershipPolicy policy(0); Forward(policy, denied, 100, 0, 100);
    Check(!policy.ShouldSuppress(Request(OwnershipSource::Engine), 101), "disabled, unknown-build or unreadable Wwise cannot acquire");
  }
}
void OtherControllersAndClocks() {
  MotorOwnershipPolicy policy(0); Acquire(policy);
  auto other = Request(OwnershipSource::Other); other.controller = 3;
  other.enabled = false; other.verified = false; other.known_request = false;
  Forward(policy, other, 1, 1167, 2);
  other = Request(OwnershipSource::Wwise, 1, 2); other.controller = 2;
  Forward(policy, other, 99999, 0, 99999);
  Check(policy.ShouldSuppress(Request(OwnershipSource::Engine), 150), "other controllers never release, extend or rewind target ownership");
  MotorOwnershipPolicy configured(4);
  auto target = Request(OwnershipSource::Wwise, 1, 2); target.controller = 4;
  Forward(configured, target, 100, 0, 100);
  auto engine = Request(OwnershipSource::Engine); engine.controller = 4;
  Check(configured.ShouldSuppress(engine, 101), "ownership applies to exactly the configured physical controller");
  MotorOwnershipPolicy backwards(0); Acquire(backwards);
  Check(!backwards.ShouldSuppress(Request(OwnershipSource::Engine), 99), "backwards decision clock fails open");
  Check(!backwards.ShouldSuppress(Request(OwnershipSource::Engine), 100), "backwards clock released the prior lease");
  Forward(backwards, Request(OwnershipSource::Wwise, 1, 2), 101, 0, 101);
  Check(backwards.ShouldSuppress(Request(OwnershipSource::Engine), 102), "fresh monotonic success can reacquire after a clock fault");
  Forward(backwards, Request(OwnershipSource::Wwise, 1, 2), 110, 0, 109);
  Check(!backwards.ShouldSuppress(Request(OwnershipSource::Engine), 111), "backwards completion cannot acquire or preserve a lease");
  MotorOwnershipPolicy overflow(0);
  constexpr auto last = std::numeric_limits<std::uint64_t>::max();
  Acquire(overflow, last - 50);
  Check(overflow.ShouldSuppress(Request(OwnershipSource::Engine), last), "lease comparison does not overflow near uint64 maximum");
  Check(!overflow.ShouldSuppress(Request(OwnershipSource::Engine), 0), "wrapped clock fails open");
}
void CompletionEvidence() {
  MotorOwnershipPolicy policy(0);
  const auto effect = Request(OwnershipSource::Wwise, 1, 2);
  policy.Complete(effect, 0, 100);
  Check(!policy.ShouldSuppress(Request(OwnershipSource::Engine), 101), "success without matching decision cannot acquire ownership");
  Check(!policy.ShouldSuppress(effect, 110), "Wwise request always forwards before success is known");
  Check(!policy.ShouldSuppress(Request(OwnershipSource::Engine), 110), "request admission alone is not successful output evidence");
  MotorOwnershipPolicy mismatch(0);
  Check(!mismatch.ShouldSuppress(effect, 100), "candidate effect forwards");
  mismatch.Complete(Request(OwnershipSource::Wwise, 3, 4), 0, 100);
  Check(!mismatch.ShouldSuppress(Request(OwnershipSource::Engine), 101), "changed completion request cannot acquire");
}
}  // namespace
int main() {
  LeaseAndWords(); StopsAndErrors(); GuardsAndEngineOutput(); OtherControllersAndClocks(); CompletionEvidence();
  std::cout << "PASS ownership bounded lease, stop preservation, guards, errors, controller isolation and clocks\n";
  return 0;
}
