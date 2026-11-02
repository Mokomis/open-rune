#pragma once
#include <cstdint>
namespace openrune::net {
// A committed contact is never rolled back. History allowance is not a grant
// of extra reach or shortened wind-up: only defense may arrive in this window.
struct MeleeTiming {
    static constexpr uint64_t GraceMs=33, GraceTicks=2, MaxDefenseAgeMs=100;
    static bool ready(uint64_t contactMs,uint64_t contactTick,uint64_t now,uint64_t tick) {
        return now>=contactMs && now-contactMs>=GraceMs && tick>=contactTick && tick-contactTick>=GraceTicks;
    }
    static bool lateBlock(uint64_t pressed,uint64_t received,uint64_t contact) {
        return pressed<=contact && contact-pressed<=MaxDefenseAgeMs && received>contact &&
            received-contact<=GraceMs && received>=pressed;
    }
};
}
