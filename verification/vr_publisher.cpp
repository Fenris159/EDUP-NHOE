#include "edtrace/vr_output.hpp"
#include <fstream>
#include <string>

int main(int argc,char** argv) {
  if(argc!=3||edtrace::VrOutputStart(true)!=ERROR_SUCCESS) return 1;
  edtrace::VrOutputPoll(true,true);
  edtrace::VrOutputPublish({.25f,.5f,.75f,1});
  { std::ofstream ready(argv[1]); ready<<"ready\n"; }
  // Owned helper only: this sends haptic data over loopback, never to a VR
  // runtime or controller. Stop marker and hard deadline bound fixture life.
  const auto start=GetTickCount64();
  while(GetTickCount64()-start<10000) {
    if(std::ifstream(argv[2]).good()) break;
    edtrace::VrOutputPublish({.25f,.5f,.75f,1});
    edtrace::VrOutputPoll(true,true);
    Sleep(1);
  }
  edtrace::VrOutputStop(); return 0;
}
