#pragma once

#include <cstddef>
#include <cstdint>

namespace edtrace {

inline constexpr char kGameSha256[] = "ed5a8f8cd10f9a80d3bda5badd7ba1d4fcfc15996c8c7e018852cb170e264b63";
inline constexpr std::uintptr_t kEngineReturnRva = 0x5c8535;
inline constexpr std::uintptr_t kWwiseReturnRva = 0x4d05312;
inline constexpr std::uintptr_t kWwiseRetryReturnRva = 0x4d05395;

enum class Writer { UnknownBuild, UnknownCaller, Engine, WwiseShared, WwiseTerminationRetry, DiagnosticPreflight, OnFoot };

constexpr unsigned long RoutedIndex(bool enabled, Writer writer, unsigned long original) noexcept {
  return enabled && original == 1 && (writer == Writer::WwiseShared || writer == Writer::WwiseTerminationRetry) ? 0 : original;
}

constexpr Writer Classify(bool profile_matches, bool inside_main_image, std::uintptr_t return_rva) noexcept {
  if (!inside_main_image) return Writer::UnknownCaller;
  if (!profile_matches) return Writer::UnknownBuild;
  switch (return_rva) {
    case kEngineReturnRva: return Writer::Engine;
    case kWwiseReturnRva: return Writer::WwiseShared;
    case kWwiseRetryReturnRva: return Writer::WwiseTerminationRetry;
    default: return Writer::UnknownCaller;
  }
}

constexpr const char* WriterName(Writer writer) noexcept {
  switch (writer) {
    case Writer::Engine: return "engine";
    case Writer::WwiseShared: return "wwise_shared";
    case Writer::WwiseTerminationRetry: return "wwise_termination_retry";
    case Writer::DiagnosticPreflight: return "diagnostic_preflight";
    case Writer::OnFoot: return "onfoot";
    case Writer::UnknownBuild: return "unknown_build";
    default: return "unknown_caller";
  }
}

inline const char* Phase(Writer writer, const std::uintptr_t* ancestors, std::size_t count) noexcept {
  if (writer == Writer::DiagnosticPreflight) return "diagnostic_pulse_or_stop";
  if (writer == Writer::WwiseTerminationRetry) return "termination_retry";
  if (writer == Writer::Engine) return "engine_update";
  if (writer != Writer::WwiseShared) return "unknown";
  for (std::size_t i = 0; i < count; ++i) {
    if (ancestors[i] == 0x4d05363) return "termination_initial";
    if (ancestors[i] == 0x4d05cc8) return "render_or_idle";
  }
  return "shared_sender";
}

}  // namespace edtrace
