#include "GameFeedback.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace GameFeedback;
int main() {
    auto now=Clock::now();
    assert(!Emit(Event::WeaponHit,1,now));
    SetEnabled(true);
    assert(!Emit(Event::Damage,std::numeric_limits<float>::quiet_NaN(),now));
    assert(!Emit(Event::Damage,0,now));
    assert(Emit(Event::WeaponHit,.5f,now));
    assert(!Emit(Event::WeaponHit,1,now)); // multiple joints, one pulse
    assert(Emit(Event::Damage,1,now)); // incoming hit takes precedence
    assert(!Emit(Event::Pickup,1,now));
    auto p=Poll(now);assert(p.event==Event::Damage&&p.ms==150&&p.low<=1&&p.high<=1);
    assert(!Poll(now).ms);
    assert(Emit(Event::Landing,1,now));
    assert(!Poll(now+std::chrono::milliseconds(101)).ms);
    SetEnabled(false);assert(!Poll(now).ms&&!Emit(Event::Damage,1,now));
    SetEnabled(true);assert(Emit(Event::Damage,1,now));SetEnabled(false);SetEnabled(true);assert(!Poll(now).ms);
    for(int i=1;i<=7;i++) {
        auto event=static_cast<Event>(i);auto full=Describe(event,1),weak=Describe(event,.35f);
        assert(full.ms>0&&full.ms<=180&&weak.low<full.low&&weak.high<full.high);
        auto packed=Pack(full);assert((packed&1023)==unsigned(full.ms));assert(((packed>>26)&15)==unsigned(i));
    }
    std::cout<<"PASS: feedback priority, coalescing, expiry, cancellation, bounds and transport\n";
}
