#pragma once
#include "SecureSession.h"
#include <array>
#include <span>
#include <string>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#endif

namespace openrune::net {
// Production IPv4 UDP boundary. Authentication and retries belong to SecureSession.
class UdpSocket {
public:
    explicit UdpSocket(const std::string& address, uint16_t port, bool client=false);
    ~UdpSocket();
    UdpSocket(const UdpSocket&)=delete;
    UdpSocket& operator=(const UdpSocket&)=delete;
    uint16_t port() const;
    bool sendDatagram(std::span<const uint8_t> bytes,const sockaddr_in& peer);
    void send(SecureSession& session,const sockaddr_in& peer) {
        for(const auto& bytes:session.takeDatagrams())sendDatagram(bytes,peer);
    }
    template<class F> void receive(F&& callback) {
        rearmReadSignal();
        for(int i=0;i<64;++i) {
            std::array<uint8_t,1201> bytes{};sockaddr_in source{};
            int n=read(bytes,source);
            if(n<0)break;
            if(source.sin_family==AF_INET && n>0 && n<=1200)
                callback(source,std::span<const uint8_t>(bytes).first(size_t(n)));
        }
    }
    // Block until any socket has a datagram or the timeout elapses. Callers must
    // drain each socket with receive() before waiting again. Sub-millisecond
    // timer precision is retained on Windows without changing global timer resolution.
    static void waitAny(std::span<UdpSocket* const> sockets,uint64_t timeoutMicroseconds);
    uint64_t sendDrops=0;
private:
    int read(std::span<uint8_t> bytes,sockaddr_in& source);
    void rearmReadSignal() noexcept;
#ifdef _WIN32
    SOCKET fd=INVALID_SOCKET;
    WSAEVENT readEvent=WSA_INVALID_EVENT;
    bool winsock=false;
#else
    int fd=-1;
#endif
    void close() noexcept;
};
}
