#pragma once
#include <array>
#include <cstdint>

namespace edtrace::vrbridge {
// Values match VrBackend's stable ABI. Only focused, output-capable sessions
// are reported ready. No controller/input interface is acquired here.
class RoutePolicy {
  struct Lease { bool ready=false; std::uint64_t seen=0; };
  std::array<Lease,5> routes_{};
  unsigned selected_=0;
  std::uint64_t quiet_since_=0,generation_=0;
  bool quiet_=false;
  bool meta_nonzero_=false,meta_pending_=false;
  std::uint64_t meta_pending_since_=0;
public:
  static constexpr std::uint64_t lease_ms=250, handover_ms=200;
  void Report(unsigned route,bool ready,std::uint64_t now) noexcept {
    if(route>0&&route<routes_.size()) routes_[route]={ready,now};
    Refresh(now);
  }
  void Refresh(std::uint64_t now) noexcept {
    if(meta_pending_&&now>=meta_pending_since_&&now-meta_pending_since_>=2500) meta_pending_=false;
    unsigned next=0;
    for(unsigned i=1;i<routes_.size();++i) {
      const auto& route=routes_[i];
      if(route.ready&&now>=route.seen&&now-route.seen<lease_ms) next=i;
    }
    if(next!=selected_) {
      if(selected_==2&&meta_nonzero_) { meta_pending_=true; meta_pending_since_=now; meta_nonzero_=false; }
      if(selected_) { quiet_=true; quiet_since_=now; }
      selected_=next; ++generation_;
    }
  }
  bool Owns(unsigned route,std::uint64_t now) const noexcept {
    return route!=0&&selected_==route&&!meta_pending_&&(!quiet_||(now>=quiet_since_&&now-quiet_since_>=handover_ms));
  }
  void NoteNonzero(unsigned route) noexcept { if(route==2) meta_nonzero_=true; }
  void AcknowledgeStop(unsigned route,std::uint64_t now) noexcept {
    if(route!=2) return;
    meta_nonzero_=false;
    if(meta_pending_) { meta_pending_=false; quiet_=true; quiet_since_=now; }
  }
  unsigned Selected() const noexcept { return selected_; }
  std::uint64_t Generation() const noexcept { return generation_; }
  void Clear(std::uint64_t now) noexcept {
    routes_={}; meta_nonzero_=false; meta_pending_=false;
    if(selected_) { selected_=0; quiet_=true; quiet_since_=now; ++generation_; }
  }
};
}
