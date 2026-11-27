#pragma once
#include "Replication.h"
#include <cmath>
#include <functional>
#include <unordered_map>

namespace openrune::net
{
// 60 Hz authority, independent of display/network poll rate. Overload drops
// wall-clock debt, never invents simulated ticks or a giant physics step.
class FixedSimulation
{
public:
    static constexpr uint32_t Hz = 60, MaxCatchUp = 4;
    bool advance(uint64_t microseconds, const std::function<void(uint64_t,float)>& tick);
    uint64_t ticks() const { return count; }
    uint64_t droppedTicks() const { return dropped; }
    // Microseconds from now until advance() will run at least one tick.
    uint64_t untilNextTick(uint64_t now) const
    {
        if (!started || now < last) return 0;
        const uint64_t due = last+(1000000-remainder+Hz-1)/Hz;
        return due > now ? due-now : 0;
    }
private:
    uint64_t last = 0, remainder = 0, count = 0, dropped = 0;
    bool started = false;
};
// Handles are local identities (e.g. actor pointers), never sent on the wire.
// Numeric IDs are not reused within an epoch, including after pointer reuse.
class EntityRegistry
{
public:
    std::optional<EntityRef> bind(uintptr_t localHandle);
    EntityRef find(uintptr_t localHandle) const;
    uintptr_t resolve(EntityRef) const;
    bool release(uintptr_t localHandle);
    size_t size() const { return handles.size(); }
private:
    uint32_t nextId = 1;
    std::unordered_map<uintptr_t,EntityRef> handles;
    std::map<uint32_t,uintptr_t> identities;
};
// Checks precede narrowing; malformed floats must never invoke undefined casts.
std::optional<int32_t> quantizePosition(double);
std::optional<int16_t> quantizeVelocity(double);
std::optional<int16_t> quantizeAnimationRate(double);
struct Pose
{
    std::array<double,3> position{}, velocity{};
    std::array<double,3> angles{}; // Rune angle units, cyclic 65536
    double bodyPhase=0,torsoPhase=0,bodyRate=0,torsoRate=0;
};
// Presentation only. No collision, scripts or authority are invoked here.
Pose interpolatePose(const Entity& before, const Entity& after, double alpha);
}
