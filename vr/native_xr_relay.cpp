#include "native_xr_relay.hpp"
#include "udp_client.hpp"
#include "edtrace/openxr_native_cache.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <random>
#include <thread>

#ifdef ED_NATIVE_XR_RELAY_FIXTURE
extern "C" std::uint64_t EDNativeXrFixtureClock() noexcept;
extern "C" bool EDNativeXrFixturePoll(std::uint64_t,std::uint64_t,bool,edtrace::vrbridge::Message&,edtrace::vrbridge::Kind);
extern "C" void EDNativeXrFixtureCycleDone() noexcept;
#endif

namespace edtrace { namespace {
std::uint64_t Clock() noexcept {
#ifdef ED_NATIVE_XR_RELAY_FIXTURE
  return EDNativeXrFixtureClock();
#else
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
#ifdef ED_NATIVE_XR_RELAY_FIXTURE
struct RelayClient {
  bool Poll(std::uint64_t nonce,std::uint64_t sequence,bool armed,vrbridge::Message& reply,vrbridge::Kind kind) {
    return EDNativeXrFixturePoll(nonce,sequence,armed,reply,kind);
  }
};
#else
using RelayClient=vrbridge::UdpClient;
#endif
struct Relay {
  std::atomic<bool> running{false},ready{false};
  std::atomic<std::uint64_t> last_ready{0},epoch{0};
  std::mutex lifecycle,cache_mutex,wait_mutex;
  std::condition_variable changed;
  NativeXrCachedFrame cache{};
  std::thread worker;
  void Invalidate() noexcept {
    std::lock_guard<std::mutex> lock(cache_mutex); cache={};
  }
  void Wait(std::chrono::steady_clock::time_point deadline) {
    std::unique_lock<std::mutex> lock(wait_mutex);
    changed.wait_until(lock,deadline,[&]{ return !running.load(std::memory_order_acquire); });
  }
  void Run() noexcept {
    while(running.load(std::memory_order_acquire)) {
      try {
        RelayClient client;
#ifdef ED_NATIVE_XR_RELAY_FIXTURE
        const std::uint64_t nonce=0x12345678;
#else
        std::random_device random;
        std::uint64_t nonce=(std::uint64_t(random())<<32)^random(); if(!nonce) nonce=1;
#endif
        std::uint64_t sequence=0;
        while(running.load(std::memory_order_acquire)) {
          const auto tick=std::chrono::steady_clock::now();
          const auto sent_epoch=epoch.load(std::memory_order_acquire);
          const bool armed=NativeXrRecentlyReady(ready.load(std::memory_order_acquire),Clock(),last_ready.load(std::memory_order_acquire));
          vrbridge::Message message{};
          const bool valid=client.Poll(nonce,++sequence,armed,message,vrbridge::Kind::NativeOpenXRPoll);
          const auto now=Clock();
          const bool still_ready=NativeXrRecentlyReady(ready.load(std::memory_order_acquire),now,last_ready.load(std::memory_order_acquire));
          {
            std::lock_guard<std::mutex> lock(cache_mutex);
            if(valid&&armed&&still_ready&&epoch.load(std::memory_order_acquire)==sent_epoch)
              cache={message.frame,now,sent_epoch,true,message.active};
            else cache={};
          }
#ifdef ED_NATIVE_XR_RELAY_FIXTURE
          EDNativeXrFixtureCycleDone();
#endif
          Wait(tick+std::chrono::milliseconds(20));
        }
        vrbridge::Message ignored{};
        client.Poll(nonce,++sequence,false,ignored,vrbridge::Kind::NativeOpenXRPoll);
      } catch(...) {
        // Resource/runtime unavailability disables this output and retries on
        // the worker. There are no sockets, waits or allocation on render reads.
        Invalidate(); Wait(std::chrono::steady_clock::now()+std::chrono::milliseconds(250));
      }
    }
    Invalidate();
  }
  void Stop() noexcept {
    std::lock_guard<std::mutex> lock(lifecycle);
    ready.store(false,std::memory_order_release); epoch.fetch_add(1,std::memory_order_acq_rel);
    running.store(false,std::memory_order_release); changed.notify_all();
    if(worker.joinable()) worker.join();
    Invalidate();
  }
  ~Relay() { Stop(); }
};
Relay& State() { static Relay value; return value; }
}
bool NativeXrRelayStart() noexcept {
  auto& relay=State();
  try {
    std::lock_guard<std::mutex> lock(relay.lifecycle);
    if(relay.running.load(std::memory_order_acquire)) return true;
    if(relay.worker.joinable()) relay.worker.join();
    relay.running.store(true,std::memory_order_release);
    relay.worker=std::thread([&relay]{ relay.Run(); }); return true;
  } catch(...) { relay.running.store(false,std::memory_order_release); return false; }
}
void NativeXrRelayStop() noexcept { State().Stop(); }
VrBool ED_VR_CALL NativeXrRelayRead(VrDword backend,VrBool session_ready,VrBridgeFrame* frame) noexcept {
  if(!frame||frame->size!=sizeof(*frame)||frame->version!=1) return 0;
  *frame={};
  if(backend!=static_cast<VrDword>(VrBackend::NativeOpenXR)) return 0;
  auto& relay=State(); const auto now=Clock(); const bool wanted=session_ready!=0;
  if(wanted) relay.last_ready.store(now,std::memory_order_release);
  if(relay.ready.exchange(wanted,std::memory_order_acq_rel)!=wanted) relay.epoch.fetch_add(1,std::memory_order_acq_rel);
  if(!wanted||!relay.running.load(std::memory_order_acquire)) return 0;
  try {
    std::unique_lock<std::mutex> lock(relay.cache_mutex,std::try_to_lock);
    if(!lock.owns_lock()||!relay.ready.load(std::memory_order_acquire)) return 0;
    return relay.cache.Sample(now,relay.epoch.load(std::memory_order_acquire),*frame)?1:0;
  } catch(...) { return 0; }
}
}
