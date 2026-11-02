#include "openrune/net/Protocol.h"
#include <limits>

namespace openrune::net
{
namespace
{
constexpr size_t HeaderBytes = 32, MessageHeaderBytes = 24;
void put(Bytes& b, uint64_t value, size_t n)
{
    for (size_t i = 0; i < n; ++i) b.push_back(uint8_t(value >> ((n - i - 1) * 8)));
}
struct Reader
{
    std::span<const uint8_t> bytes;
    size_t offset = 0;
    bool get(uint64_t& value, size_t n)
    {
        if (n > bytes.size() - offset) return false;
        value = 0;
        for (size_t i = 0; i < n; ++i) value = (value << 8) | bytes[offset++];
        return true;
    }
};
bool validAcks(const std::vector<AckRange>& acks)
{
    if (acks.size() > MaxAckRanges) return false;
    uint64_t previous = 0;
    for (auto r : acks)
    {
        if (!r.first || r.first > r.last || r.last == std::numeric_limits<uint64_t>::max()) return false;
        if (previous && (r.last >= previous || previous - r.last <= 1)) return false;
        previous = r.first;
    }
    return true;
}
}
bool validMessage(const Message& m)
{
    if (!m.id || m.id == UINT64_MAX || m.createdMs == UINT64_MAX || m.payload.size() > MaxPayloadBytes) return false;
    switch (m.lane)
    {
    case Lane::Gameplay: return m.kind == Kind::State && m.lifetimeMs > 0 && m.lifetimeMs <= 1000;
    case Lane::Snapshots: return m.kind == Kind::SnapshotPart && m.lifetimeMs > 0 && m.lifetimeMs <= 1000;
    case Lane::Control: return m.kind == Kind::Event && m.lifetimeMs == 0;
    case Lane::Bulk: return m.kind == Kind::Chunk && m.lifetimeMs == 0;
    }
    return false;
}
bool encode(const Packet& p, Bytes& output)
{
    if (!p.session || !p.epoch || !p.number || p.number == UINT64_MAX || !validAcks(p.acks)) return false;
    if (p.message && !validMessage(*p.message)) return false;
    const size_t body = p.acks.size() * 16 + (p.message ? MessageHeaderBytes + p.message->payload.size() : 0);
    if (body + HeaderBytes > MaxPacketBytes) return false;
    Bytes b;
    b.reserve(HeaderBytes + body);
    put(b, 0x4f524e31, 4); put(b, ProtocolVersion, 2); put(b, 0, 2);
    put(b, p.session, 8); put(b, p.epoch, 4); put(b, p.number, 8);
    put(b, p.acks.size(), 1); put(b, p.message ? 1 : 0, 1); put(b, body, 2);
    for (auto r : p.acks) { put(b, r.first, 8); put(b, r.last, 8); }
    if (p.message)
    {
        const auto& m = *p.message;
        put(b, uint8_t(m.kind), 1); put(b, uint8_t(m.lane), 1); put(b, 0, 2);
        put(b, m.id, 8); put(b, m.createdMs, 8); put(b, m.lifetimeMs, 2); put(b, m.payload.size(), 2);
        b.insert(b.end(), m.payload.begin(), m.payload.end());
    }
    output = std::move(b);
    return true;
}
bool decode(std::span<const uint8_t> bytes, Packet& output)
{
    if (bytes.size() < HeaderBytes || bytes.size() > MaxPacketBytes) return false;
    Reader r{bytes};
    Packet p;
    uint64_t magic, version, reserved, epoch, ackCount, count, length;
    if (!r.get(magic,4) || !r.get(version,2) || !r.get(reserved,2) ||
        !r.get(p.session,8) || !r.get(epoch,4) || !r.get(p.number,8) ||
        !r.get(ackCount,1) || !r.get(count,1) || !r.get(length,2)) return false;
    if (magic != 0x4f524e31 || version != ProtocolVersion || reserved || !p.session || !epoch ||
        !p.number || p.number == UINT64_MAX || ackCount > MaxAckRanges || count > 1 || length != bytes.size()-HeaderBytes) return false;
    p.epoch = uint32_t(epoch);
    for (size_t i = 0; i < ackCount; ++i)
    {
        AckRange a;
        if (!r.get(a.first,8) || !r.get(a.last,8)) return false;
        p.acks.push_back(a);
    }
    if (!validAcks(p.acks)) return false;
    if (count)
    {
        Message m;
        uint64_t kind, lane, lifetime, size;
        if (!r.get(kind,1) || !r.get(lane,1) || !r.get(reserved,2) || reserved ||
            !r.get(m.id,8) || !r.get(m.createdMs,8) || !r.get(lifetime,2) || !r.get(size,2) ||
            size > MaxPayloadBytes || size != bytes.size()-r.offset) return false;
        m.kind = Kind(kind); m.lane = Lane(lane); m.lifetimeMs = uint16_t(lifetime);
        m.payload.assign(bytes.begin()+r.offset, bytes.end());
        if (!validMessage(m)) return false;
        p.message = std::move(m);
        r.offset = bytes.size();
    }
    if (r.offset != bytes.size()) return false;
    output = std::move(p);
    return true;
}
bool ReplayWindow::contains(uint64_t n) const
{
    return n && n <= highest && highest-n < ReplayBits && seen[size_t(highest-n)];
}
bool ReplayWindow::acceptable(uint64_t n) const
{
    return n && n != UINT64_MAX && (n > highest || (highest-n < ReplayBits && !contains(n)));
}
bool ReplayWindow::accept(uint64_t n)
{
    if (!acceptable(n)) return false;
    if (n > highest)
    {
        const auto distance = n-highest;
        if (distance >= ReplayBits) seen.reset(); else seen <<= size_t(distance);
        highest = n;
    }
    seen.set(size_t(highest-n));
    return true;
}
std::vector<AckRange> ReplayWindow::ranges() const
{
    std::vector<AckRange> result;
    for (size_t i = 0; i < ReplayBits && i < highest; ++i)
    {
        if (!seen[i]) continue;
        const uint64_t last = highest-i;
        while (i+1 < ReplayBits && i+1 < highest && seen[i+1]) ++i;
        result.push_back({highest-i, last});
        if (result.size() == MaxAckRanges) break;
    }
    return result;
}
}
