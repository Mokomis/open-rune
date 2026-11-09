#include "Render/ShadowTransitions.h"
#include <cassert>
#include <iostream>
#include <cmath>
int main(){
 for(int hz:{30,60,120,144,240}) {
    ShadowTransitions<3> s;float dt=1.f/hz;
    for(int i=0;i<hz;++i)s.update({1,2,3},dt,3);
    assert(s.slots[0].light==1 && s.slots[0].strength==1);
    s.update({3,2,1},dt,3);assert(s.slots[0].light==1 && s.slots[2].light==3);
    float previous=1;bool replaced=false;
    for(int i=0;i<hz;++i) {
        auto old=s.slots[0];s.update({4,2,3},dt,3);auto now=s.slots[0];
        if(old.light!=now.light){assert(now.strength==0);replaced=true;}
        else assert(std::abs(now.strength-old.strength)<=dt/.16f+.0001f);
        previous=now.strength;
        assert(s.slots[1].light==2 && s.slots[2].light==3);
    }
    assert(replaced && s.slots[0].light==4 && previous==1);
    s.update({4},dt,1);assert(!s.slots[1].light && !s.slots[2].light);
    s.reset();for(auto slot:s.slots)assert(!slot.light && slot.strength==0);
 }
 std::cout<<"SHADOW_TRANSITIONS_OK rates=5 stable_slots=1 bounded_budget=1 zero_strength_replacement=1\n";
}
