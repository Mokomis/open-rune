#include "openrune/net/Simulation.h"
#include <algorithm>
#include <stdexcept>
namespace openrune::net
{
bool FixedSimulation::advance(uint64_t now,const std::function<void(uint64_t,float)>& tick)
{
    if (!tick || now>ClockSync::MaxTime || (started && now<last)) return false;
    if (!started) { last=now; started=true; return true; }
    const auto elapsed=now-last;
    // Bound multiplication even after suspend; record the whole discarded debt.
    const auto seconds=elapsed/1000000, fraction=elapsed%1000000;
    const auto units=remainder+fraction*Hz;
    const auto due=seconds*Hz+units/1000000;
    const auto steps=std::min(uint64_t(MaxCatchUp),due);
    if (UINT64_MAX-count<steps || UINT64_MAX-dropped<due-steps) return false;
    last=now; remainder=units%1000000; dropped+=due-steps;
    for (uint64_t i=0;i<steps;++i) tick(++count,1.0f/Hz);
    return true;
}
std::optional<EntityRef> EntityRegistry::bind(uintptr_t handle)
{
    if (!handle) return {};
    auto old=handles.find(handle); if (old!=handles.end()) return old->second;
    if (handles.size()>=MaxEntities || nextId==UINT32_MAX) return {};
    EntityRef ref{nextId++,1}; handles.emplace(handle,ref); identities.emplace(ref.id,handle); return ref;
}
EntityRef EntityRegistry::find(uintptr_t handle) const
{ auto it=handles.find(handle); return it==handles.end()?EntityRef{}:it->second; }
uintptr_t EntityRegistry::resolve(EntityRef ref) const
{ auto it=identities.find(ref.id); return ref.generation==1 && it!=identities.end()?it->second:0; }
bool EntityRegistry::release(uintptr_t handle)
{ auto it=handles.find(handle); if (it==handles.end()) return false; identities.erase(it->second.id); handles.erase(it); return true; }
namespace
{
template<class T> std::optional<T> quantize(double v,double scale,double minimum,double maximum)
{
    if (!std::isfinite(v)) return {};
    double rounded=std::round(v*scale);
    if (!std::isfinite(rounded) || rounded<minimum || rounded>maximum) return {};
    return T(rounded);
}
}
std::optional<int32_t> quantizePosition(double v) { return quantize<int32_t>(v,8,-(1<<27),1<<27); }
std::optional<int16_t> quantizeVelocity(double v) { return quantize<int16_t>(v,4,INT16_MIN,INT16_MAX); }
std::optional<int16_t> quantizeAnimationRate(double v) { return quantize<int16_t>(v,256,INT16_MIN,INT16_MAX); }
Pose interpolatePose(const Entity& a,const Entity& b,double alpha)
{
    if (!std::isfinite(alpha)) alpha=1;
    alpha=std::clamp(alpha,0.0,1.0);
    if (a.ref!=b.ref || a.kind!=b.kind || a.life!=b.life) alpha=1;
    Pose p;
    for (size_t i=0;i<3;++i)
    {
        p.position[i]=(double(a.position[i])*(1-alpha)+double(b.position[i])*alpha)/8;
        p.velocity[i]=(double(a.velocity[i])*(1-alpha)+double(b.velocity[i])*alpha)/4;
        auto delta=int32_t(b.angles[i])-int32_t(a.angles[i]);
        if (delta>32768) delta-=65536;
        if (delta<-32768) delta+=65536;
        p.angles[i]=std::fmod(double(a.angles[i])+double(delta)*alpha+65536,65536);
    }
    auto phase=[&](uint16_t oldAnimation,uint16_t newAnimation,int16_t oldPhase,int16_t newPhase,uint8_t loopFlag)
    {
        double start=double(oldPhase)/32767.0,end=double(newPhase)/32767.0;
        if(!newAnimation || oldAnimation!=newAnimation || alpha==1)return end;
        double delta=end-start;
        if(start>=0 && end>=0 && (a.flags & loopFlag) && (b.flags & loopFlag))
        {
            if(delta<-.5)delta+=1;
            else if(delta>.5)delta-=1;
            double value=std::fmod(start+delta*alpha+1.0,1.0);
            return value;
        }
        if(delta<0 && start>=0)return end; // A non-looping attack restarted; never interpolate backward.
        return start+delta*alpha;
    };
    p.bodyPhase=phase(a.bodyAnimation,b.bodyAnimation,a.animationPhase,b.animationPhase,8);
    p.torsoPhase=phase(a.torsoAnimation,b.torsoAnimation,a.torsoPhase,b.torsoPhase,16);
    p.bodyRate=(a.bodyAnimation==b.bodyAnimation ?
        (double(a.animationRate)*(1-alpha)+double(b.animationRate)*alpha):double(b.animationRate))/256;
    p.torsoRate=(a.torsoAnimation==b.torsoAnimation ?
        (double(a.torsoRate)*(1-alpha)+double(b.torsoRate)*alpha):double(b.torsoRate))/256;
    return p;
}
}
