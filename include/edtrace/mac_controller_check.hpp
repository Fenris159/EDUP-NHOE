#pragma once

namespace edtrace::macbridge {
// Paired controllers can arrive after the app finishes launching. Let the
// application run loop deliver GameController work before each snapshot.
inline constexpr unsigned kControllerCheckAttempts = 24;
inline constexpr double kControllerCheckIntervalSeconds = 0.125;

template <typename Activate, typename PumpEvents, typename ReadControllers>
auto CheckConnectedControllers(Activate activate, PumpEvents pump, ReadControllers read) {
  activate();
  auto snapshot = read();
  for (unsigned attempt = 0; attempt < kControllerCheckAttempts; ++attempt) {
    pump(kControllerCheckIntervalSeconds);
    snapshot = read();
  }
  return snapshot;
}
}  // namespace edtrace::macbridge
