#include "openrune/net/UdpSocket.h"
#include <chrono>
#include <thread>
#include <stdexcept>
#include <iostream>
#ifndef _WIN32
#include <arpa/inet.h>
#include <unistd.h>
#endif
using namespace openrune::net;
static void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
static sockaddr_in endpoint(uint16_t port){sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);inet_pton(AF_INET,"127.0.0.1",&a.sin_addr);return a;}
int main(){try{
 UdpSocket host("127.0.0.1",0),client("0.0.0.0",0,true),other("127.0.0.1",0);
 auto target=endpoint(host.port());std::array<uint8_t,4> payload{1,2,3,4};
 bool refused=false;try{UdpSocket duplicate("127.0.0.1",host.port());}catch(const std::exception&){refused=true;}check(refused,"exclusive host port");
 refused=false;try{UdpSocket wildcard("0.0.0.0",0);}catch(const std::exception&){refused=true;}check(refused,"wildcard host rejected");
 check(client.sendDatagram(payload,target),"client send");check(other.sendDatagram(payload,target),"second peer send");
 int received=0;bool sawClient=false,sawOther=false;
 auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
 while(received<2 && std::chrono::steady_clock::now()<deadline){host.receive([&](auto& source,auto bytes){check(bytes.size()==payload.size() && std::equal(bytes.begin(),bytes.end(),payload.begin()),"payload");sawClient|=ntohs(source.sin_port)==client.port();sawOther|=ntohs(source.sin_port)==other.port();++received;});std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 check(received==2&&sawClient&&sawOther,"multiple peers preserve source");
 // Exercise hostile wire sizes through raw UDP; production sends disallow them.
 auto raw=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);std::array<char,4096> large{};
 sendto(raw,large.data(),int(large.size()),0,reinterpret_cast<sockaddr*>(&target),sizeof(target));
 sendto(raw,large.data(),1201,0,reinterpret_cast<sockaddr*>(&target),sizeof(target));
 sendto(raw,large.data(),0,0,reinterpret_cast<sockaddr*>(&target),sizeof(target));
 client.sendDatagram(payload,target);received=0;deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
 while(!received&&std::chrono::steady_clock::now()<deadline){host.receive([&](auto&,auto bytes){check(bytes.size()==4,"oversized or empty leaked");++received;});std::this_thread::sleep_for(std::chrono::milliseconds(1));}check(received==1,"receive recovers after oversized datagram");
#ifdef _WIN32
 closesocket(raw);
#else
 ::close(raw);
#endif
 std::array<uint8_t,1201> excessive{};refused=false;try{client.sendDatagram(excessive,target);}catch(const std::exception&){refused=true;}check(refused,"outbound MTU bound");
 for(int i=0;i<80;++i)client.sendDatagram(payload,target);
 std::this_thread::sleep_for(std::chrono::milliseconds(10));received=0;host.receive([&](auto&,auto){++received;});check(received==64,"receive budget");
 int rest=0;host.receive([&](auto&,auto){++rest;});check(rest==16,"remaining packets drained next frame");
 // A departed peer's ICMP response must not break a shared host socket.
 uint16_t gone;{UdpSocket departed("127.0.0.1",0);gone=departed.port();}
 host.sendDatagram(payload,endpoint(gone));std::this_thread::sleep_for(std::chrono::milliseconds(30));host.receive([](auto&,auto){});
 client.sendDatagram(payload,target);received=0;deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
 while(!received&&std::chrono::steady_clock::now()<deadline){host.receive([&](auto&,auto){++received;});std::this_thread::sleep_for(std::chrono::milliseconds(1));}check(received==1,"departed peer isolation");
 std::cout<<"Production UDP: multi-peer, exclusive bind, MTU rejection, drain budget, ICMP recovery passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
