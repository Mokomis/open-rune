#pragma once
#include "Replication.h"

namespace openrune::net
{
// A visual clock, independent of gameplay/ACK installation. Retains only
// installed, authenticated worlds. Never extrapolates beyond received state.
class PresentationTimeline
{
public:
    static constexpr size_t Capacity=16;
    static constexpr double MinimumDelayMs=1000.0/30,MaximumDelayMs=100;
    struct Frame { const Snapshot* before=nullptr; const Snapshot* after=nullptr; double alpha=1; };
    struct Stats { double targetDelayMs=MinimumDelayMs,bufferedMs=0; uint64_t holds=0,resets=0,catchups=0; };
    bool push(const Snapshot&,uint64_t arrivalMs);
    Frame sample(uint64_t localMs);
    void reset();
    size_t size() const { return history.size(); }
    const Stats& stats() const { return metrics; }
private:
    struct Received { Snapshot snapshot; uint64_t arrival; double serverMs; };
    std::deque<Received> history;
    std::deque<std::pair<uint64_t,double>> offsets;
    std::deque<std::pair<uint64_t,double>> intervals;
    uint64_t lastNow=0;
    double cursor=0,offset=0;
    bool started=false;
    Stats metrics;
};
}
