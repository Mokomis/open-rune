#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>

// Platform-neutral, transient output. Backends consume pulses; no Android,
// controller IDs, actors or save data cross this boundary. Channels express
// low/high-frequency intent, not a particular hardware motor arrangement.
namespace GameFeedback {
enum class Event { WeaponHit=1, ShieldBlock, Damage, Kick, Bash, Landing, Pickup };
struct Pulse { Event event=Event::WeaponHit; float low=0, high=0; int ms=0, priority=0; };
inline Pulse Describe(Event event,float strength=1) {
    if(!std::isfinite(strength))return {};
    strength=std::clamp(strength,0.0f,1.0f);
    Pulse p; p.event=event;
    switch(event) {
    case Event::WeaponHit:p={event,.55f,.75f,75,2};break;
    case Event::ShieldBlock:p={event,.35f,.9f,65,3};break;
    case Event::Damage:p={event,.9f,.6f,150,4};break;
    case Event::Kick:p={event,.65f,.25f,85,2};break;
    case Event::Bash:p={event,.75f,.55f,100,3};break;
    case Event::Landing:p={event,.7f,.15f,110,1};break;
    case Event::Pickup:p={event,.12f,.35f,35,0};break;
    default:return {};
    }
    p.low*=strength;p.high*=strength;
    if(strength==0)p.ms=0;
    return p;
}
using Clock=std::chrono::steady_clock;
inline std::mutex Mutex;
inline std::atomic<bool> Enabled{false};
inline Pulse Pending;
inline Clock::time_point PendingTime;
inline std::array<Clock::time_point,8> Last{};
inline void SetEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(Mutex);
    Enabled.store(enabled);Pending={};Last={};
}
inline bool Emit(Event event,float strength=1,Clock::time_point now=Clock::now()) {
    if(!Enabled.load())return false;
    Pulse p=Describe(event,strength);if(!p.ms)return false;
    std::lock_guard<std::mutex> lock(Mutex);
    if(!Enabled.load())return false;
    auto index=static_cast<unsigned>(event);
    if(index>=Last.size())return false;
    if(Last[index]!=Clock::time_point{}&&now-Last[index]<std::chrono::milliseconds(60))return false;
    Last[index]=now;
    // Never backlog impacts. A stronger event replaces a weaker one; same
    // priority combines maxima, not sums. Discard stale pending feedback.
    if(!Pending.ms||now-PendingTime>std::chrono::milliseconds(100)||p.priority>Pending.priority)Pending=p;
    else if(p.priority==Pending.priority) {
        Pending.low=std::max(Pending.low,p.low);Pending.high=std::max(Pending.high,p.high);
        Pending.ms=std::max(Pending.ms,p.ms);
    } else return false;
    PendingTime=now;return true;
}
inline Pulse Poll(Clock::time_point now=Clock::now()) {
    std::lock_guard<std::mutex> lock(Mutex);
    Pulse p=Pending;Pending={};
    return Enabled.load()&&now-PendingTime<=std::chrono::milliseconds(100)?p:Pulse{};
}
// Small JNI transport. Other backends can consume Pulse directly.
inline uint64_t Pack(Pulse p) {
    if(!p.ms)return 0;
    return uint64_t(p.ms)|(uint64_t(std::lround(p.low*255))<<10)|
        (uint64_t(std::lround(p.high*255))<<18)|(uint64_t(p.event)<<26)|(uint64_t(p.priority)<<30);
}
}
