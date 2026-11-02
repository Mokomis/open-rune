#pragma once
#include <cstdint>
#include <optional>
#include <deque>

namespace openrune::net
{
// Integer millisecond clock mapping. Never changes the local simulation clock.
// Samples must come from the current authenticated peer/session.
class ClockSync
{
public:
    struct Probe { uint64_t id, sent; };
    std::optional<Probe> request(uint64_t localNow);
    bool reply(uint64_t id, uint64_t remoteReceive, uint64_t remoteSend, uint64_t localNow);
    bool ready(uint64_t localNow) const;
    std::optional<uint64_t> toLocal(uint64_t remoteTime, uint64_t localNow) const;
    int64_t offsetMs() const { return offset; } // Remote minus local.
    uint64_t uncertaintyMs() const { return uncertainty; }
    uint64_t rejected() const { return rejects; }
    static constexpr uint64_t MaxTime = uint64_t(INT64_MAX)/4;
private:
    struct Sample { int64_t offset; uint64_t rtt, at; };
    std::optional<Probe> pending;
    std::deque<Sample> samples;
    uint64_t nextId = 1, nextRequest = 0, lastNow = 0, lastGood = 0, uncertainty = 0, rejects = 0;
    int64_t offset = 0;
    bool established = false;
    bool advance(uint64_t now);
};
}
