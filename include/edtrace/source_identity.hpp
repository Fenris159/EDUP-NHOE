#pragma once
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <limits>

namespace edtrace {
enum class SourceIdentityStatus { Known, ReadFault, UnsupportedContext, Changed };
struct SourceIdentity {
  SourceIdentityStatus status = SourceIdentityStatus::ReadFault;
  std::uintptr_t context = 0, pbi = 0, sound = 0;
  std::uintptr_t game_object = 0;
  std::uint32_t sound_id = 0;
  std::uint64_t game_object_id = 0;
  float volume_db = 0, mute_fade_ratio = 0;
};

// Read-only layout from the exact game's standard IAkSourcePluginContext.
// The caller has independently verified the executable. Other context types
// remain unknown; a missing/changed read never establishes source absence.
template <typename Reader>
SourceIdentity ObserveSourceIdentity(std::uintptr_t instance, std::uintptr_t image, Reader&& read) noexcept {
  SourceIdentity result;
  auto pointer = [](std::uintptr_t value) { return value >= 65536 && value <= 0x7fffffffffffULL && value % 8 == 0; };
  auto field = [&](std::uintptr_t base, std::uintptr_t offset, auto& value) {
    return base <= std::numeric_limits<std::uintptr_t>::max() - offset &&
           read(base + offset, &value, sizeof(value));
  };
  if (!pointer(instance) || !image || image > std::numeric_limits<std::uintptr_t>::max() - 0x5a86310 ||
      !field(instance, 0x70, result.context) || !pointer(result.context) || result.context < 0x28) return result;
  const auto wrapper = result.context - 0x28;
  std::uintptr_t primary = 0, context_vtable = 0, companion = 0, source_vtable = 0, backreference = 0;
  if (!field(wrapper, 0, primary) || !field(result.context, 0, context_vtable) ||
      !field(result.context, 8, companion) || !field(instance, 0, source_vtable) || !field(wrapper, 0xc0, backreference)) return result;
  if (primary != image + 0x57eb868 || context_vtable != image + 0x57eb970 || companion != image + 0x57eba10 ||
      source_vtable != image + 0x5a86310 || backreference != instance) {
    result.status = SourceIdentityStatus::UnsupportedContext;
    return result;
  }
  if (!field(wrapper, 0x18, result.pbi) || !pointer(result.pbi) ||
      !field(result.pbi, 0x180, result.sound) || !pointer(result.sound) ||
      !field(result.sound, 0x10, result.sound_id) ||
      !field(result.pbi, 0x28, result.game_object) || !pointer(result.game_object) ||
      !field(result.game_object, 0x48, result.game_object_id) || !field(result.pbi, 0x78, result.volume_db) ||
      !field(result.pbi, 0xe0, result.mute_fade_ratio) || !std::isfinite(result.volume_db) || !std::isfinite(result.mute_fade_ratio)) return result;
  std::uintptr_t pbi_context_vtable = 0, sound_vtable = 0;
  if (!field(result.pbi, 0x18, pbi_context_vtable) || !field(result.sound, 0, sound_vtable)) return result;
  if (pbi_context_vtable != image + 0x57e7ab8 || sound_vtable != image + 0x57e7ec0) {
    result.status = SourceIdentityStatus::UnsupportedContext;
    return result;
  }
  std::uintptr_t context2 = 0, primary2 = 0, context_vtable2 = 0, companion2 = 0, pbi2 = 0, sound2 = 0;
  std::uintptr_t source_vtable2 = 0, backreference2 = 0, pbi_context_vtable2 = 0, sound_vtable2 = 0, game_object2 = 0;
  std::uint32_t sound_id2 = 0;
  std::uint64_t game_object_id2 = 0;
  float volume2 = 0, fade2 = 0;
  if (!field(instance, 0x70, context2) || !field(wrapper, 0, primary2) || !field(result.context, 0, context_vtable2) ||
      !field(result.context, 8, companion2) || !field(wrapper, 0x18, pbi2) || !field(result.pbi, 0x180, sound2) ||
      !field(result.sound, 0x10, sound_id2) ||
      !field(instance, 0, source_vtable2) || !field(wrapper, 0xc0, backreference2) ||
      !field(result.pbi, 0x18, pbi_context_vtable2) || !field(result.sound, 0, sound_vtable2) ||
      !field(result.pbi, 0x28, game_object2) || !field(result.game_object, 0x48, game_object_id2) ||
      !field(result.pbi, 0x78, volume2) || !field(result.pbi, 0xe0, fade2)) return result;
  result.status = context2 == result.context && primary2 == primary && context_vtable2 == context_vtable &&
                  companion2 == companion && pbi2 == result.pbi && sound2 == result.sound &&
                  sound_id2 == result.sound_id &&
                  source_vtable2 == source_vtable && backreference2 == backreference &&
                  pbi_context_vtable2 == pbi_context_vtable && sound_vtable2 == sound_vtable &&
                  game_object2 == result.game_object && game_object_id2 == result.game_object_id &&
                  volume2 == result.volume_db && fade2 == result.mute_fade_ratio ?
                  SourceIdentityStatus::Known : SourceIdentityStatus::Changed;
  return result;
}
}  // namespace edtrace
