#include "SurrealEngine/Render/ShadowSettings.h"
#include <bit>
#include <iostream>
#include <stdexcept>
template<int N>void check(){
    using Layout=RuneShadows::SlotLayout<N>;
    for(int lane=0;lane<8;++lane)for(int slot=0;slot<N;++slot){auto packed=Layout::Encode(lane,slot);if(Layout::Decode(packed,lane)!=slot)throw std::runtime_error("slot encoding");for(int other=0;other<8;++other)if(other!=lane&&Layout::Decode(packed,other)!=-1)throw std::runtime_error("cross-lane contamination");}
    uint32_t all=0;for(int lane=0;lane<8;++lane)all|=Layout::Encode(lane,lane%N);
    for(int lane=0;lane<8;++lane)if(Layout::Decode(all,lane)!=lane%N)throw std::runtime_error("packed light set");
    if(Layout::Encode(8,0)||Layout::Encode(0,N)||Layout::Encode(-1,0))throw std::runtime_error("invalid index");
}
int main(){check<3>();check<8>();if(std::bit_cast<int32_t>(RuneShadows::SlotLayout<8>::Encode(7,7))>=0)throw std::runtime_error("high bit not exercised");if(RuneShadows::ChoiceBudget(0)!=1||RuneShadows::ChoiceBudget(1)!=3||RuneShadows::ChoiceBudget(2)!=4||RuneShadows::ChoiceBudget(3)!=6||RuneShadows::ChoiceBudget(4)!=8)throw std::runtime_error("saved setting migration");RuneShadows::RequestedBudget=8;if(RuneShadows::Budget(4096)!=RuneShadows::Capacity)throw std::runtime_error("platform clamp");RuneShadows::RequestedBudget=0;if(RuneShadows::Budget(4096)!=3||RuneShadows::Budget(0)!=1)throw std::runtime_error("legacy setting");std::cout<<"PASS: 3/8-slot encoding, sign bit, atlas lanes, saved choices and platform clamp\n";}
