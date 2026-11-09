#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Render-only playback of a confirmed, non-looping animation. It never starts
// an attack, advances a wind-up into a strike, or invokes animation notifies.
class RuneAnimationPlayback
{
public:
    static constexpr double MaximumLead=.050, RecoveryGap=.250;
    struct Sample {
        uint64_t snapshot=0,tick=0;
        uint32_t life=0;
        uint16_t sequence=0;
        uintptr_t model=0;
        double phase=0,rate=0,end=1;
        bool looping=false;
    };
    void reset(){valid=false;}
    double present(const Sample& s,double now,double fallback)
    {
        if(!s.sequence || !s.snapshot || !std::isfinite(now) ||
           !std::isfinite(s.phase) || !std::isfinite(s.rate) || !std::isfinite(s.end) ||
           s.looping || s.phase<0 || s.rate<=0 || s.end<=0) {
            reset();return fallback;
        }
        bool fresh=!valid || s.snapshot!=snapshot;
        bool changed=!valid || s.life!=life || s.sequence!=sequence || s.model!=model ||
            now<renderedAt || (fresh && (s.tick<tick || now-receivedAt>RecoveryGap ||
            s.phase+2.0/32767<authoritative));
        if(changed) {
            phase=std::clamp(s.phase,0.0,s.end);renderedAt=receivedAt=now;
            valid=true;
        }
        if(fresh || changed) {
            snapshot=s.snapshot;tick=s.tick;life=s.life;sequence=s.sequence;model=s.model;
            authoritative=std::clamp(s.phase,0.0,s.end);receivedAt=now;
        }
        const double dt=std::clamp(now-renderedAt,0.0,.100);renderedAt=now;
        const double lead=s.rate*std::clamp(now-receivedAt,0.0,MaximumLead);
        const double target=std::min(s.end,authoritative+lead);
        // Slew the rate rather than replacing the visible phase at each packet.
        // Matching motion remains at its authored speed; corrections are small
        // and monotonic. Packet loss can hold at the bounded confirmed lead.
        const double error=target-phase-s.rate*dt;
        const double speed=s.rate*std::clamp(1+error*10/s.rate,.75,1.25);
        const double ceiling=std::max(phase,std::min(s.end,authoritative+s.rate*MaximumLead));
        phase=std::clamp(phase+speed*dt,phase,ceiling);
        return phase;
    }
private:
    bool valid=false;
    uint64_t snapshot=0,tick=0;
    uint32_t life=0;
    uint16_t sequence=0;
    uintptr_t model=0;
    double phase=0,authoritative=0,renderedAt=0,receivedAt=0;
};
