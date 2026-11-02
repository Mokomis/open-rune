#include "openrune/net/MeleeTiming.h"
#include <stdexcept>
#include <iostream>
#include <limits>
using openrune::net::MeleeTiming;
#define CHECK(v) do{if(!(v))throw std::runtime_error(#v);}while(0)
int main(){try {
    CHECK(!MeleeTiming::ready(1000,60,1032,62));
    CHECK(!MeleeTiming::ready(1000,60,1033,61));
    CHECK(MeleeTiming::ready(1000,60,1033,62));
    CHECK(!MeleeTiming::ready(1000,60,999,62));
    CHECK(!MeleeTiming::ready(UINT64_MAX-5,60,2,62));
    CHECK(MeleeTiming::lateBlock(990,1015,1000));
    CHECK(MeleeTiming::lateBlock(1000,1033,1000));
    CHECK(!MeleeTiming::lateBlock(1001,1015,1000));
    CHECK(!MeleeTiming::lateBlock(990,1034,1000));
    CHECK(!MeleeTiming::lateBlock(899,1001,1000));
    CHECK(!MeleeTiming::lateBlock(990,999,1000));
    CHECK(!MeleeTiming::lateBlock(UINT64_MAX,5,0));
    std::cout<<"PASS melee commitment watermark, late/on-time defense bounds, stalls and overflow\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
