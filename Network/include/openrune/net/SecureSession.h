#pragma once
#include "SecureChannel.h"
#include "ClockSync.h"
#include "Transport.h"

namespace openrune::net
{
// Single-owner, caller-driven authenticated session. UDP routing and invitation
// provisioning remain caller responsibilities. No public listener or game binding.
class SecureSession
{
public:
    SecureSession(bool server, const InvitationKey&, const std::string& identity, Bytes peerAddress, uint32_t epoch = 1);
    bool receiveDatagram(std::span<const uint8_t> bytes);
    void step(uint64_t localNow, bool transmit = true);
    void flush(); // Send queued work after this frame has captured its input.
    bool enqueue(Lane lane, Bytes payload, uint16_t lifetimeMs = 100);
    std::vector<Message> drain();
    std::vector<Bytes> takeDatagrams();
    bool ready() const;
    bool failed() const;
    const ClockSync& clockSync() const { return clock; }
    uint64_t sessionId() const { return session; }
    int securityError() const { return channel.error(); }
    size_t pending() const { return transport ? transport->pending() : 0; }
    bool isServer() const { return server; }
    uint32_t worldEpoch() const { return epoch; }
    uint64_t localTime() const { return now; }
    uint64_t roundTripMs() const { return transport ? transport->stats().rttMs : 100; }
    const Stats* transportStats() const { return transport ? &transport->stats() : nullptr; }
    size_t snapshotPartsPending() const { return transport ? transport->snapshotPartsPending() : 0; }
    void discardSnapshotParts() { if (transport) transport->discardSnapshotParts(); }
private:
    SecureChannel channel;
    ClockSync clock;
    std::unique_ptr<Transport> transport;
    bool server, joined = false, stopped = false;
    uint32_t epoch;
    uint64_t session = 0, now = 0, start = 0, nextHello = 0, nextClockReply = 0;
    bool started = false;
    bool usable = false;
    uint64_t lastUsable = 0;
    void handle(std::span<const uint8_t> bytes);
    void hello(uint8_t type);
};
}
