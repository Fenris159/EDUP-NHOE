#include "edtrace/mac_controller_check.hpp"

#include <cstdio>

int main() {
  using edtrace::macbridge::CheckConnectedControllers;
  bool active = false;
  unsigned ticks = 0;
  const auto visible = CheckConnectedControllers([&] { active = true; }, [&](double) { if (active) ++ticks; }, [&] {
    return ticks >= 3 ? 1u : 0u;
  });
  if (visible != 1) {
    std::fputs("FAIL: a paired controller appeared late and the checker reported zero\n", stderr);
    return 1;
  }
  ticks = 0;
  const auto multiple = CheckConnectedControllers([] {}, [&](double) { ++ticks; }, [&] {
    return ticks >= 10 ? 2u : 1u;
  });
  if (multiple != 2) {
    std::fputs("FAIL: a second controller can appear during discovery\n", stderr);
    return 1;
  }
  unsigned empty_reads = 0;
  double wait_seconds = 0;
  CheckConnectedControllers([] {}, [&](double interval) { wait_seconds += interval; }, [&] {
    ++empty_reads;
    return 0;
  });
  if (empty_reads != 25 || wait_seconds != 3.0) {
    std::fputs("FAIL: an empty controller list did not finish within the expected scan window\n", stderr);
    return 1;
  }
  std::puts("PASS paired controller after app activation and bounded empty scan");
}
