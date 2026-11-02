#pragma once
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace openrune::net
{
using std::size_t;
// Experimental inner protocol. Only authenticated plaintext may reach session state.
// SecureSession authenticates the envelope before delivering packets.
using Bytes = std::vector<uint8_t>;
constexpr size_t MaxPacketBytes = 1100; // Leave room inside 1200 for security framing.
constexpr size_t MaxPayloadBytes = 768;
constexpr size_t MaxAckRanges = 8;
constexpr size_t ReplayBits = 1024;
constexpr size_t ReliableWindow = 64;
constexpr uint16_t ProtocolVersion = 22;
inline constexpr const char* ProtocolAlpn = "open-rune-net/22";
enum class Lane : uint8_t { Gameplay = 0, Control = 1, Bulk = 2, Snapshots = 3 };
enum class Kind : uint8_t { State = 1, Event = 2, Chunk = 3, SnapshotPart = 4 };
struct AckRange
{
    uint64_t first = 0, last = 0;
    bool operator==(const AckRange&) const = default;
};
struct Message
{
    Kind kind = Kind::State;
    Lane lane = Lane::Gameplay;
    uint64_t id = 0;
    uint64_t createdMs = 0;
    uint16_t lifetimeMs = 0; // Gameplay/Snapshots only; reliable messages never silently expire.
    Bytes payload;
    bool operator==(const Message&) const = default;
};
struct Packet
{
    uint64_t session = 0;
    uint32_t epoch = 0;
    uint64_t number = 0;
    std::vector<AckRange> acks;
    std::optional<Message> message; // One record per packet in the first contract.
    bool operator==(const Packet&) const = default;
};
// Strict, canonical encoding. Failure never modifies the caller's output.
bool encode(const Packet&, Bytes&);
bool decode(std::span<const uint8_t>, Packet&);
bool validMessage(const Message&);
class ReplayWindow
{
public:
    bool contains(uint64_t number) const;
    bool acceptable(uint64_t number) const;
    bool accept(uint64_t number);
    std::vector<AckRange> ranges() const;
private:
    uint64_t highest = 0;
    std::bitset<ReplayBits> seen;
};
}
