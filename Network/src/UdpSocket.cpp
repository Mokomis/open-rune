#include "openrune/net/UdpSocket.h"
#include <stdexcept>
#include <iostream>
#ifdef _WIN32
#include <mswsock.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#endif
namespace openrune::net {
namespace {
#ifdef _WIN32
using AddressLength=int;
int lastError(){return WSAGetLastError();}
bool temporary(int e){return e==WSAEWOULDBLOCK || e==WSAEINTR || e==WSAENOBUFS || e==WSAENETUNREACH || e==WSAEHOSTUNREACH || e==WSAENETDOWN || e==WSAECONNREFUSED || e==WSAECONNRESET;}
#else
using AddressLength=socklen_t;
int lastError(){return errno;}
bool temporary(int e){return e==EAGAIN || e==EWOULDBLOCK || e==EINTR || e==ENOBUFS || e==ENETUNREACH || e==EHOSTUNREACH || e==ENETDOWN || e==ECONNREFUSED || e==ECONNRESET;}
#endif
[[noreturn]] void fail(const char* operation,int error){throw std::runtime_error(std::string("Open Rune UDP ")+operation+" failed ("+std::to_string(error)+")");}
}
UdpSocket::UdpSocket(const std::string& address,uint16_t requestedPort,bool client) {
#ifdef _WIN32
    WSADATA data{};int startup=WSAStartup(MAKEWORD(2,2),&data);
    if(startup)fail("startup",startup);
    winsock=true;
#endif
    try {
        sockaddr_in local{};local.sin_family=AF_INET;local.sin_port=htons(requestedPort);
        if(inet_pton(AF_INET,address.c_str(),&local.sin_addr)!=1 || (!client && local.sin_addr.s_addr==INADDR_ANY))
            throw std::runtime_error("Open Rune UDP requires a specific numeric IPv4 host address");
        fd=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
#ifdef _WIN32
        if(fd==INVALID_SOCKET)fail("socket",lastError());
        // Prevent a second process from hijacking a host's bound port.
        BOOL exclusive=TRUE;
        if(setsockopt(fd,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof(exclusive)))fail("exclusive bind",lastError());
        u_long nonblocking=1;
        if(ioctlsocket(fd,FIONBIO,&nonblocking))fail("nonblocking",lastError());
        // Do not turn an ICMP port-unreachable from a departed player into a
        // fatal error for every other player sharing this socket.
        BOOL reset=FALSE;DWORD returned=0;
        if(WSAIoctl(fd,SIO_UDP_CONNRESET,&reset,sizeof(reset),nullptr,0,&returned,nullptr,nullptr))fail("ICMP policy",lastError());
#else
        if(fd<0)fail("socket",lastError());
        int flags=fcntl(fd,F_GETFL,0);
        if(flags<0 || fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0)fail("nonblocking",lastError());
#endif
        if(bind(fd,reinterpret_cast<sockaddr*>(&local),sizeof(local)))fail("bind",lastError());
    } catch(...) {close();throw;}
}
void UdpSocket::close() noexcept {
#ifdef _WIN32
    if(fd!=INVALID_SOCKET){closesocket(fd);fd=INVALID_SOCKET;}
    if(winsock){WSACleanup();winsock=false;}
#else
    if(fd>=0){::close(fd);fd=-1;}
#endif
}
UdpSocket::~UdpSocket(){close();}
uint16_t UdpSocket::port() const {
    sockaddr_in local{};AddressLength size=sizeof(local);
    if(getsockname(fd,reinterpret_cast<sockaddr*>(&local),&size))fail("local address",lastError());
    return ntohs(local.sin_port);
}
bool UdpSocket::sendDatagram(std::span<const uint8_t> bytes,const sockaddr_in& peer) {
    if(bytes.empty() || bytes.size()>1200)throw std::runtime_error("Open Rune UDP payload outside 1..1200 bytes");
#ifdef _WIN32
    auto n=sendto(fd,reinterpret_cast<const char*>(bytes.data()),int(bytes.size()),0,reinterpret_cast<const sockaddr*>(&peer),sizeof(peer));
#else
    auto n=sendto(fd,bytes.data(),bytes.size(),0,reinterpret_cast<const sockaddr*>(&peer),sizeof(peer));
#endif
    if(n<0) {
        int error=lastError();
        if(!temporary(error))fail("send",error);
        ++sendDrops;
        if((sendDrops&(sendDrops-1))==0)std::cerr<<"OPEN_RUNE_UDP_DROP count="<<sendDrops<<" error="<<error<<'\n';
        return false;
    }
    if(size_t(n)!=bytes.size())throw std::runtime_error("Open Rune short UDP datagram send");
    return true;
}
int UdpSocket::read(std::span<uint8_t> bytes,sockaddr_in& source) {
    AddressLength size=sizeof(source);
#ifdef _WIN32
    int n=recvfrom(fd,reinterpret_cast<char*>(bytes.data()),int(bytes.size()),0,reinterpret_cast<sockaddr*>(&source),&size);
#else
    int n=int(recvfrom(fd,bytes.data(),bytes.size(),0,reinterpret_cast<sockaddr*>(&source),&size));
#endif
    if(n<0) {
        int error=lastError();
#ifdef _WIN32
        // Windows discards the tail and reports WSAEMSGSIZE for a truncated
        // packet. Ignore the entire unauthenticated packet and keep draining.
        if(error==WSAEMSGSIZE)return 0;
#endif
        if(temporary(error))return -1;
        fail("receive",error);
    }
    return n;
}
}
