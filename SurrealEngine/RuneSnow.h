#pragma once
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cmath>
namespace RuneSnow {
inline std::atomic<int> Mode{0}; // Off / Light / Heavy. Cosmetic; no network actors.
inline uint32_t Hash(uint32_t n) {n^=n>>16;n*=0x7feb352du;n^=n>>15;n*=0x846ca68bu;return n^(n>>16);}
inline float Unit(uint32_t n) {return float(Hash(n)>>8)*(1.f/16777216.f);}
inline float Wrap(float v,float period) {return v-std::floor(v/period)*period;}
inline float Height(float time,uint32_t seed,float cameraZ) {
    float z=Wrap(Unit(seed)*768.f-time*(38.f+Unit(seed+1)*42.f),768.f);
    return z+std::floor((cameraZ-z+384.f)/768.f)*768.f;
}
inline int FlakesPerCell(int mode){return mode==1?4:10;}
inline int MaxFlakes(int mode){return mode==1?700:1800;}
inline float CoverageLimit(int mode){return mode==1?.025f:.06f;}
}
