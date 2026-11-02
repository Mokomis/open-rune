#pragma once
#include "Replication.h"
#include <functional>

namespace openrune::net
{
struct MovementPose
{
    std::array<float,3> position{}, velocity{};
    uint8_t mode=MovementUnsupported; // Local replay only; 3 means collision-stopped falling.
};
// Bounded local presentation prediction. Step must be a read-only collision
// simulation: no VM events, touch/pickup, damage, audio or changes to actors.
// Only the checkpoint's acknowledged input may retire history; packet receipt
// and action admission cannot do so. Unsupported locomotion stays authoritative.
class MovementPrediction
{
public:
    using Step = std::function<bool(MovementPose&,const Input&,const MovementAck&,float)>;
    static constexpr uint64_t MaxLeadMs=350;
    bool sample(Input input,uint64_t now); // sequence=0 for an unsent local sample
    bool checkpoint(const Snapshot&,EntityRef owned,uint64_t now,const Step&);
    MovementPose present(uint64_t now,const Step&);
    void reset();
    bool active() const { return enabled; }
    size_t historySize() const { return history.size(); }
    struct Stats { uint64_t checkpoints=0,replayedSteps=0,resets=0,limited=0,unsupported=0,missing=0,stale=0,future=0; double maxCorrection=0; };
    const Stats& stats() const { return metrics; }
private:
    struct Sample { Input input; uint64_t at; };
    std::deque<Sample> history;
    MovementAck acknowledgement;
    MovementPose base,lastPresented;
    std::array<float,3> correction{};
    uint64_t baseUs=0,lastNow=0,lastPresent=0,lastTick=0,latestSent=0;
    uint32_t life=0;
    EntityRef owned;
    bool enabled=false,initialized=false;
    Stats metrics;
    MovementPose replay(uint64_t now,const Step&);
};
}
