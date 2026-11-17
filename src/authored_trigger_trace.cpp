#include "edtrace/authored_trigger_trace.hpp"
#include "edtrace/source_identity.hpp"
#include "MinHook.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace edtrace {
namespace {
using U32 = std::uint32_t;
using U64 = std::uint64_t;
using Address = std::uintptr_t;
enum class AkCallbackType : std::int32_t {};
struct AkCallbackInfo;
struct AkExternalSourceInfo;
using Callback = void (WINAPI*)(AkCallbackType, AkCallbackInfo*);
using PostFn = U32 (WINAPI*)(U32, U64, U32, Callback, void*, U32, AkExternalSourceInfo*, U32);
using StateFn = std::int32_t (WINAPI*)(U32, U32);
using ApplyFn = void (WINAPI*)(void*, U32, U32, std::uint8_t, std::uint8_t);
using InitFn = std::int32_t (WINAPI*)(void*, void*, void*, void*, void*);
using ExecuteFn = void (WINAPI*)(void*, void*);
static_assert(sizeof(void*) == 8 && std::atomic<U64>::is_always_lock_free, "Windows x64 trace only");
static_assert(std::atomic<PostFn>::is_always_lock_free && std::atomic<StateFn>::is_always_lock_free &&
              std::atomic<ApplyFn>::is_always_lock_free && std::atomic<InitFn>::is_always_lock_free &&
              std::atomic<ExecuteFn>::is_always_lock_free,
              "Hooks require lock-free trampoline pointers");

constexpr std::size_t kCapacity = 1024;
constexpr U64 kMaxBytes = 16ULL * 1024 * 1024;
constexpr std::array<Address, 5> kRvas{{0x4a236c0, 0x4a28fd0, 0x4a3d690, 0x4d03950, 0x4d035f0}};
constexpr std::array<std::array<unsigned char, 32>, 5> kSignatures{{
    {{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x4c,0x89,0x74,0x24,0x20,0x41,0x57,0x48,0x83,0xec,0x60,0x45,0x33,0xd2,0x49,0x8b,0xf1}},
    {{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x8b,0xfa,0x8b,0xd9,0xe8,0xcd,0x98,0x7c,0xfb,0x48,0x8b,0x0d,0x86,0x57,0x62,0x01,0xba,0x07,0x00,0x00,0x00,0x44}},
    {{0x44,0x89,0x44,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xd9,0x48,0x81,0xec,0xf8,0x00,0x00,0x00,0x48,0x8b,0x01,0x45,0x0f,0xb6}},
    {{0x48,0x8b,0x54,0x24,0x28,0x4c,0x89,0x41,0x70,0x4c,0x89,0x49,0x68,0x41,0x0f,0xb6,0x81,0x90,0x00,0x00,0x00,0x88,0x42,0x04,0x81,0x62,0x04,0xff,0x00,0x00,0x00,0x8b}},
    {{0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x78,0xc7,0x42,0x0c,0x2d,0x00,0x00,0x00,0x4c,0x8b,0xf1,0x48,0x8b,0x49,0x68,0x0f,0x57,0xc9,0x4c,0x89,0x64,0x24,0x68,0x4c,0x8b}}
}};
enum class Kind : U32 { Post, StateRequest, StateManager, SourceInit, SourceExecute };
enum class ReadStatus : U32 { NotRead, Known, ReadFault, Invalid, NotFound, Changed };
struct StateObservation { ReadStatus status = ReadStatus::NotRead; U32 value = 0; };
enum class AudioStatus : U32 { NotRead, Known, ReadFault, Invalid, Empty, NonFinite };
struct AudioObservation {
  AudioStatus status = AudioStatus::NotRead;
  U32 channel_config = 0, channel_count = 0;
  std::uint16_t max_frames = 0, valid_frames = 0;
  std::array<float, 4> peaks{};
  std::array<U32, 4> nonzero{};
};
struct Record {
  Kind kind = Kind::Post;
  U64 sequence = 0;
  LONGLONG start = 0, end = 0;
  DWORD thread = 0, incoming_error = 0, outgoing_error = 0;
  U32 event = 0, playing = 0, group = 0, state = 0;
  U64 game_object = 0;
  std::int32_t result = 0;
  std::uint8_t flag4 = 0, flag5 = 0;
  Address self = 0, context = 0, parameters = 0, buffer = 0;
  StateObservation before{}, after{};
  SourceIdentity identity{};
  AudioObservation audio{};
};
struct Slot { std::atomic<std::size_t> sequence{0}; Record record{}; };
std::array<Slot, kCapacity> queue{};
std::atomic<std::size_t> write_position{0};
std::size_t read_position = 0;
std::atomic<U64> sequence{0}, active{0};
std::atomic<U64> post_count{0}, state_count{0}, apply_count{0}, init_count{0}, unknown_identity{0};
std::atomic<U64> execute_count{0}, unknown_audio{0};
std::atomic<U64> written{0}, queue_drops{0}, storage_drops{0};
std::atomic<DWORD> installation_error{0}, logging_error{0}, hook_mask{0};
std::atomic<bool> capture_open{false}, started{false}, stopped{false};
std::atomic<PostFn> original_post{nullptr};
std::atomic<StateFn> original_state{nullptr};
std::atomic<ApplyFn> original_apply{nullptr};
std::atomic<InitFn> original_init{nullptr};
std::atomic<ExecuteFn> original_execute{nullptr};
SRWLOCK lifecycle = SRWLOCK_INIT;
HANDLE stop_event = nullptr, worker = nullptr, file = INVALID_HANDLE_VALUE;
Address image_base = 0;
std::atomic<bool> controlled_fixture{false};
std::array<void*, 5> targets{};
LARGE_INTEGER qpc_frequency{}, anchor_qpc_before{}, anchor_qpc_after{};
U64 anchor_filetime = 0;
ObservationWindow observation_window;
U64 MonotonicMs() noexcept { return GetTickCount64(); }
bool CaptureOpen() noexcept {
  if (!capture_open.load(std::memory_order_acquire)) return false;
  if (!observation_window.Expired()) return true;
  capture_open.store(false, std::memory_order_release); return false;
}
U64 bytes_written = 0;

struct CallGuard {
  CallGuard() noexcept { active.fetch_add(1, std::memory_order_acq_rel); }
  ~CallGuard() { active.fetch_sub(1, std::memory_order_release); }
};
bool IsEvent(U32 id) noexcept { return id == 1280793276 || id == 1280793275 || id == 3711367895 || id == 3711367888; }
bool IsGroup(U32 id) noexcept { return id == 4021597691 || id == 1391855890; }
bool IsSound(U32 id) noexcept { return id == 786626383 || id == 974846344; }
bool ReadBytes(Address address, void* out, std::size_t size) noexcept {
  if (address < 65536 || address > std::numeric_limits<Address>::max() - size) return false;
  SIZE_T count = 0;
  return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), out, size, &count) && count == size;
}
template <typename T> bool ReadAt(Address base, Address offset, T& value) noexcept {
  return base <= std::numeric_limits<Address>::max() - offset && ReadBytes(base + offset, &value, sizeof(value));
}
StateObservation ObserveState(Address manager, U32 group) noexcept {
  struct Entry { U32 group, padding; Address state; };
  static_assert(sizeof(Entry) == 16, "Exact-build state manager entry");
  std::array<Entry, 512> entries{};
  Address array = 0; U32 count = 0;
  if (!ReadAt(manager, 0, array) || !ReadAt(manager, 8, count)) return {ReadStatus::ReadFault, 0};
  if (!array || array % 8 || !count || count > entries.size()) return {ReadStatus::Invalid, 0};
  if (!ReadBytes(array, entries.data(), count * sizeof(Entry))) return {ReadStatus::ReadFault, 0};
  std::size_t match = entries.size();
  for (U32 i = 0; i < count; ++i) {
    if (entries[i].group != group) continue;
    if (match != entries.size()) return {ReadStatus::Invalid, 0};
    match = i;
  }
  if (match == entries.size()) return {ReadStatus::NotFound, 0};
  U32 value = 0, value2 = 0, count2 = 0; Address array2 = 0; Entry entry2{};
  const auto state_pointer = entries[match].state;
  if (!state_pointer || state_pointer % 8 || !ReadAt(state_pointer, 4, value) ||
      !ReadBytes(array + match * sizeof(Entry), &entry2, sizeof(entry2)) ||
      !ReadAt(state_pointer, 4, value2) || !ReadAt(manager, 0, array2) || !ReadAt(manager, 8, count2))
    return {ReadStatus::ReadFault, 0};
  if (array != array2 || count != count2 || entry2.group != group || entry2.state != state_pointer || value != value2)
    return {ReadStatus::Changed, 0};
  return {ReadStatus::Known, value};
}
AudioObservation ObserveAudio(Address buffer) noexcept {
  struct Header {
    Address data;
    U32 config, state;
    std::uint16_t max_frames, valid_frames;
  } header{};
  static_assert(offsetof(Header, max_frames) == 0x10 && offsetof(Header, valid_frames) == 0x12);
  AudioObservation audio;
  if (!ReadBytes(buffer, &header, 0x14)) { audio.status = AudioStatus::ReadFault; return audio; }
  audio.channel_config = header.config; audio.channel_count = header.config & 0xff;
  audio.max_frames = header.max_frames; audio.valid_frames = header.valid_frames;
  if (!audio.channel_count || audio.channel_count > 4 || !header.max_frames || header.max_frames > 4096 ||
      header.valid_frames > header.max_frames) { audio.status = AudioStatus::Invalid; return audio; }
  if (!header.valid_frames) { audio.status = AudioStatus::Empty; return audio; }
  if (!header.data || header.data % alignof(float)) { audio.status = AudioStatus::Invalid; return audio; }
  std::array<float, 4096> samples{};
  for (U32 channel = 0; channel < audio.channel_count; ++channel) {
    const auto offset = static_cast<Address>(channel) * header.max_frames * sizeof(float);
    if (header.data > std::numeric_limits<Address>::max() - offset ||
        !ReadBytes(header.data + offset, samples.data(), header.valid_frames * sizeof(float))) {
      audio.status = AudioStatus::ReadFault; return audio;
    }
    for (std::uint16_t frame = 0; frame < header.valid_frames; ++frame) {
      const float sample = samples[frame];
      if (!std::isfinite(sample)) { audio.status = AudioStatus::NonFinite; return audio; }
      const float magnitude = std::fabs(sample);
      if (magnitude > audio.peaks[channel]) audio.peaks[channel] = magnitude;
      if (sample != 0.0f) ++audio.nonzero[channel];
    }
  }
  audio.status = AudioStatus::Known; return audio;
}
void Begin(Record& record, Kind kind, DWORD incoming) noexcept {
  record.kind = kind;
  LARGE_INTEGER now{}; QueryPerformanceCounter(&now); record.start = now.QuadPart;
  record.thread = GetCurrentThreadId(); record.incoming_error = incoming;
}
void Enqueue(Record& record, DWORD outgoing) noexcept {
  record.sequence = sequence.fetch_add(1, std::memory_order_relaxed) + 1;
  LARGE_INTEGER now{}; QueryPerformanceCounter(&now); record.end = now.QuadPart; record.outgoing_error = outgoing;
  auto position = write_position.load(std::memory_order_relaxed);
  for (unsigned attempt = 0; attempt < 16; ++attempt) {
    auto& slot = queue[position % kCapacity];
    const auto difference = static_cast<std::intptr_t>(slot.sequence.load(std::memory_order_acquire)) - static_cast<std::intptr_t>(position);
    if (difference == 0) {
      if (write_position.compare_exchange_weak(position, position + 1, std::memory_order_relaxed)) {
        slot.record = record; slot.sequence.store(position + 1, std::memory_order_release); return;
      }
    } else if (difference < 0) break;
    else position = write_position.load(std::memory_order_relaxed);
  }
  queue_drops.fetch_add(1, std::memory_order_relaxed);
}
bool Dequeue(Record& record) noexcept {
  auto& slot = queue[read_position % kCapacity];
  if (slot.sequence.load(std::memory_order_acquire) != read_position + 1) return false;
  record = slot.record; slot.sequence.store(read_position + kCapacity, std::memory_order_release); ++read_position; return true;
}
U32 WINAPI ObservePost(U32 event, U64 object, U32 flags, Callback callback, void* cookie,
                       U32 external_count, AkExternalSourceInfo* external, U32 playing) noexcept {
  const DWORD incoming = GetLastError(); CallGuard guard;
  const bool captured = CaptureOpen() && IsEvent(event);
  Record record{}; if (captured) Begin(record, Kind::Post, incoming);
  SetLastError(incoming);
  const U32 result = original_post.load(std::memory_order_acquire)(event, object, flags, callback, cookie, external_count, external, playing);
  const DWORD outgoing = GetLastError();
  if (captured) {
    post_count.fetch_add(1, std::memory_order_relaxed);
    record.event = event; record.game_object = object; record.playing = result; Enqueue(record, outgoing);
  }
  SetLastError(outgoing); return result;
}
std::int32_t WINAPI ObserveStateRequest(U32 group, U32 state) noexcept {
  const DWORD incoming = GetLastError(); CallGuard guard;
  const bool captured = CaptureOpen() && IsGroup(group);
  Record record{}; if (captured) Begin(record, Kind::StateRequest, incoming);
  SetLastError(incoming); const auto result = original_state.load(std::memory_order_acquire)(group, state);
  const DWORD outgoing = GetLastError();
  if (captured) {
    state_count.fetch_add(1, std::memory_order_relaxed);
    record.group = group; record.state = state; record.result = result; Enqueue(record, outgoing);
  }
  SetLastError(outgoing); return result;
}
void WINAPI ObserveApply(void* self, U32 group, U32 state, std::uint8_t flag4, std::uint8_t flag5) noexcept {
  const DWORD incoming = GetLastError(); CallGuard guard;
  const bool captured = CaptureOpen() && IsGroup(group);
  Record record{};
  if (captured) { Begin(record, Kind::StateManager, incoming); record.before = ObserveState(reinterpret_cast<Address>(self), group); }
  SetLastError(incoming); original_apply.load(std::memory_order_acquire)(self, group, state, flag4, flag5);
  const DWORD outgoing = GetLastError();
  if (captured) {
    apply_count.fetch_add(1, std::memory_order_relaxed);
    record.group = group; record.state = state; record.flag4 = flag4; record.flag5 = flag5;
    record.after = ObserveState(reinterpret_cast<Address>(self), group); Enqueue(record, outgoing);
  }
  SetLastError(outgoing);
}
std::int32_t WINAPI ObserveInit(void* self, void* allocator, void* context, void* parameters, void* format) noexcept {
  const DWORD incoming = GetLastError(); CallGuard guard;
  const bool observed = CaptureOpen();
  Record record{}; if (observed) Begin(record, Kind::SourceInit, incoming);
  SetLastError(incoming); const auto result = original_init.load(std::memory_order_acquire)(self, allocator, context, parameters, format);
  const DWORD outgoing = GetLastError();
  if (observed) {
    const auto identity = ObserveSourceIdentity(reinterpret_cast<Address>(self), image_base, ReadBytes);
    if (identity.status != SourceIdentityStatus::Known) unknown_identity.fetch_add(1, std::memory_order_relaxed);
    else if (IsSound(identity.sound_id)) {
      init_count.fetch_add(1, std::memory_order_relaxed);
      record.result = result; record.self = reinterpret_cast<Address>(self); record.context = reinterpret_cast<Address>(context);
      record.parameters = reinterpret_cast<Address>(parameters); record.identity = identity; Enqueue(record, outgoing);
    }
  }
  SetLastError(outgoing); return result;
}
void WINAPI ObserveExecute(void* self, void* buffer) noexcept {
  const DWORD incoming = GetLastError(); CallGuard guard;
  const bool observed = CaptureOpen();
  Record record{}; if (observed) Begin(record, Kind::SourceExecute, incoming);
  SetLastError(incoming); original_execute.load(std::memory_order_acquire)(self, buffer);
  const DWORD outgoing = GetLastError();
  if (observed) {
    const auto identity = ObserveSourceIdentity(reinterpret_cast<Address>(self), image_base, ReadBytes);
    if (identity.status != SourceIdentityStatus::Known) unknown_identity.fetch_add(1, std::memory_order_relaxed);
    else if (IsSound(identity.sound_id)) {
      execute_count.fetch_add(1, std::memory_order_relaxed);
      record.self = reinterpret_cast<Address>(self); record.buffer = reinterpret_cast<Address>(buffer); record.identity = identity;
      record.audio = ObserveAudio(record.buffer);
      if (record.audio.status != AudioStatus::Known && record.audio.status != AudioStatus::Empty)
        unknown_audio.fetch_add(1, std::memory_order_relaxed);
      Enqueue(record, outgoing);
    }
  }
  SetLastError(outgoing);
}
const char* Status(ReadStatus status) noexcept {
  switch (status) {
    case ReadStatus::Known: return "known";
    case ReadStatus::ReadFault: return "read_fault";
    case ReadStatus::Invalid: return "invalid";
    case ReadStatus::NotFound: return "not_found";
    case ReadStatus::Changed: return "changed";
    default: return "not_read";
  }
}
void StateJson(std::ostringstream& out, const StateObservation& state) {
  out << "{\"status\":\"" << Status(state.status) << "\",\"state_id\":";
  if (state.status == ReadStatus::Known) out << state.value; else out << "null";
  out << '}';
}
const char* Status(AudioStatus status) noexcept {
  switch (status) {
    case AudioStatus::Known: return "known";
    case AudioStatus::ReadFault: return "read_fault";
    case AudioStatus::Invalid: return "invalid";
    case AudioStatus::Empty: return "empty";
    case AudioStatus::NonFinite: return "non_finite";
    default: return "not_read";
  }
}
void AudioJson(std::ostringstream& out, const AudioObservation& audio) {
  out << "{\"status\":\"" << Status(audio.status) << "\",\"channel_config\":" << audio.channel_config
      << ",\"channel_count\":" << audio.channel_count << ",\"max_frames\":" << audio.max_frames
      << ",\"valid_frames\":" << audio.valid_frames << ",\"peaks\":[";
  for (std::size_t i = 0; i < audio.peaks.size(); ++i) {
    if (i) out << ',';
    if (audio.status == AudioStatus::Known && i < audio.channel_count) out << std::setprecision(9) << audio.peaks[i]; else out << "null";
  }
  out << "],\"nonzero\":[";
  for (std::size_t i = 0; i < audio.nonzero.size(); ++i) {
    if (i) out << ',';
    if (audio.status == AudioStatus::Known && i < audio.channel_count) out << audio.nonzero[i]; else out << "null";
  }
  out << "]}";
}
std::string Json(const Record& record) {
  std::ostringstream out;
  const char* kind = record.kind == Kind::Post ? "authored_post_event" : record.kind == Kind::StateRequest ?
                     "authored_state_request" : record.kind == Kind::StateManager ? "authored_state_manager" :
                     record.kind == Kind::SourceExecute ? "authored_trigger_source_execute" : "authored_source_init";
  out << "{\"type\":\"" << kind << "\",\"sequence\":" << record.sequence << ",\"start_qpc\":" << record.start
      << ",\"end_qpc\":" << record.end << ",\"thread_id\":" << record.thread
      << ",\"incoming_last_error\":" << record.incoming_error << ",\"outgoing_last_error\":" << record.outgoing_error;
  if (record.kind == Kind::Post) {
    out << ",\"event_id\":" << record.event << ",\"game_object_id\":" << record.game_object << ",\"returned_playing_id\":" << record.playing;
  } else if (record.kind == Kind::StateRequest || record.kind == Kind::StateManager) {
    out << ",\"group_id\":" << record.group << ",\"requested_state_id\":" << record.state;
    if (record.kind == Kind::StateRequest) out << ",\"result\":" << record.result;
    else {
      out << ",\"flag4\":" << static_cast<unsigned>(record.flag4) << ",\"flag5\":" << static_cast<unsigned>(record.flag5) << ",\"before\":";
      StateJson(out, record.before); out << ",\"after\":"; StateJson(out, record.after);
    }
  } else {
    out << ",\"instance\":" << record.self;
    if (record.kind == Kind::SourceExecute) {
      out << ",\"buffer\":" << record.buffer << ",\"sampled\":\"after_original\",\"audio\":";
      AudioJson(out, record.audio);
    } else out << ",\"result\":" << record.result << ",\"context\":" << record.context << ",\"parameters\":" << record.parameters;
    out << ",\"source_identity\":{\"status\":\"known\",\"sound_id\":" << record.identity.sound_id
        << ",\"game_object_id\":" << record.identity.game_object_id << ",\"pbi\":" << record.identity.pbi << ",\"sound\":" << record.identity.sound
        << ",\"accumulated_volume_db\":" << std::setprecision(9) << record.identity.volume_db
        << ",\"mute_fade_ratio\":" << record.identity.mute_fade_ratio << '}';
  }
  out << '}'; return out.str();
}
bool WriteLine(const std::string& text, bool footer = false) noexcept {
  if (!footer && (bytes_written >= kMaxBytes || text.size() + 2 > kMaxBytes - bytes_written)) {
    capture_open.store(false, std::memory_order_release); return false;
  }
  const char newline = '\n'; DWORD count = 0;
  if (!WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &count, nullptr) || count != text.size() ||
      !WriteFile(file, &newline, 1, &count, nullptr) || count != 1) {
    const DWORD error = GetLastError(); logging_error.store(error ? error : ERROR_WRITE_FAULT); capture_open.store(false, std::memory_order_release); return false;
  }
  bytes_written += text.size() + 1; return true;
}
DWORD WINAPI Writer(void*) noexcept {
  try {
    std::ostringstream header;
    header << "{\"type\":\"authored_trigger_session\",\"schema\":1,\"mode\":\"observation_only\",\"pid\":" << GetCurrentProcessId()
           << ",\"qpc_frequency\":" << qpc_frequency.QuadPart
           << ",\"time_anchor_qpc\":" << anchor_qpc_before.QuadPart + (anchor_qpc_after.QuadPart - anchor_qpc_before.QuadPart) / 2
           << ",\"time_anchor_qpc_before\":" << anchor_qpc_before.QuadPart << ",\"time_anchor_qpc_after\":" << anchor_qpc_after.QuadPart
           << ",\"time_anchor_filetime\":" << anchor_filetime << ",\"image_base\":" << image_base
           << ",\"controlled_fixture\":" << (controlled_fixture.load(std::memory_order_acquire) ? "true" : "false")
           << ",\"max_capture_ms\":" << observation_window.DurationMs()
           << ",\"verified_game_sha256\":";
    if (controlled_fixture.load(std::memory_order_acquire)) header << "null}";
    else header << "\"ed5a8f8cd10f9a80d3bda5badd7ba1d4fcfc15996c8c7e018852cb170e264b63\"}";
    bool writable = WriteLine(header.str());
    for (;;) {
      if (observation_window.Expired()) capture_open.store(false, std::memory_order_release);
      Record record{};
      while (Dequeue(record)) {
        if (writable && WriteLine(Json(record))) written.fetch_add(1, std::memory_order_relaxed);
        else { writable = false; storage_drops.fetch_add(1, std::memory_order_relaxed); }
      }
      const bool stop = WaitForSingleObject(stop_event, 20) == WAIT_OBJECT_0 ||
                        observation_window.TimeLimitReached() || !writable;
      if (stop && active.load(std::memory_order_acquire) == 0 &&
          read_position == write_position.load(std::memory_order_acquire)) break;
    }
    const auto stats = GetAuthoredTriggerTraceStats();
    std::ostringstream footer;
    footer << "{\"type\":\"authored_trigger_summary\",\"capture_closed\":true,\"hook_mask\":" << stats.hook_mask
           << ",\"time_limit_reached\":" << (stats.time_limit_reached ? "true" : "false")
           << ",\"installation_error\":" << stats.installation_error << ",\"logging_error\":" << stats.logging_error
           << ",\"post_events\":" << stats.post_events << ",\"state_requests\":" << stats.state_requests
           << ",\"state_manager_calls\":" << stats.state_manager_calls << ",\"source_initializations\":" << stats.source_initializations
           << ",\"source_executions\":" << stats.source_executions << ",\"unknown_source_audio\":" << stats.unknown_source_audio
           << ",\"unknown_source_identity\":" << stats.unknown_source_identity << ",\"written\":" << stats.written
           << ",\"queue_drops\":" << stats.queue_drops << ",\"storage_drops\":" << stats.storage_drops << '}';
    if (logging_error.load() == 0) WriteLine(footer.str(), true);
    FlushFileBuffers(file);
  } catch (...) { logging_error.store(ERROR_NOT_ENOUGH_MEMORY); capture_open.store(false, std::memory_order_release); }
  return 0;
}
void DisableHooks() noexcept {
  for (std::size_t i = 0; i < targets.size(); ++i) {
    if (hook_mask.load(std::memory_order_acquire) & (DWORD{1} << i)) {
      const auto status = MH_DisableHook(targets[i]);
      if (status == MH_OK || status == MH_ERROR_DISABLED) hook_mask.fetch_and(~(DWORD{1} << i), std::memory_order_release);
    }
  }
}
}  // namespace

static DWORD StartTrace(Address image, std::size_t image_size, bool verified, const wchar_t* log_directory,
                        const AuthoredTriggerTraceFixtureTargets* fixture) noexcept {
  AcquireSRWLockExclusive(&lifecycle);
  DWORD error = ERROR_SUCCESS;
  if (started.load(std::memory_order_acquire)) { ReleaseSRWLockExclusive(&lifecycle); return ERROR_ALREADY_INITIALIZED; }
  if (!verified || !image || !log_directory || !*log_directory || image > std::numeric_limits<Address>::max() - image_size) error = ERROR_INVALID_PARAMETER;
  if (fixture && (!fixture->max_capture_ms || fixture->max_capture_ms > kMaxLiveObservationMs)) error = ERROR_INVALID_PARAMETER;
  std::array<unsigned char, 32> observed{};
  for (std::size_t i = 0; error == ERROR_SUCCESS && i < targets.size(); ++i) {
    if (fixture) {
      if (!ReadBytes(fixture->addresses[i], observed.data(), observed.size()) || observed != fixture->signatures[i]) error = ERROR_REVISION_MISMATCH;
      else targets[i] = reinterpret_cast<void*>(fixture->addresses[i]);
    } else {
      if (kRvas[i] > image_size || observed.size() > image_size - kRvas[i] ||
          !ReadBytes(image + kRvas[i], observed.data(), observed.size()) || observed != kSignatures[i]) error = ERROR_REVISION_MISMATCH;
      else targets[i] = reinterpret_cast<void*>(image + kRvas[i]);
    }
  }
  if (error != ERROR_SUCCESS) { installation_error.store(error); ReleaseSRWLockExclusive(&lifecycle); return error; }
  started.store(true, std::memory_order_release); stopped.store(false, std::memory_order_release); image_base = image;
  controlled_fixture.store(fixture != nullptr, std::memory_order_release);
  observation_window.Initialize(fixture && fixture->observation_clock ? fixture->observation_clock : &MonotonicMs,
                                fixture ? fixture->max_capture_ms : kMaxLiveObservationMs);
  try {
    for (std::size_t i = 0; i < queue.size(); ++i) queue[i].sequence.store(i, std::memory_order_relaxed);
    FILETIME time{};
    if (!QueryPerformanceFrequency(&qpc_frequency) || qpc_frequency.QuadPart <= 0) error = ERROR_INVALID_DATA;
    QueryPerformanceCounter(&anchor_qpc_before);
    GetSystemTimePreciseAsFileTime(&time); QueryPerformanceCounter(&anchor_qpc_after);
    anchor_filetime = (static_cast<U64>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    const std::wstring path = std::wstring(log_directory) + L"\\authored-trigger-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(anchor_filetime) + L".jsonl";
    if (!error) {
      file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (file == INVALID_HANDLE_VALUE) error = GetLastError();
    }
    HMODULE own_module = nullptr;
    if (!error && !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                     reinterpret_cast<LPCWSTR>(&StartAuthoredTriggerTrace), &own_module)) error = GetLastError();
    if (!error) { stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr); if (!stop_event) error = GetLastError(); }
    const MH_STATUS initialized = error ? MH_ERROR_NOT_INITIALIZED : MH_Initialize();
    if (!error && initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) error = ERROR_INVALID_FUNCTION;
    const std::array<void*, 5> detours{{reinterpret_cast<void*>(&ObservePost), reinterpret_cast<void*>(&ObserveStateRequest), reinterpret_cast<void*>(&ObserveApply), reinterpret_cast<void*>(&ObserveInit), reinterpret_cast<void*>(&ObserveExecute)}};
    std::array<void*, 5> trampolines{}; std::size_t created = 0;
    while (!error && created < targets.size()) {
      if (MH_CreateHook(targets[created], detours[created], &trampolines[created]) != MH_OK) error = ERROR_INVALID_FUNCTION;
      else ++created;
    }
    if (!error) {
      original_post.store(reinterpret_cast<PostFn>(trampolines[0]), std::memory_order_release);
      original_state.store(reinterpret_cast<StateFn>(trampolines[1]), std::memory_order_release);
      original_apply.store(reinterpret_cast<ApplyFn>(trampolines[2]), std::memory_order_release);
      original_init.store(reinterpret_cast<InitFn>(trampolines[3]), std::memory_order_release);
      original_execute.store(reinterpret_cast<ExecuteFn>(trampolines[4]), std::memory_order_release);
      worker = CreateThread(nullptr, 0, Writer, nullptr, 0, nullptr); if (!worker) error = GetLastError();
    }
    if (!error) {
      capture_open.store(true, std::memory_order_release);
      for (std::size_t i = 0; i < targets.size(); ++i) {
        if (MH_EnableHook(targets[i]) != MH_OK) { error = ERROR_INVALID_FUNCTION; break; }
        hook_mask.fetch_or(DWORD{1} << i, std::memory_order_release);
      }
    }
    if (error && !worker) {
      // No hook was enabled; these trampolines cannot be in use.
      for (std::size_t i = 0; i < created; ++i) MH_RemoveHook(targets[i]);
    }
  } catch (...) { error = ERROR_NOT_ENOUGH_MEMORY; }
  installation_error.store(error, std::memory_order_release);
  if (error) {
    capture_open.store(false, std::memory_order_release); DisableHooks();
    if (stop_event) SetEvent(stop_event);
    if (worker) WaitForSingleObject(worker, 10000);
    if (!worker || WaitForSingleObject(worker, 0) == WAIT_OBJECT_0) {
      if (worker) { CloseHandle(worker); worker = nullptr; }
      if (file != INVALID_HANDLE_VALUE) { CloseHandle(file); file = INVALID_HANDLE_VALUE; }
      if (stop_event) { CloseHandle(stop_event); stop_event = nullptr; }
      stopped.store(true, std::memory_order_release);
    }
  }
  ReleaseSRWLockExclusive(&lifecycle); return error;
}
DWORD StartAuthoredTriggerTrace(Address image, std::size_t image_size, bool verified, const wchar_t* log_directory) noexcept {
  return StartTrace(image, image_size, verified, log_directory, nullptr);
}
DWORD StartAuthoredTriggerTraceControlledFixture(const AuthoredTriggerTraceFixtureTargets& fixture,
                                                const wchar_t* log_directory) noexcept {
  return StartTrace(fixture.image, 0, true, log_directory, &fixture);
}
bool StopAuthoredTriggerTrace() noexcept {
  AcquireSRWLockExclusive(&lifecycle);
  capture_open.store(false, std::memory_order_release); DisableHooks();
  if (stop_event) SetEvent(stop_event);
  const bool done = !worker || WaitForSingleObject(worker, 10000) == WAIT_OBJECT_0;
  if (done) {
    if (worker) { CloseHandle(worker); worker = nullptr; }
    if (file != INVALID_HANDLE_VALUE) { CloseHandle(file); file = INVALID_HANDLE_VALUE; }
    if (stop_event) { CloseHandle(stop_event); stop_event = nullptr; }
    stopped.store(true, std::memory_order_release);
  }
  const bool clean = done && hook_mask.load(std::memory_order_acquire) == 0;
  ReleaseSRWLockExclusive(&lifecycle); return clean;
}
AuthoredTriggerTraceStats GetAuthoredTriggerTraceStats() noexcept {
  AuthoredTriggerTraceStats result;
  result.installation_error = installation_error.load(std::memory_order_acquire); result.logging_error = logging_error.load(std::memory_order_acquire);
  result.hook_mask = hook_mask.load(std::memory_order_acquire); result.started = started.load(std::memory_order_acquire); result.stopped = stopped.load(std::memory_order_acquire);
  result.controlled_fixture = controlled_fixture.load(std::memory_order_acquire);
  result.time_limit_reached = observation_window.TimeLimitReached();
  result.post_events = post_count.load(std::memory_order_relaxed); result.state_requests = state_count.load(std::memory_order_relaxed);
  result.state_manager_calls = apply_count.load(std::memory_order_relaxed); result.source_initializations = init_count.load(std::memory_order_relaxed);
  result.source_executions = execute_count.load(std::memory_order_relaxed); result.unknown_source_audio = unknown_audio.load(std::memory_order_relaxed);
  result.unknown_source_identity = unknown_identity.load(std::memory_order_relaxed); result.written = written.load(std::memory_order_relaxed);
  result.queue_drops = queue_drops.load(std::memory_order_relaxed); result.storage_drops = storage_drops.load(std::memory_order_relaxed); return result;
}
}  // namespace edtrace
