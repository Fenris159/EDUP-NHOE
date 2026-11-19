#include "edtrace/sco_haptics.hpp"

#include <array>
#include <charconv>

namespace edtrace {
namespace {
constexpr std::uint64_t kMainShip = 0x01000000, kSupercruise = 0x10, kFsdJump = 0x40000000;
constexpr std::uint64_t kOvercharge = 0x100000;

// Status contains many unrelated fields, including nested objects. Read only
// the top-level flags from a complete Status object.
class JsonCursor {
 public:
  explicit JsonCursor(std::string_view json) noexcept : rest_(json) {}
  bool Done() noexcept { Space(); return rest_.empty(); }
  bool Take(char value) noexcept {
    Space();
    return Raw(value);
  }
  std::optional<std::string_view> String() noexcept {
    if (!Take('"')) return {};
    const auto original = rest_;
    while (!rest_.empty()) {
      const char byte = rest_.front();
      rest_.remove_prefix(1);
      if (byte == '"') return original.substr(0, original.size() - rest_.size() - 1);
      if (static_cast<unsigned char>(byte) < 0x20) return {};
      if (byte != '\\') continue;
      if (rest_.empty()) return {};
      const char escape = rest_.front();
      rest_.remove_prefix(1);
      if (escape == 'u') {
        for (int digit = 0; digit < 4; ++digit) {
          if (rest_.empty() || std::string_view("0123456789abcdefABCDEF").find(rest_.front()) == std::string_view::npos)
            return {};
          rest_.remove_prefix(1);
        }
      } else if (std::string_view("\"\\/bfnrt").find(escape) == std::string_view::npos) return {};
    }
    return {};
  }
  bool Unsigned(std::uint64_t& value) noexcept {
    Space();
    if (!Digit()) return false;
    const auto number = rest_;
    if (Raw('0')) {
      value = 0;
      return !Digit();
    }
    Digits();
    const auto end = number.data() + (number.size() - rest_.size());
    return std::from_chars(number.data(), end, value).ec == std::errc{};
  }
  bool SkipValue(unsigned depth = 0) noexcept {
    Space();
    if (rest_.empty() || depth == 8) return false;
    if (rest_.front() == '"') return String().has_value();
    const bool object = Take('{');
    if (object || Take('[')) {
      const char close = object ? '}' : ']';
      if (Take(close)) return true;
      do {
        if (object && (!String() || !Take(':'))) return false;
        if (!SkipValue(depth + 1)) return false;
        if (Take(close)) return true;
      } while (Take(','));
      return false;
    }
    for (const std::string_view literal : {"true", "false", "null"}) {
      if (rest_.substr(0, literal.size()) != literal) continue;
      rest_.remove_prefix(literal.size());
      return true;
    }
    Raw('-');
    if (Raw('0')) { if (Digit()) return false; }
    else if (!Digits()) return false;
    if (Raw('.') && !Digits()) return false;
    if (Raw('e') || Raw('E')) {
      if (!Raw('+')) Raw('-');
      if (!Digits()) return false;
    }
    return true;
  }

 private:
  std::string_view rest_;
  void Space() noexcept {
    while (!rest_.empty() && std::string_view(" \t\r\n").find(rest_.front()) != std::string_view::npos)
      rest_.remove_prefix(1);
  }
  bool Raw(char value) noexcept {
    if (rest_.empty() || rest_.front() != value) return false;
    rest_.remove_prefix(1);
    return true;
  }
  bool Digit() const noexcept { return !rest_.empty() && rest_.front() >= '0' && rest_.front() <= '9'; }
  bool Digits() noexcept {
    const auto before = rest_.size();
    while (Digit()) rest_.remove_prefix(1);
    return rest_.size() != before;
  }
};

// The recognized keys and event name contain ASCII letters and digits.
// Their only alternative JSON spelling is a Unicode escape.
bool TokenEquals(std::string_view token, std::string_view expected) noexcept {
  for (const auto character : expected) {
    if (token.empty()) return false;
    if (token.front() != '\\') {
      if (token.front() != character) return false;
      token.remove_prefix(1);
      continue;
    }
    if (token.size() < 6 || token[1] != 'u') return false;
    unsigned code = 0;
    const auto digits = token.data() + 2;
    const auto parsed = std::from_chars(digits, digits + 4, code, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != digits + 4 || code != static_cast<unsigned char>(character))
      return false;
    token.remove_prefix(6);
  }
  return token.empty();
}

struct Point { unsigned ms; float low, high; };
// Uneven bursts on both motors settle as the ship stabilizes.
constexpr std::array<Point, 20> kEngage{{
    {0,0,0}, {30,.94f,.73f}, {90,.44f,.31f}, {150,.85f,.66f}, {215,.34f,.42f},
    {282,.76f,.29f}, {365,.36f,.49f}, {459,.66f,.28f}, {517,.28f,.35f}, {624,.57f,.44f},
    {746,.29f,.24f}, {843,.50f,.29f}, {948,.25f,.32f}, {1100,.43f,.24f}, {1240,.25f,.19f},
    {1430,.34f,.22f}, {1615,.19f,.12f}, {1810,.23f,.15f}, {2100,.11f,.07f}, {2500,0,0},
}};
// The high motor softens early while the low motor carries the slowdown.
constexpr std::array<Point, 8> kDisengage{{
    {0,0,0}, {80,.27f,.18f}, {180,.56f,.31f}, {390,.54f,.27f},
    {850,.43f,.19f}, {1400,.26f,.095f}, {1980,.10f,.027f}, {2500,0,0},
}};

template<std::size_t Size>
HapticFrame Envelope(const std::array<Point, Size>& points, std::uint64_t elapsed) noexcept {
  if (elapsed >= points.back().ms) return {};
  for (std::size_t i = 1; i < Size; ++i) {
    const auto& next = points[i];
    if (elapsed >= next.ms) continue;
    const auto& start = points[i - 1];
    const auto fraction = static_cast<float>(elapsed - start.ms) / static_cast<float>(next.ms - start.ms);
    return {start.low + (next.low - start.low) * fraction, start.high + (next.high - start.high) * fraction, 0, 0};
  }
  return {};
}
}  // namespace

std::optional<ScoStatus> ParseScoStatus(std::string_view json) noexcept {
  JsonCursor cursor(json);
  if (!cursor.Take('{')) return {};
  bool have_flags = false, have_flags2 = false, have_event = false;
  std::uint64_t flags = 0, flags2 = 0;
  do {
    const auto key = cursor.String();
    if (!key || !cursor.Take(':')) return {};
    if (TokenEquals(*key, "Flags")) {
      if (have_flags || !cursor.Unsigned(flags)) return {};
      have_flags = true;
    } else if (TokenEquals(*key, "Flags2")) {
      if (have_flags2 || !cursor.Unsigned(flags2)) return {};
      have_flags2 = true;
    } else if (TokenEquals(*key, "event")) {
      const auto event = cursor.String();
      if (have_event || !event || !TokenEquals(*event, "Status")) return {};
      have_event = true;
    } else if (!cursor.SkipValue()) return {};
  } while (cursor.Take(','));
  if (!cursor.Take('}') || !cursor.Done() || !have_flags || !have_flags2 || !have_event) return {};
  const bool eligible = (flags & (kMainShip | kSupercruise | kFsdJump)) == (kMainShip | kSupercruise);
  return ScoStatus{eligible, (flags2 & kOvercharge) != 0};
}

void ScoHaptics::Observe(ScoStatus status, std::uint64_t now) noexcept {
  if (!status.in_supercruise) { Reset(); return; }
  if (previous_ && *previous_ != status.overcharged) {
    pulse_ = status.overcharged ? Pulse::Engage : Pulse::Disengage;
    start_ = now;
  }
  previous_ = status.overcharged;
}
void ScoHaptics::Reset() noexcept {
  previous_.reset();
  pulse_ = Pulse::None;
}
HapticFrame ScoHaptics::Sample(std::uint64_t now) const noexcept {
  if (now < start_) return {};
  switch (pulse_) {
    case Pulse::Engage: return Envelope(kEngage, now - start_);
    case Pulse::Disengage: return Envelope(kDisengage, now - start_);
    case Pulse::None: return {};
  }
  return {};
}
}  // namespace edtrace
