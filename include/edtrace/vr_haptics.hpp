#pragma once
#include "edtrace/haptic_frame.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace edtrace::vrbridge {
// Versioned, little-endian loopback protocol; never native struct serialization.
constexpr std::uint16_t kPort = 26761;
constexpr std::size_t kPacketSize = 48;
constexpr std::uint64_t kSourceLeaseMs = 100, kClientLeaseMs = 250;
constexpr std::uint64_t kPollDeadlineMs = 40;
constexpr float kPulseSeconds = .06f;
using Packet = std::array<unsigned char, kPacketSize>;
enum class Kind : unsigned char { Poll = 1, Frame = 2, NativeOpenXRPoll = 3 };
struct Message {
  Kind kind = Kind::Poll;
  bool active = false;
  std::uint64_t nonce = 0, sequence = 0;
  HapticFrame frame{};
};
inline void Put64(Packet& p, std::size_t offset, std::uint64_t value) noexcept {
  for (unsigned i = 0; i < 8; ++i) p[offset+i] = static_cast<unsigned char>(value >> (i*8));
}
inline std::uint64_t Get64(const Packet& p, std::size_t offset) noexcept {
  std::uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i) value |= std::uint64_t(p[offset+i]) << (i*8);
  return value;
}
inline Packet Encode(Message message) noexcept {
  Packet p{}; p[0]='E'; p[1]='D'; p[2]='V'; p[3]='H'; p[4]=1;
  p[5]=static_cast<unsigned char>(message.kind); p[6]=message.active ? 1 : 0;
  Put64(p,8,message.nonce); Put64(p,16,message.sequence);
  const auto f = message.kind == Kind::Frame && message.active ? HapticScale(message.frame,1) : HapticFrame{};
  const float values[]{f.low,f.high,f.left,f.right};
  for (unsigned i=0;i<4;++i) {
    std::uint32_t bits=0; static_assert(sizeof(bits)==sizeof(float));
    std::memcpy(&bits,&values[i],4);
    for(unsigned j=0;j<4;++j) p[24+i*4+j]=static_cast<unsigned char>(bits>>(j*8));
  }
  return p;
}
inline bool Decode(const unsigned char* data, std::size_t size, Message& out) noexcept {
  out = {};
  if (size!=kPacketSize || !data) return false;
  Packet p{}; std::memcpy(p.data(),data,p.size());
  if (p[0]!='E'||p[1]!='D'||p[2]!='V'||p[3]!='H'||p[4]!=1||
      (p[5]!=1&&p[5]!=2&&p[5]!=3)||p[6]>1||p[7]) return false;
  for(unsigned i=40;i<48;++i) if(p[i]) return false;
  Message result{static_cast<Kind>(p[5]),p[6]==1,Get64(p,8),Get64(p,16),{}};
  if(!result.nonce||!result.sequence) return false;
  float values[4]{};
  for(unsigned i=0;i<4;++i) {
    std::uint32_t bits=0;
    for(unsigned j=0;j<4;++j) bits|=std::uint32_t(p[24+i*4+j])<<(j*8);
    std::memcpy(&values[i],&bits,4);
    if(!std::isfinite(values[i])||values[i]<0||values[i]>1) return false;
    if((result.kind!=Kind::Frame||!result.active)&&values[i]!=0) return false;
  }
  result.frame={values[0],values[1],values[2],values[3]}; out=result; return true;
}
struct HandPulse { float amplitude=0, frequency_hz=0; };
struct HandFrame { HandPulse left{},right{}; };
// Main motors are frequency bands, not left/right spatial positions. Carry
// both into both hands, then retain each impulse trigger on its own hand.
// Frequency is an adapter choice, not a recovered frequency from the game.
inline HandFrame MapHands(HapticFrame value, float strength=1) noexcept {
  value=HapticScale(value,1); strength=HapticUnit(strength);
  const float body=value.low>value.high?value.low:value.high;
  const auto hand=[&](float trigger) {
    const float amplitude=body>trigger?body:trigger;
    const float frequency=trigger>body?160.0f:value.high>value.low?160.0f:80.0f;
    return HandPulse{amplitude*strength,amplitude>0?frequency:0};
  };
  return {hand(value.left),hand(value.right)};
}
inline HapticFrame FreshFrame(HapticFrame frame, bool active, std::uint64_t now,
                             std::uint64_t written, bool known) noexcept {
  return active&&known&&now>=written&&now-written<kSourceLeaseMs ? HapticScale(frame,1) : HapticFrame{};
}
// Receiver accepts only the response to this poll, within the poll deadline.
// Old queued packets never acquire a new lifetime just because they arrived.
inline bool MatchesPoll(const Message& message, std::uint64_t nonce, std::uint64_t sequence,
                        std::uint64_t elapsed_ms) noexcept {
  return message.kind==Kind::Frame&&message.nonce==nonce&&message.sequence==sequence&&elapsed_ms<kPollDeadlineMs;
}
} // namespace edtrace::vrbridge
