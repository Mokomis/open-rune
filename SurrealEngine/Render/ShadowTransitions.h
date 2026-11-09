#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>

// Fixed atlas budget: retain slot identities and fade out before replacing a
// light. No second atlas, duplicate light or extra shadow pass is required.
template<size_t N> class ShadowTransitions {
public:
    struct Slot { uintptr_t light=0,target=0;float strength=0; };
    std::array<Slot,N> slots{};
    void reset(){slots={};}
    void update(const std::vector<uintptr_t>& desired,float dt,size_t budget) {
        budget=std::min(budget,N);dt=std::clamp(dt,0.f,.05f);
        std::vector<uintptr_t> claimed;
        for(size_t i=0;i<N;++i) {
            auto& s=slots[i];s.target=0;
            if(i>=budget){s={};continue;}
            if(s.light && std::find(desired.begin(),desired.end(),s.light)!=desired.end()) {
                s.target=s.light;claimed.push_back(s.light);
            }
        }
        for(size_t i=0;i<budget;++i)if(!slots[i].target)
            for(auto light:desired)if(light && std::find(claimed.begin(),claimed.end(),light)==claimed.end()) {
                slots[i].target=light;claimed.push_back(light);break;
            }
        for(size_t i=0;i<budget;++i) {
            auto& s=slots[i];
            if(!s.light)s.light=s.target;
            if(s.light!=s.target) {
                s.strength=std::max(0.f,s.strength-dt/.16f);
                if(s.strength==0)s.light=s.target;
            }else s.strength=s.light?std::min(1.f,s.strength+dt/.16f):0;
        }
    }
};
