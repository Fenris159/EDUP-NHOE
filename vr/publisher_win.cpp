#include <winsock2.h>
#include "edtrace/vr_output.hpp"
#include "edtrace/vr_backend.hpp"
#include "edtrace/vr_haptics.hpp"
#include "edtrace/vr_route_policy.hpp"
#include <atomic>

namespace edtrace { namespace {
SOCKET socket_handle=INVALID_SOCKET;
bool winsock=false;
SRWLOCK lock=SRWLOCK_INIT;
HapticFrame latest{};
std::uint64_t latest_written=0;
bool latest_known=false;
vrbridge::RoutePolicy policy;
std::atomic<DWORD> error{ERROR_NOT_READY},backend{0};
std::atomic<bool> enabled{false},listening{false},accepting{false},gain_ready{false},focused{false};
std::atomic<std::uint64_t> polls{0},replies{0},frames{0},generation{0};
void PublishSelection() noexcept {
  backend.store(policy.Selected(),std::memory_order_release);
  focused.store(policy.Selected()!=0,std::memory_order_release);
  generation.store(policy.Generation(),std::memory_order_release);
}
}
DWORD VrOutputStart(bool requested) noexcept {
  if(!requested) return ERROR_NOT_READY;
  if(enabled.exchange(true)) return ERROR_ALREADY_INITIALIZED;
  // Native adapters remain usable even if the optional UDP relay cannot bind.
  WSADATA data{};
  const int startup=WSAStartup(MAKEWORD(2,2),&data);
  if(startup) { error.store(startup); return startup; }
  winsock=true; socket_handle=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
  if(socket_handle!=INVALID_SOCKET) {
    sockaddr_in address{}; address.sin_family=AF_INET;
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK); address.sin_port=htons(vrbridge::kPort);
    BOOL exclusive=TRUE; u_long nonblocking=1;
    if(setsockopt(socket_handle,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof(exclusive))==0&&
       bind(socket_handle,reinterpret_cast<const sockaddr*>(&address),sizeof(address))==0&&
       ioctlsocket(socket_handle,FIONBIO,&nonblocking)==0) {
      error.store(ERROR_SUCCESS); listening.store(true,std::memory_order_release); return ERROR_SUCCESS;
    }
  }
  const DWORD failure=WSAGetLastError();
  if(socket_handle!=INVALID_SOCKET) { closesocket(socket_handle); socket_handle=INVALID_SOCKET; }
  if(winsock) { WSACleanup(); winsock=false; }
  error.store(failure); return failure;
}
void VrOutputPublish(HapticFrame frame) noexcept {
  if(!enabled.load(std::memory_order_acquire)||!accepting.load(std::memory_order_acquire)||!TryAcquireSRWLockExclusive(&lock)) return;
  if(!accepting.load(std::memory_order_acquire)) { ReleaseSRWLockExclusive(&lock); return; }
  latest=HapticScale(frame,1); latest_written=GetTickCount64(); latest_known=true;
  ReleaseSRWLockExclusive(&lock); frames.fetch_add(1,std::memory_order_relaxed);
}
BOOL WINAPI VrOutputReadFrame(DWORD route,BOOL session_ready,VrBridgeFrame* output) noexcept {
  const DWORD incoming=GetLastError();
  if(!output) { SetLastError(incoming); return FALSE; }
  const bool valid=output->size==sizeof(VrBridgeFrame)&&output->version==1&&route>=1&&route<=4;
  *output={};
  if(!valid||!enabled.load(std::memory_order_acquire)||!TryAcquireSRWLockExclusive(&lock)) { SetLastError(incoming); return FALSE; }
  const auto now=GetTickCount64();
  policy.Report(route,session_ready!=FALSE,now); PublishSelection();
  output->generation=policy.Generation();
  const bool owned=gain_ready.load(std::memory_order_acquire)&&session_ready&&policy.Owns(route,now);
  if(owned) {
    output->frame=vrbridge::FreshFrame(latest,true,now,latest_written,latest_known);
    if(HapticNonzero(output->frame)) policy.NoteNonzero(route);
  }
  ReleaseSRWLockExclusive(&lock); SetLastError(incoming); return owned?TRUE:FALSE;
}
void VrOutputAcknowledgeStop(DWORD route) noexcept {
  const DWORD incoming=GetLastError();
  AcquireSRWLockExclusive(&lock); policy.AcknowledgeStop(route,GetTickCount64()); ReleaseSRWLockExclusive(&lock);
  SetLastError(incoming);
}
bool VrOutputPoll(bool gain_enabled,bool desktop_focused) noexcept {
  if(!enabled.load(std::memory_order_acquire)) return false;
  gain_ready.store(gain_enabled,std::memory_order_release);
  // Bounded drain; networking and runtime discovery never run on audio threads.
  if(socket_handle!=INVALID_SOCKET) for(unsigned i=0;i<8;++i) {
    unsigned char bytes[vrbridge::kPacketSize+1]{}; sockaddr_in peer{}; int length=sizeof(peer);
    const int count=recvfrom(socket_handle,reinterpret_cast<char*>(bytes),sizeof(bytes),0,reinterpret_cast<sockaddr*>(&peer),&length);
    if(count==SOCKET_ERROR) break;
    vrbridge::Message poll{};
    if(length!=sizeof(peer)||peer.sin_family!=AF_INET||peer.sin_addr.s_addr!=htonl(INADDR_LOOPBACK)||
       !vrbridge::Decode(bytes,static_cast<std::size_t>(count),poll)||
       (poll.kind!=vrbridge::Kind::Poll&&poll.kind!=vrbridge::Kind::NativeOpenXRPoll)) continue;
    polls.fetch_add(1,std::memory_order_relaxed);
    VrBridgeFrame sample{};
    const DWORD route=poll.kind==vrbridge::Kind::NativeOpenXRPoll?4:static_cast<DWORD>(VrBackend::SteamVR);
    const bool owns=VrOutputReadFrame(route,poll.active,&sample)!=FALSE;
    const auto reply=vrbridge::Encode({vrbridge::Kind::Frame,owns,poll.nonce,poll.sequence,sample.frame});
    if(sendto(socket_handle,reinterpret_cast<const char*>(reply.data()),reply.size(),0,reinterpret_cast<const sockaddr*>(&peer),sizeof(peer))==static_cast<int>(reply.size()))
      replies.fetch_add(1,std::memory_order_relaxed);
  }
  AcquireSRWLockExclusive(&lock);
  policy.Refresh(GetTickCount64()); PublishSelection();
  const bool ready=policy.Selected()!=0;
  const bool can_publish=gain_enabled&&(desktop_focused||ready);
  const bool was_accepting=accepting.exchange(can_publish,std::memory_order_acq_rel);
  if(!can_publish||!was_accepting) { latest={}; latest_known=false; }
  ReleaseSRWLockExclusive(&lock); return ready;
}
void VrOutputStop() noexcept {
  enabled.store(false,std::memory_order_release); listening.store(false,std::memory_order_release);
  accepting.store(false,std::memory_order_release); gain_ready.store(false,std::memory_order_release);
  if(socket_handle!=INVALID_SOCKET) { closesocket(socket_handle); socket_handle=INVALID_SOCKET; }
  if(winsock) { WSACleanup(); winsock=false; }
  AcquireSRWLockExclusive(&lock); latest={}; latest_known=false; policy.Clear(GetTickCount64()); PublishSelection(); ReleaseSRWLockExclusive(&lock);
}
VrOutputStats VrOutputGetStats() noexcept {
  return {error.load(),listening.load(),polls.load(),replies.load(),frames.load(),enabled.load(),focused.load(),backend.load(),generation.load()};
}
}
