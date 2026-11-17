#pragma once
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <chrono>
namespace RuneSnow {
// -2: original-comparison weather; -1: automatic; 0..2: client override. Legacy Blizzard values fall back to Heavy.
inline constexpr int Original=-2, Automatic=-1, Heavy=2;
inline int ClampMode(int mode){return std::clamp(mode,Original,Heavy);}
inline std::atomic<int> Mode{Automatic};
inline uint32_t TestSeed=0;inline uint64_t TestPeriodMs=0;
inline uint32_t Hash(uint32_t n) {n^=n>>16;n*=0x7feb352du;n^=n>>15;n*=0x846ca68bu;return n^(n>>16);}
inline float Unit(uint32_t n) {return float(Hash(n)>>8)*(1.f/16777216.f);}
inline float Wrap(float v,float period) {return v-std::floor(v/period)*period;}
inline float Height(float time,uint32_t seed,float cameraZ) {
    float z=Wrap(Unit(seed)*768.f-time*(38.f+Unit(seed+1)*42.f),768.f);
    return z+std::floor((cameraZ-z+384.f)/768.f)*768.f;
}

inline int FlakesPerCell(int mode){return mode<=0?0:mode==1?4:10;}
inline int MaxFlakes(int mode){return mode<=0?0:mode==1?700:1800;}
inline float CoverageLimit(int mode){return mode<=0?0.f:mode==1?.025f:.06f;}

// Stateless schedule: a map has a seeded 3–7 minute interval, clear/light/heavy
// weights of40/35/25, and20-second fades. Random access is O(1), so
// late joiners never replay a long list of past weather changes.
struct Conditions {int from=0,to=0;float blend=1;bool operator==(const Conditions&)const=default;};
inline uint32_t NewSeed(){static std::atomic<uint32_t> serial{0};auto t=uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());auto s=Hash(uint32_t(t)^uint32_t(t>>32)^++serial);return s?s:1;}
inline uint64_t Period(uint32_t seed){return 180000+Hash(seed^0xb79a12d5u)%240001;}
inline int Level(uint32_t seed,uint64_t epoch){auto n=Hash(seed^Hash(uint32_t(epoch))^Hash(uint32_t(epoch>>32))^0xa8b5739du)%100;return n<40?0:n<75?1:Heavy;}
inline Conditions At(uint32_t seed,uint64_t elapsed,uint64_t period=0){
 if(!seed)return {};if(!period)period=Period(seed);period=std::max<uint64_t>(1000,period);
 uint64_t epoch=elapsed/period;int to=Level(seed,epoch);if(!epoch)return {to,to,1};
 float t=std::min(1.f,float(elapsed%period)/float(std::min<uint64_t>(20000,period/3)));t=t*t*(3-2*t);
 return {Level(seed,epoch-1),to,t};
}
// Derive remote age from synchronized "now", not by mapping the old origin
// into local time: a long-running match may predate the joining device's boot.
inline uint64_t RemoteAge(uint64_t origin,uint64_t localNow,int64_t offset){
 constexpr auto limit=uint64_t(INT64_MAX)/4;if(localNow>limit || offset>int64_t(limit) || offset<-int64_t(limit))return 0;
 int64_t remote=int64_t(localNow)+offset;return remote>0&&uint64_t(remote)>origin?uint64_t(remote)-origin:0;
}
struct Timeline {uint32_t seed=0;double anchor=0;uint64_t age=0;bool remote=false,ready=false;};
inline Timeline timeline;
inline void BeginMap(double now,bool remote){timeline={remote?0:(TestSeed?TestSeed:NewSeed()),now,0,remote,!remote};}
inline void Observe(uint32_t seed,uint64_t elapsed,double now){if(!seed)return;timeline={seed,now,elapsed,true,true};}
inline Conditions Current(double now){int mode=ClampMode(Mode.load());if(mode>=0)return {mode,mode,1};if(mode==Original || !timeline.ready)return {};auto delta=std::max(0.0,now-timeline.anchor);return At(timeline.seed,timeline.age+uint64_t(delta*1000),TestPeriodMs);}
struct Appearance {float count=0,limit=0,coverage=0,blizzard=0;};
inline Appearance Style(int level){level=std::clamp(level,0,Heavy);if(!level)return {};return {float(FlakesPerCell(level)),float(MaxFlakes(level)),CoverageLimit(level),0.f};}
inline Appearance Style(Conditions c){auto a=Style(c.from),b=Style(c.to);auto lerp=[&](float x,float y){return x+(y-x)*c.blend;};return {lerp(a.count,b.count),lerp(a.limit,b.limit),lerp(a.coverage,b.coverage),lerp(a.blizzard,b.blizzard)};}
}
