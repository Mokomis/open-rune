#include "openrune/net/Transport.h"
#include "openrune/net/ClockSync.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace openrune::net
{
Transport::Transport(uint64_t s, uint32_t e) : session(s), epoch(e)
{
    if (!s || !e) throw std::invalid_argument("session and epoch must be nonzero");
}
bool Transport::clock(uint64_t now)
{
    // Reserve arithmetic headroom; a backwards caller clock cannot revive deadlines.
    if (now < lastNow || now > UINT64_MAX-60000 || stopped) return false;
    lastNow = now;
    return true;
}
uint64_t Transport::timeout() const { return std::clamp(counters.rttMs*4, uint64_t(200), uint64_t(2000)); }
uint64_t Transport::idleMs(uint64_t now, uint64_t cap) const
{
    if (stopped) return 0;
    auto until = [&](uint64_t deadline) { return deadline > now ? deadline-now : uint64_t(0); };
    uint64_t wait = cap;
    for (const auto& lane : outgoing)
        for (const auto& [id,p] : lane)
        {
            (void)id;
            if (!p.lastSent) return 0;
            wait = std::min(wait, until(*p.lastSent+timeout()));
        }
    if (ackDirty) wait = std::min(wait, until(nextAck));
    return wait;
}
size_t Transport::pending() const
{
    return outgoing[0].size()+outgoing[1].size()+outgoing[2].size()+outgoing[3].size();
}
size_t Transport::buffered() const
{
    return incoming[0].size()+incoming[1].size()+incoming[2].size()+incoming[3].size();
}
bool Transport::enqueue(Lane lane, Bytes payload, uint64_t now, uint16_t lifetime)
{
    const size_t l = size_t(lane);
    if (l > 3 || !clock(now) || payload.size() > MaxPayloadBytes || nextMessage[l] == UINT64_MAX) return false;
    if (l == 3 && outgoing[l].size() >= 32) return false;
    if (l && outgoing[l].size() >= ReliableWindow) return false;
    Message m{Kind(l+1), lane, nextMessage[l], now, uint16_t(l == 0 || l == 3 ? lifetime : 0), std::move(payload)};
    if (!validMessage(m)) return false;
    if (!l && !outgoing[l].empty()) { ++counters.replaced; outgoing[l].clear(); }
    outgoing[l].emplace(m.id, Pending{std::move(m), std::nullopt, 0});
    ++nextMessage[l];
    return true;
}
void Transport::expire(uint64_t now)
{
    for (size_t lane : {size_t(0),size_t(3)})
    for (auto it = outgoing[lane].begin(); it != outgoing[lane].end();)
    {
        const auto& m = it->second.message;
        if (now-m.createdMs >= m.lifetimeMs) { it = outgoing[lane].erase(it); ++counters.expired; }
        else ++it;
    }
    for (size_t l = 1; l < 3; ++l)
        for (auto& [id,p] : outgoing[l])
        {
            (void)id;
            if (now-p.message.createdMs >= 30000) { stopped = true; return; }
            if (p.lastSent && now-*p.lastSent >= timeout()) p.lastSent.reset();
        }
    for (auto it = sent.begin(); it != sent.end();)
    {
        if (now-it->second.time < timeout()) { ++it; continue; }
        if (it->first > recoveryThrough)
        {
            counters.congestionWindow = std::max(size_t(2400), counters.congestionWindow/2);
            slowStartThreshold = counters.congestionWindow;
            recoveryThrough = nextPacket-1;
        }
        counters.inFlight -= it->second.bytes;
        ++counters.losses;
        it = sent.erase(it);
    }
}
std::optional<Bytes> Transport::poll(uint64_t now)
{
    if (!update(now)) return {};
    if (nextPacket == UINT64_MAX) { stopped = true; return {}; }
    Packet packet{session,epoch,nextPacket,receivedData.ranges(),{}};
    Pending* selected = nullptr;
    size_t selectedLane = 0;
    std::array<size_t,4> laneInFlight{};
    for (const auto& [number, record] : sent)
    {
        (void)number;
        laneInFlight[size_t(record.lane)] += record.bytes;
    }
    // Weighted round robin; an always-busy gameplay lane cannot starve reliable repair.
    constexpr std::array<size_t,5> order{0,1,0,3,2};
    if (now >= nextSend && sent.size() < 256)
        for (size_t i = 0; i < order.size() && !selected; ++i)
        {
            const auto l = order[(scheduler+i)%order.size()];
            for (auto& [id,p] : outgoing[l])
            {
                (void)id;
                // Reserve room for fresh input. Reliable lanes have small caps;
                // fragments may fill more of the shared window for world repair.
                const size_t laneBudget = l == 3 ? std::max(size_t(1800),std::min(size_t(9600),counters.congestionWindow*3/4)) : 1800;
                if (l && laneInFlight[l]+32+24+packet.acks.size()*16+p.message.payload.size()+100 > laneBudget) continue;
                if (!p.lastSent) { selected = &p; selectedLane = l; break; }
            }
            if (selected) scheduler = (scheduler+i+1)%order.size();
        }
    if (selected) packet.message = selected->message;
    Bytes bytes;
    if (!encode(packet, bytes)) { stopped = true; return {}; }
    size_t cost = bytes.size()+100; // Conservative reservation for the future secure envelope.
    if (selected && counters.inFlight+cost > counters.congestionWindow)
    {
        selected = nullptr; packet.message.reset();
        if (!encode(packet,bytes)) { stopped = true; return {}; }
    }
    if (!selected && (!ackDirty || now < nextAck)) return {};
    ++nextPacket; ++counters.sent;counters.sentBytes+=bytes.size();
    ackDirty = false; nextAck = now+10;
    if (selected)
    {
        ++counters.dataSent;
        if (selected->attempts++) ++counters.retransmissions;
        selected->lastSent = now;
        sent.emplace(packet.number,Sent{now,cost,Lane(selectedLane),selected->message.id});
        counters.inFlight += cost;
        nextSend = now+std::max(uint64_t(1), uint64_t(cost)*counters.rttMs/counters.congestionWindow);
        if (selectedLane == 0 || selectedLane == 3) outgoing[selectedLane].erase(selected->message.id);
    }
    return bytes;
}
bool Transport::update(uint64_t now)
{
    if (!clock(now)) return false;
    expire(now);
    return !stopped;
}
void Transport::acknowledge(const std::vector<AckRange>& ranges, uint64_t now)
{
    for (auto it = sent.begin(); it != sent.end();)
    {
        const bool acked = std::any_of(ranges.begin(), ranges.end(), [&](auto r){return it->first >= r.first && it->first <= r.last;});
        if (!acked) { ++it; continue; }
        const auto record = it->second;
        const auto sample = std::clamp(now-record.time,uint64_t(1),uint64_t(2000));
        ++counters.acknowledged;++counters.rttSamples;
        counters.observedRttMs=counters.rttSamples==1?sample:(7*counters.observedRttMs+sample)/8;
        if(previousRtt)counters.rttVariationMs+=(std::abs(double(sample)-double(*previousRtt))-counters.rttVariationMs)/16;
        previousRtt=sample;
        counters.rttMs = (7*counters.rttMs+sample)/8;
        // Basic bounded NewReno-style experiment, not full RFC 9002 recovery.
        if (it->first > recoveryThrough)
        {
            const size_t growth = counters.congestionWindow < slowStartThreshold ? record.bytes :
                std::max(size_t(1),1200*record.bytes/counters.congestionWindow);
            counters.congestionWindow = std::min(size_t(256*1200),counters.congestionWindow+growth);
        }
        outgoing[size_t(record.lane)].erase(record.message);
        counters.inFlight -= record.bytes;
        it = sent.erase(it);
    }
}
Receive Transport::receiveTrusted(std::span<const uint8_t> bytes, uint64_t now)
{
    Packet packet;
    if (!clock(now) || !decode(bytes,packet) || packet.session != session || packet.epoch != epoch ||
        std::any_of(packet.acks.begin(),packet.acks.end(),[&](auto a){return a.last >= nextPacket;}))
    { ++counters.rejected; return Receive::Invalid; }
    if (!replay.acceptable(packet.number))
    {
        counters.receivedBytes+=bytes.size();
        if (packet.message && receivedData.contains(packet.number)) ackDirty = true;
        ++counters.duplicates;
        return Receive::Duplicate;
    }
    if (packet.message)
    {
        const auto& m = *packet.message;
        const auto l = size_t(m.lane);
        if (m.createdMs > now) { ++counters.rejected; return Receive::Invalid; }
        if ((l == 1 || l == 2) && m.id >= nextReceive[l] && m.id-nextReceive[l] >= ReliableWindow)
            return Receive::Backpressure; // Do not acknowledge data not retained.
        if (!l)
        {
            if (m.id >= nextReceive[0])
            {
                nextReceive[0] = m.id+1;
                incoming[0].clear(); // Newer expired state also invalidates older pending state.
                if (now-m.createdMs < m.lifetimeMs) incoming[0].emplace(m.id,m);
                else ++counters.expired;
            }
        }
        else if (l == 3)
        {
            if (snapshotMessages.acceptable(m.id))
            {
                if (incoming[3].size() >= 32) return Receive::Backpressure;
                snapshotMessages.accept(m.id);
                if (now-m.createdMs < m.lifetimeMs) incoming[3].emplace(m.id,m);
                else ++counters.expired;
            }
        }
        else if (m.id >= nextReceive[l]) incoming[l].try_emplace(m.id,m);
        receivedData.accept(packet.number);
        ackDirty = true;
    }
    replay.accept(packet.number);
    counters.receivedBytes+=bytes.size();
    acknowledge(packet.acks,now);
    ++counters.received;
    return Receive::Accepted;
}
Receive Transport::receiveTrusted(std::span<const uint8_t> bytes, uint64_t now, const ClockSync& remoteClock)
{
    Packet packet;
    if (!remoteClock.ready(now) || !decode(bytes,packet)) { ++counters.rejected; return Receive::Invalid; }
    if (packet.message)
    {
        auto time = remoteClock.toLocal(packet.message->createdMs,now);
        if (!time) { ++counters.rejected; return Receive::Invalid; }
        packet.message->createdMs = *time;
    }
    Bytes mapped;
    if (!encode(packet,mapped)) { ++counters.rejected; return Receive::Invalid; }
    return receiveTrusted(mapped,now);
}
std::vector<Message> Transport::drain()
{
    std::vector<Message> messages;
    if (stopped) return messages;
    for (auto& [id,m] : incoming[0])
    {
        (void)id;
        if (lastNow-m.createdMs < m.lifetimeMs) messages.push_back(std::move(m));
        else ++counters.expired;
    }
    incoming[0].clear();
    for (auto& [id,m] : incoming[3])
    {
        (void)id;
        if (lastNow-m.createdMs < m.lifetimeMs) messages.push_back(std::move(m));
        else ++counters.expired;
    }
    incoming[3].clear();
    for (size_t l = 1; l < 3; ++l)
        while (true)
        {
            auto it = incoming[l].find(nextReceive[l]);
            if (it == incoming[l].end()) break;
            messages.push_back(std::move(it->second));
            incoming[l].erase(it); ++nextReceive[l];
        }
    return messages;
}
}
