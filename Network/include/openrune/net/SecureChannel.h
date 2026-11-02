#pragma once
#include "Protocol.h"
#include <memory>
#include <string>

namespace openrune::net
{
using InvitationKey = std::array<uint8_t,32>;
// Generate out-of-band invitations with the system-seeded Mbed TLS DRBG.
InvitationKey generateInvitationKey();
enum class SecureState { Handshaking, Established, Failed };
class SecureChannel
{
public:
    // identity and secret must be provisioned privately per client. Transport ID
    // must bind the actual peer address/port; never obtain it from packet contents.
    SecureChannel(bool server, const InvitationKey&, const std::string& identity, Bytes transportId);
    ~SecureChannel();
    SecureChannel(const SecureChannel&) = delete;
    SecureChannel& operator=(const SecureChannel&) = delete;
    bool receive(std::span<const uint8_t> datagram);
    void step(uint64_t localNow);
    bool send(std::span<const uint8_t> plaintext);
    std::vector<Bytes> takeDatagrams();
    std::vector<Bytes> takePlaintext();
    SecureState state() const;
    int error() const;
    uint64_t randomSessionId(); // Established server only.
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
