#include "udp_client.hpp"
#include <openvr.h>
#include <array>
#include <cstdlib>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace {
volatile std::sig_atomic_t running=1;
void Stop(int) { running=0; }
std::filesystem::path CompanionDirectory(const char* argument) {
#ifdef _WIN32
  (void)argument;
  std::array<wchar_t,32768> path{};
  const DWORD length=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
  if(!length||length>=path.size()) throw std::runtime_error("Cannot find companion directory");
  return std::filesystem::path(std::wstring(path.data(),length)).parent_path();
#else
  std::error_code error;
  const auto path=std::filesystem::read_symlink("/proc/self/exe",error);
  return (error?std::filesystem::absolute(argument):path).parent_path();
#endif
}
struct Lifetime {
#ifdef _WIN32
  HANDLE parent=nullptr,stop=nullptr;
  ~Lifetime() { if(parent) CloseHandle(parent); if(stop) CloseHandle(stop); }
  void Start(unsigned pid,const std::string& event) {
    if(pid) {
      parent=OpenProcess(SYNCHRONIZE,FALSE,pid);
      if(!parent) throw std::runtime_error("Cannot observe parent lifetime");
    }
    if(!event.empty()) {
      stop=OpenEventA(SYNCHRONIZE,FALSE,event.c_str());
      if(!stop) throw std::runtime_error("Cannot open owned stop event");
    }
  }
  bool Alive() const {
    return running&&(!parent||WaitForSingleObject(parent,0)==WAIT_TIMEOUT)&&
      (!stop||WaitForSingleObject(stop,0)==WAIT_TIMEOUT);
  }
#else
  unsigned parent=0;
  void Start(unsigned pid,const std::string& event) {
    if(!event.empty()) throw std::runtime_error("Named stop events require Windows");
    parent=pid;
  }
  bool Alive() const { return running&&(!parent||static_cast<unsigned>(getppid())==parent); }
#endif
};
void WaitRetry(const Lifetime& lifetime) {
  for(unsigned i=0;i<40&&lifetime.Alive();++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
class SteamVr {
  vr::IVRSystem* system_=nullptr;
  vr::VRActionSetHandle_t set_=0;
  vr::VRActionHandle_t actions_[2]{};
  vr::VRInputValueHandle_t hands_[2]{};
  bool available_[2]{};
public:
  ~SteamVr() { if(system_) vr::VR_Shutdown(); }
  void Start(const std::filesystem::path& manifest) {
    vr::EVRInitError error=vr::VRInitError_None;
    system_=vr::VR_Init(&error,vr::VRApplication_Background);
    if(error!=vr::VRInitError_None||!system_) throw std::runtime_error(vr::VR_GetVRInitErrorAsEnglishDescription(error));
    const auto applications=vr::VRApplications();
    if(!applications) throw std::runtime_error("SteamVR application interface unavailable");
    const auto identity=std::filesystem::absolute(manifest).parent_path()/"haptics.vrmanifest";
    if(applications->AddApplicationManifest(identity.u8string().c_str(),true)!=vr::VRApplicationError_None||
       applications->IdentifyApplication(0,"local.ed.haptics.output")!=vr::VRApplicationError_None)
      throw std::runtime_error("Cannot isolate SteamVR haptic application identity");
    if(!vr::VRInput()) throw std::runtime_error("SteamVR haptic interface unavailable");
    Check(vr::VRInput()->SetActionManifestPath(std::filesystem::absolute(manifest).u8string().c_str()));
    Check(vr::VRInput()->GetActionSetHandle("/actions/edhaptics",&set_));
    Check(vr::VRInput()->GetActionHandle("/actions/edhaptics/out/left",&actions_[0]));
    Check(vr::VRInput()->GetActionHandle("/actions/edhaptics/out/right",&actions_[1]));
    Check(vr::VRInput()->GetInputSourceHandle("/user/hand/left",&hands_[0]));
    Check(vr::VRInput()->GetInputSourceHandle("/user/hand/right",&hands_[1]));
  }
  bool Ready(unsigned game_pid) {
    vr::VREvent_t event{};
    for(unsigned i=0;i<32&&system_->PollNextEvent(&event,sizeof(event));++i)
      if(event.eventType==vr::VREvent_Quit) {
        system_->AcknowledgeQuit_Exiting(); throw std::runtime_error("SteamVR is stopping");
      }
    // Scene ownership is queried, never requested. A background helper must
    // never rumble controllers for another game's foreground VR session.
    const auto compositor=vr::VRCompositor();
    const auto applications=vr::VRApplications();
    if(!compositor||!applications) return false;
    const auto scene=compositor->GetCurrentSceneFocusProcess();
    bool owns_scene=scene!=0&&game_pid!=0&&scene==game_pid;
    if(!owns_scene&&scene) {
      char key[vr::k_unMaxApplicationKeyLength]{};
      if(applications->GetApplicationKeyByProcessId(scene,key,sizeof(key))==vr::VRApplicationError_None)
        owns_scene=std::string(key)=="steam.app.359320";
    }
    if(!owns_scene||system_->ShouldApplicationPause()) return false;
    // Only our two output actions are updated. There are no input actions,
    // controller-state getters, tracking reads, or binding edits.
    vr::VRActiveActionSet_t active{}; active.ulActionSet=set_;
    Check(vr::VRInput()->UpdateActionState(&active,sizeof(active),1));
    bool connected=false;
    const vr::ETrackedControllerRole roles[]{vr::TrackedControllerRole_LeftHand,vr::TrackedControllerRole_RightHand};
    for(unsigned i=0;i<2;++i) {
      // Connection metadata only. Vibration actions need no digital/analog
      // action-origin query and no button, axis or tracking-state getter.
      const auto index=system_->GetTrackedDeviceIndexForControllerRole(roles[i]);
      available_[i]=index!=vr::k_unTrackedDeviceIndexInvalid&&system_->IsTrackedDeviceConnected(index);
      connected=connected||available_[i];
    }
    return connected;
  }
  void Submit(edtrace::vrbridge::HandFrame value) {
    const edtrace::vrbridge::HandPulse pulses[]{value.left,value.right};
    for(unsigned i=0;i<2;++i) {
      if(!available_[i]) continue;
      const auto& p=pulses[i];
      Check(vr::VRInput()->TriggerHapticVibrationAction(actions_[i],0,edtrace::vrbridge::kPulseSeconds,
        p.frequency_hz>0?p.frequency_hz:80,p.amplitude,hands_[i]));
    }
  }
private:
  static void Check(vr::EVRInputError error) {
    if(error!=vr::VRInputError_None) throw std::runtime_error("SteamVR output error "+std::to_string(static_cast<int>(error)));
  }
};
}
int main(int argc,char** argv) {
  try {
    bool dry=false,automatic=false; unsigned seconds=0,parent_pid=0,game_pid=0; float strength=.6f;
    std::string stop_event;
    auto manifest=CompanionDirectory(argv[0])/"resources/actions.json";
    for(int i=1;i<argc;++i) {
      const std::string arg=argv[i];
      if(arg=="--help") {
        std::cout<<"ed-vr-haptics [--auto] [--dry-run] [--seconds N] [--strength 0..100]\n"
          "  [--manifest FILE] [--game-pid N] [--parent-pid N] [--stop-event NAME]\n"
          "Output only. Auto mode waits/reconnects; never starts SteamVR or requests focus.\n"
          "Dry run initializes no VR runtime and sends no motor commands.\n"; return 0;
      } else if(arg=="--dry-run") dry=true;
      else if(arg=="--auto") automatic=true;
      else if((arg=="--seconds"||arg=="--strength"||arg=="--manifest"||arg=="--game-pid"||arg=="--parent-pid"||arg=="--stop-event")&&i+1<argc) {
        const std::string value=argv[++i];
        if(arg=="--manifest") manifest=std::filesystem::u8path(value);
        else if(arg=="--stop-event") stop_event=value;
        else {
          std::size_t used=0; const auto number=std::stoull(value,&used);
          const auto maximum=arg=="--strength"?100ull:arg=="--seconds"?86400ull:0xffffffffull;
          if(used!=value.size()||value.empty()||value[0]=='-'||number>maximum) throw std::runtime_error("Invalid "+arg);
          if(arg=="--strength") strength=static_cast<float>(number)/100;
          else if(arg=="--seconds") seconds=static_cast<unsigned>(number);
          else if(arg=="--game-pid") game_pid=static_cast<unsigned>(number);
          else parent_pid=static_cast<unsigned>(number);
        }
      } else throw std::runtime_error("Unknown or incomplete argument: "+arg);
    }
    std::signal(SIGINT,Stop); std::signal(SIGTERM,Stop);
#ifdef _WIN32
    SetEnvironmentVariableW(L"SteamAppId",nullptr); SetEnvironmentVariableW(L"SteamGameId",nullptr);
#else
    unsetenv("SteamAppId"); unsetenv("SteamGameId");
#endif
    Lifetime lifetime; lifetime.Start(parent_pid,stop_event);
    edtrace::vrbridge::UdpClient client;
    std::random_device random;
    std::uint64_t nonce=(std::uint64_t(random())<<32)^random(); if(!nonce) nonce=1;
    std::uint64_t sequence=0,received=0,missed=0;
    std::unique_ptr<SteamVr> vr;
    const auto started=std::chrono::steady_clock::now();
    auto report=started;
    bool was_owned=false;
    std::string last_error;
    if(dry) std::cout<<"Dry run; no VR or motor calls.\n"<<std::flush;
    while(lifetime.Alive()) {
      const auto tick=std::chrono::steady_clock::now();
      if(seconds&&tick-started>=std::chrono::seconds(seconds)) break;
      try {
        if(!dry&&!vr) {
          if(automatic) {
            edtrace::vrbridge::Message probe{};
            if(!client.Poll(nonce,++sequence,false,probe)) { WaitRetry(lifetime); continue; }
          }
          auto candidate=std::make_unique<SteamVr>(); candidate->Start(manifest); vr=std::move(candidate);
          std::cout<<"SteamVR output actions initialized; physical output unverified.\n"<<std::flush;
          last_error.clear();
        }
        const bool ready=dry||vr->Ready(game_pid);
        edtrace::vrbridge::Message reply{};
        const bool valid=client.Poll(nonce,++sequence,ready,reply);
        edtrace::vrbridge::HandFrame hands{};
        const bool owned=valid&&ready&&reply.active;
        if(valid) { ++received; if(owned) hands=edtrace::vrbridge::MapHands(reply.frame,strength); }
        else ++missed;
        // Standby adapters emit nothing, including zeros. Otherwise an
        // unselected API could cancel the currently selected runtime's output.
        if(!dry&&owned) vr->Submit(hands);
        was_owned=owned;
        if(tick-report>=std::chrono::seconds(5)) {
          std::cout<<"replies="<<received<<" missed="<<missed<<" armed="<<ready<<" selected="<<owned
            <<" left="<<hands.left.amplitude<<" right="<<hands.right.amplitude<<'\n'<<std::flush;
          report=tick;
        }
        std::this_thread::sleep_until(tick+std::chrono::milliseconds(20));
      } catch(const std::exception& error) {
        vr.reset(); was_owned=false;
        if(!automatic) throw;
        if(last_error!=error.what()) { last_error=error.what(); std::cerr<<"VR waiting: "<<last_error<<'\n'; }
        WaitRetry(lifetime);
      }
    }
    if(vr&&was_owned) vr->Submit({});
    std::cout<<"Stopped. replies="<<received<<" missed="<<missed<<'\n'; return 0;
  } catch(const std::exception& error) {
    std::cerr<<"VR bridge stopped: "<<error.what()<<'\n'; return 1;
  }
}
