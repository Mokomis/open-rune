#include "openrune/net/Prediction.h"
#include <algorithm>
#include <cmath>

namespace openrune::net
{
namespace {
bool sameIntent(const Input& a,const Input& b)
{return a.life==b.life && a.forward==b.forward && a.strafe==b.strafe && a.yaw==b.yaw && a.pitch==b.pitch && a.held==b.held;}
bool finite(const MovementPose& p)
{return std::all_of(p.position.begin(),p.position.end(),[](float v){return std::isfinite(v);}) &&
    std::all_of(p.velocity.begin(),p.velocity.end(),[](float v){return std::isfinite(v);});}
}
void MovementPrediction::reset()
{
    history.clear();acknowledgement={};base={};lastPresented={};correction={};
    baseUs=lastNow=lastPresent=lastTick=latestSent=0;life=0;owned={};enabled=initialized=false;
    ++metrics.resets;
}
bool MovementPrediction::sample(Input input,uint64_t now)
{
    if(now<lastNow || now>ClockSync::MaxTime/1000 || !input.life || input.forward<-1000 || input.forward>1000 ||
        input.strafe<-1000 || input.strafe>1000 || input.pitch<-16384 || input.pitch>16384 || (input.held&~7) ||
        (input.sequence && input.sequence<=latestSent))return false;
    lastNow=now;
    if(input.sequence)latestSent=input.sequence;
    input.createdMs=now;
    // Keep every sent marker for acknowledgement, but coalesce repeated local
    // samples. A render-rate change must not expand memory or replay work.
    if(!input.sequence && !history.empty() && sameIntent(input,history.back().input))return true;
    history.push_back({input,now*1000});
    while(history.size()>128 || (!history.empty() && now*1000-history.front().at>1000000))history.pop_front();
    return true;
}
MovementPose MovementPrediction::replay(uint64_t now,const Step& step)
{
    auto result=base;
    if(!enabled || !step || history.empty() || now>ClockSync::MaxTime/1000)return result;
    uint64_t end=now*1000;
    if(end<=baseUs)return result;
    if(end-baseUs>MaxLeadMs*1000){end=baseUs+MaxLeadMs*1000;++metrics.limited;}
    Input intent=history.front().input;size_t cursor=0;
    while(cursor<history.size() && history[cursor].at<=baseUs)intent=history[cursor++].input;
    uint64_t at=baseUs;unsigned steps=0;
    while(at<end && steps<64)
    {
        while(cursor<history.size() && history[cursor].at<=at)intent=history[cursor++].input;
        auto until=std::min(end,at+16667);
        if(cursor<history.size())until=std::min(until,history[cursor].at);
        if(until<=at)break;
        auto next=result;
        if(intent.life!=life || !step(next,intent,acknowledgement,float(until-at)/1000000.0f) || !finite(next))break;
        result=next;at=until;++steps;++metrics.replayedSteps;
    }
    return result;
}
bool MovementPrediction::checkpoint(const Snapshot& snapshot,EntityRef identity,uint64_t now,const Step& step)
{
    if(now<lastNow || now>ClockSync::MaxTime/1000 || !snapshot.tick || snapshot.tick<=lastTick ||
        !validMovementAck(snapshot.movement,snapshot.world))return false;
    auto found=std::find_if(snapshot.world.begin(),snapshot.world.end(),[&](const auto& e){return e.ref==identity;});
    if(found==snapshot.world.end() || found->kind!=EntityKind::Pawn)return false;
    if(snapshot.movement && (snapshot.movement->ref!=identity || snapshot.movement->input>latestSent))return false;
    auto old=replay(now,step);
    bool sameLife=initialized && owned==identity && life==found->life;
    lastTick=snapshot.tick;lastNow=now;owned=identity;life=found->life;++metrics.checkpoints;
    for(size_t i=0;i<3;++i){base.position[i]=float(found->position[i])/8;base.velocity[i]=float(found->velocity[i])/4;}
    base.mode=snapshot.movement?snapshot.movement->mode:uint8_t(MovementUnsupported);
    enabled=false;baseUs=now*1000;
    if(snapshot.movement && snapshot.movement->mode && found->health>0)
    {
        const auto& ack=*snapshot.movement;
        auto input=std::find_if(history.begin(),history.end(),[&](const auto& s){return s.input.sequence==ack.input && s.input.life==life;});
        if(input!=history.end()) {
            auto checkpointUs=input->at+uint64_t(ack.heldTicks)*1000000/60;
            // Stalled/unknown/implausibly future checkpoints fall back cleanly.
            if(checkpointUs<=now*1000+20000 && now*1000<=checkpointUs+MaxLeadMs*1000) {
                acknowledgement=ack;baseUs=checkpointUs;enabled=true;
                // Retain the acknowledged marker even if its held duration
                // extends beyond another local sample; replay starts at baseUs.
                history.erase(history.begin(),input);
            } else if(checkpointUs>now*1000+20000)++metrics.future;
            else ++metrics.stale;
        } else ++metrics.missing;
    } else ++metrics.unsupported;
    auto predicted=replay(now,step);double error=0;
    for(size_t i=0;i<3;++i){double d=old.position[i]-predicted.position[i];error+=d*d;}
    error=std::sqrt(error);
    if(sameLife)metrics.maxCorrection=std::max(metrics.maxCorrection,error);
    // An authority-confirmed takeoff can arrive after much of its arc has
    // elapsed. Ease that bounded catch-up rather than popping upward in one frame.
    // Landing and unsupported/special transitions still reset immediately.
    const bool takeoff=old.mode==MovementWalking && base.mode==MovementFalling;
    const double limit=base.mode==MovementFalling?96:64;
    if(sameLife && enabled && (old.mode==base.mode || takeoff) && error<limit) {
        for(size_t i=0;i<3;++i)correction[i]+=old.position[i]-predicted.position[i];
        double length=0;for(float v:correction)length+=double(v)*v;
        if(length>limit*limit)correction={};
    } else {correction={};++metrics.resets;}
    initialized=true;return true;
}
MovementPose MovementPrediction::present(uint64_t now,const Step& step)
{
    if(now<lastNow || now>ClockSync::MaxTime/1000)return lastPresented;
    auto pose=replay(now,step);
    auto elapsed=lastPresent && now>=lastPresent?now-lastPresent:0;
    float decay=std::exp(-float(std::min<uint64_t>(elapsed,1000))/65.0f);
    for(size_t i=0;i<3;++i){correction[i]*=decay;pose.position[i]+=correction[i];}
    lastNow=lastPresent=now;lastPresented=pose;return pose;
}
}
