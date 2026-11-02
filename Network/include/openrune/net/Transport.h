#pragma once
#include "Protocol.h"
#include <map>

namespace openrune::net
{
class ClockSync;
enum class Receive { Accepted, Duplicate, Invalid, Backpressure };
struct Stats
{
    uint64_t sent = 0, received = 0, rejected = 0, duplicates = 0;
    uint64_t retransmissions = 0, expired = 0, replaced = 0, losses = 0;
    size_t inFlight = 0, congestionWindow = 12000;
    uint64_t rttMs = 100;
    // Authenticated transport bytes (excluding DTLS/UDP/DHT framing). Losses
    // count TX timeouts, not proven packet loss. RTT includes ACK scheduling.
    uint64_t dataSent=0, acknowledged=0, rttSamples=0, observedRttMs=0, sentBytes=0, receivedBytes=0;
    double rttVariationMs=0;
};
// Single-owner delivery core, with no socket or engine access. SecureSession
// supplies authenticated plaintext and the peer clock mapping. The two-argument
// receive overload retains the shared-clock test path. Never attach it to raw UDP.
class Transport
{
public:
    Transport(uint64_t session, uint32_t epoch);
    bool enqueue(Lane lane, Bytes payload, uint64_t now, uint16_t lifetimeMs = 100);
    std::optional<Bytes> poll(uint64_t now);
    bool update(uint64_t now); // Advance expiry without creating a transmission.
    Receive receiveTrusted(std::span<const uint8_t> bytes, uint64_t now);
    Receive receiveTrusted(std::span<const uint8_t> bytes, uint64_t now, const ClockSync& remoteClock);
    std::vector<Message> drain();
    size_t pending() const;
    size_t snapshotPartsPending() const { return outgoing[3].size(); }
    void discardSnapshotParts() { outgoing[3].clear(); }
    size_t buffered() const;
    const Stats& stats() const { return counters; }
    bool failed() const { return stopped; }
private:
    struct Pending { Message message; std::optional<uint64_t> lastSent; uint32_t attempts = 0; };
    struct Sent { uint64_t time; size_t bytes; Lane lane; uint64_t message; };
    uint64_t session;
    uint32_t epoch;
    uint64_t nextPacket = 1, lastNow = 0, nextSend = 0, nextAck = 0;
    uint64_t recoveryThrough = 0;
    std::array<uint64_t,4> nextMessage{1,1,1,1}, nextReceive{1,1,1,1};
    std::array<std::map<uint64_t,Pending>,4> outgoing;
    std::array<std::map<uint64_t,Message>,4> incoming;
    std::map<uint64_t,Sent> sent;
    ReplayWindow replay, receivedData, snapshotMessages;
    Stats counters;
    size_t scheduler = 0, slowStartThreshold = SIZE_MAX;
    bool ackDirty = false, stopped = false;
    std::optional<uint64_t> previousRtt;
    bool clock(uint64_t now);
    uint64_t timeout() const;
    void expire(uint64_t now);
    void acknowledge(const std::vector<AckRange>& acks, uint64_t now);
};
}
