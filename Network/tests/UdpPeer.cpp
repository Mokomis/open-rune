// Test-only loopback adapter. No remote addresses are accepted by this program.
// Legacy mode is unauthenticated; secure mode uses a PUBLIC fixture key.
// Never use this executable as the production game transport.
#include "openrune/net/Transport.h"
#include "openrune/net/SecureSession.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;
using AddressLength = int;
constexpr Socket InvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
using Socket = int;
using AddressLength = socklen_t;
constexpr Socket InvalidSocket = -1;
#endif

using namespace openrune::net;
namespace
{
uint64_t milliseconds()
{
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
bool wouldBlock()
{
#ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR;
#endif
}
bool peerClosed()
{
#ifdef _WIN32
    return WSAGetLastError() == WSAECONNRESET || WSAGetLastError() == WSAECONNREFUSED;
#else
    return errno == ECONNREFUSED || errno == ECONNRESET;
#endif
}
struct SocketOwner
{
    Socket value = InvalidSocket;
    ~SocketOwner()
    {
        if (value != InvalidSocket)
        {
#ifdef _WIN32
            closesocket(value);
#else
            close(value);
#endif
        }
    }
};
struct Address
{
    sockaddr_storage storage{};
    AddressLength length;
    Address(bool ipv6, uint16_t port) : length(ipv6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in))
    {
        if (ipv6)
        {
            auto& a = reinterpret_cast<sockaddr_in6&>(storage);
            a.sin6_family = AF_INET6; a.sin6_addr = in6addr_loopback; a.sin6_port = htons(port);
        }
        else
        {
            auto& a = reinterpret_cast<sockaddr_in&>(storage);
            a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons(port);
        }
    }
    sockaddr* get() { return reinterpret_cast<sockaddr*>(&storage); }
    uint16_t port() const
    {
        return storage.ss_family == AF_INET6 ? ntohs(reinterpret_cast<const sockaddr_in6&>(storage).sin6_port) :
            ntohs(reinterpret_cast<const sockaddr_in&>(storage).sin_port);
    }
    bool loopback() const
    {
        if (storage.ss_family == AF_INET) return reinterpret_cast<const sockaddr_in&>(storage).sin_addr.s_addr == htonl(INADDR_LOOPBACK);
        if (storage.ss_family == AF_INET6) return IN6_IS_ADDR_LOOPBACK(&reinterpret_cast<const sockaddr_in6&>(storage).sin6_addr);
        return false;
    }
};
}
int main(int argc, char** argv)
{
    try
    {
        const bool secure = argc > 1 && std::string(argv[argc-1]) == "secure";
        if (secure) --argc;
        if (argc != 3 && argc != 5) throw std::runtime_error("usage: net_udp_peer server|client 4|6 [port shared-clock-origin]");
        const bool server = std::string(argv[1]) == "server", ipv6 = std::string(argv[2]) == "6";
        if ((!server && std::string(argv[1]) != "client") || (server ? argc != 3 : argc != 5) ||
            (std::string(argv[2]) != "4" && !ipv6)) throw std::runtime_error("invalid test arguments");
#ifdef _WIN32
        WSADATA data;
        if (WSAStartup(MAKEWORD(2,2),&data)) throw std::runtime_error("WSAStartup");
#endif
        SocketOwner socket;
        socket.value = ::socket(ipv6 ? AF_INET6 : AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        if (socket.value == InvalidSocket) throw std::runtime_error("socket creation");
        Address local(ipv6,0);
        if (bind(socket.value,local.get(),local.length)) throw std::runtime_error("loopback bind");
        if (getsockname(socket.value,local.get(),&local.length)) throw std::runtime_error("getsockname");
#ifdef _WIN32
        u_long nonblocking = 1;
        if (ioctlsocket(socket.value,FIONBIO,&nonblocking)) throw std::runtime_error("nonblocking socket");
#else
        if (fcntl(socket.value,F_SETFL,O_NONBLOCK) == -1) throw std::runtime_error("nonblocking socket");
#endif
        uint64_t origin = (server || secure) ? milliseconds() : std::stoull(argv[4]);
        // Public deterministic test invitation; never use this outside loopback.
        InvitationKey testKey{};
        for (size_t i = 0; i < testKey.size(); ++i) testKey[i] = uint8_t(i+1);
        std::unique_ptr<SecureSession> protectedSession;
        auto startSecure = [&](Address& peer)
        {
            auto* raw = reinterpret_cast<uint8_t*>(peer.get());
            protectedSession = std::make_unique<SecureSession>(server,testKey,"loopback-test",Bytes(raw,raw+peer.length));
        };
        bool connected = !server;
        if (server) std::cout << local.port() << ' ' << origin << std::endl;
        else
        {
            const auto port = std::stoul(argv[3]);
            if (!port || port > 65535) throw std::runtime_error("invalid port");
            Address peer(ipv6,uint16_t(port));
            if (connect(socket.value,peer.get(),peer.length)) throw std::runtime_error("loopback connect");
            if (secure) startSecure(peer);
        }
        Transport transport(7,1);
        unsigned submitted = 0, received = 0;
        uint64_t finishedAt = 0;
        auto completed = [&] { return received == 100 && submitted == 100 &&
            (protectedSession ? protectedSession->pending() : transport.pending()) == 0; };
        auto report = [&]
        {
            std::cout << "PASS " << (secure ? "DTLS" : "UDP") << (ipv6 ? 6 : 4)
                << " sent=" << submitted << " received=" << received
                << (secure ? " clock_offset_ms=" : " retries=")
                << (secure ? protectedSession->clockSync().offsetMs() : int64_t(transport.stats().retransmissions)) << '\n';
            return 0;
        };
        while (milliseconds()-origin < 10000)
        {
            auto now = milliseconds()-origin;
            for (unsigned budget = 0; budget < 64; ++budget)
            {
                std::array<uint8_t,1201> buffer{};
                Address peer(ipv6,0);
                auto length = recvfrom(socket.value,reinterpret_cast<char*>(buffer.data()),int(buffer.size()),0,peer.get(),&peer.length);
                if (length < 0)
                {
                    if (wouldBlock()) break;
                    // Both processes independently prove receipt and ACK of all
                    // records. Clock probes may race the other process closing.
                    if (peerClosed() && completed()) return report();
                    throw std::runtime_error("recvfrom");
                }
                if (!peer.loopback()) throw std::runtime_error("non-loopback source rejected");
                if (!connected)
                {
                    Packet first;
                    if (secure)
                    { if (length < 13 || buffer[0] != 22) continue; }
                    else if (!decode(std::span(buffer).first(size_t(length)),first) || first.session != 7 || first.epoch != 1) continue;
                    if (connect(socket.value,peer.get(),peer.length)) throw std::runtime_error("server loopback connect");
                    connected = true;
                    if (secure) startSecure(peer);
                }
                now = milliseconds()-origin;
                if (secure) protectedSession->receiveDatagram(std::span(buffer).first(size_t(length)));
                else
                {
                    const auto result = transport.receiveTrusted(std::span(buffer).first(size_t(length)),now);
                    if (result == Receive::Invalid) throw std::runtime_error("invalid peer data");
                }
            }
            now = milliseconds()-origin;
            const auto localClock = now+(server ? 10000 : 50000);
            if (protectedSession) protectedSession->step(localClock);
            for (const auto& m : protectedSession ? protectedSession->drain() : transport.drain())
            {
                ++received;
                if (m.id != received || m.payload != Bytes{uint8_t(received),uint8_t(server ? 0xa1 : 0xb2)})
                    throw std::runtime_error("payload or order mismatch");
            }
            if (connected)
            {
                while (submitted < 100)
                {
                    Bytes payload{uint8_t(submitted+1),uint8_t(server ? 0xb2 : 0xa1)};
                    const bool accepted = secure ? protectedSession->enqueue(Lane::Control,payload) : transport.enqueue(Lane::Control,payload,now);
                    if (!accepted) break;
                    ++submitted;
                }
                std::vector<Bytes> outgoing;
                if (secure) { protectedSession->step(localClock); outgoing = protectedSession->takeDatagrams(); }
                else if (auto bytes = transport.poll(now)) outgoing.push_back(std::move(*bytes));
                for (const auto& bytes : outgoing)
                {
                    auto n = send(socket.value,reinterpret_cast<const char*>(bytes.data()),int(bytes.size()),0);
                    if (n < 0 && !wouldBlock())
                    {
                        if (peerClosed() && completed()) return report();
                        throw std::runtime_error("send");
                    }
                    if (n >= 0 && size_t(n) != bytes.size()) throw std::runtime_error("partial datagram");
                }
            }
            if (transport.failed() || (protectedSession && protectedSession->failed())) throw std::runtime_error("transport failed");
            if (completed())
            {
                if (!finishedAt) finishedAt = now;
                // Deliberately asymmetric linger exercises the completed-peer
                // close race instead of relying on identical process timing.
                if (now-finishedAt >= (server ? 100 : 500)) return report();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        throw std::runtime_error("loopback test deadline exceeded");
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
