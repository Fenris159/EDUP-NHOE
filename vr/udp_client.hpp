#pragma once
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include "edtrace/vr_haptics.hpp"
#include <chrono>
#include <stdexcept>

namespace edtrace::vrbridge {
class UdpClient {
#ifdef _WIN32
  SOCKET socket_=INVALID_SOCKET;
#else
  int socket_=-1;
#endif
public:
  UdpClient() {
#ifdef _WIN32
    WSADATA data{};
    if(WSAStartup(MAKEWORD(2,2),&data)!=0) throw std::runtime_error("Winsock unavailable");
#endif
    socket_=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    sockaddr_in address{}; address.sin_family=AF_INET;
    address.sin_port=htons(kPort); address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(!Valid()||connect(socket_,reinterpret_cast<sockaddr*>(&address),sizeof(address))!=0) {
      Close(); throw std::runtime_error("Loopback socket unavailable");
    }
  }
  UdpClient(const UdpClient&)=delete;
  UdpClient& operator=(const UdpClient&)=delete;
  ~UdpClient() { Close(); }
  bool Poll(std::uint64_t nonce, std::uint64_t sequence, bool armed, Message& reply, Kind kind=Kind::Poll) {
    const auto begin=std::chrono::steady_clock::now();
    const auto packet=Encode({kind,armed,nonce,sequence,{}});
    if(send(socket_,reinterpret_cast<const char*>(packet.data()),packet.size(),0)!=static_cast<int>(packet.size())) return false;
    // Drain old/wrong packets without extending this poll's deadline.
    for(unsigned i=0;i<32;++i) {
      const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();
      if(elapsed>=static_cast<long long>(kPollDeadlineMs)) return false;
      fd_set read{}; FD_ZERO(&read); FD_SET(socket_,&read);
      timeval timeout{0,static_cast<long>((kPollDeadlineMs-elapsed)*1000)};
      if(select(static_cast<int>(socket_)+1,&read,nullptr,nullptr,&timeout)<=0) return false;
      unsigned char bytes[kPacketSize+1]{};
      const auto count=recv(socket_,reinterpret_cast<char*>(bytes),sizeof(bytes),0);
      Message message{};
      const auto age=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();
      if(count>0&&Decode(bytes,static_cast<std::size_t>(count),message)&&MatchesPoll(message,nonce,sequence,static_cast<std::uint64_t>(age))) {
        reply=message; return true;
      }
    }
    return false;
  }
private:
  bool Valid() const {
#ifdef _WIN32
    return socket_!=INVALID_SOCKET;
#else
    return socket_>=0;
#endif
  }
  void Close() {
#ifdef _WIN32
    if(Valid()) { closesocket(socket_); socket_=INVALID_SOCKET; }
    WSACleanup();
#else
    if(Valid()) { close(socket_); socket_=-1; }
#endif
  }
};
}
