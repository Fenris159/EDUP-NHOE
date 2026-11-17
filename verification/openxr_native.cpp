#include "edtrace/openxr_native_cache.hpp"
#include "edtrace/openxr_native_identity.hpp"
#include <cstdio>
#include <cstdlib>

void Check(bool value,const char* message) {
  if(!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
int main() {
  using namespace edtrace;
  const char* mapping="0000000140000000-0000000141000000 r-xp 00001000 08:2a 123456 /private/path/EliteDangerous64.exe";
  Check(NativeXrMappedFile(mapping,8,42,123456),"installed PE mapping matches literal device/inode identity");
  Check(!NativeXrMappedFile(mapping,8,42,123457),"other inode cannot activate native layer");
  Check(!NativeXrMappedFile(mapping,8,43,123456),"other device cannot activate native layer");
  Check(!NativeXrMappedFile("1000-2000 r-xp 00000000 00:00 0",0,0,0),"anonymous mapping cannot activate native layer");
  Check(!NativeXrMappedFile("1000-2000 r-xp 00000000 08:2a not-an-inode",8,42,123456),"malformed inode rejected");
  Check(!NativeXrMappedFile("1000-2000 r-xp 00000000 08:no 123456",8,42,123456),"malformed device rejected");
  Check(NativeXrRecentlyReady(true,1099,1000),"recent render heartbeat arms worker");
  Check(!NativeXrRecentlyReady(true,1100,1000),"render stall expires readiness at 100 ms");
  Check(!NativeXrRecentlyReady(false,1001,1000),"inactive render heartbeat never arms worker");
  Check(!NativeXrRecentlyReady(true,999,1000),"invalid backward heartbeat rejected");
  NativeXrCachedFrame cache{{.25f,.5f,.75f,1},1000,7,true,true};
  VrBridgeFrame output{};
  Check(cache.Sample(1099,7,output)&&output.frame.low==.25f&&output.frame.high==.5f&&output.frame.left==.75f&&output.frame.right==1,
        "fresh owned relay frame preserves four channels");
  Check(!cache.Sample(1100,7,output)&&!HapticNonzero(output.frame),"cached relay frame expires at 100 ms");
  Check(!cache.Sample(1001,8,output)&&!HapticNonzero(output.frame),"prior readiness epoch cannot replay cached output");
  cache.owned=false;
  Check(!cache.Sample(1001,7,output)&&!HapticNonzero(output.frame),"standby relay response never becomes output");
  cache.owned=true; cache.known=false;
  Check(!cache.Sample(1001,7,output),"unknown relay frame never becomes output");
  std::puts("PASS native OpenXR identity, readiness expiry, bounded frame cache and readiness epochs");
}
