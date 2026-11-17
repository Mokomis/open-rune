#pragma once
#include "VisibleRuneEffects.h"
#include "RuneBreath.h"
#include "RuneSnow.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include <map>
#include <set>

namespace RuneBreathPresentation {
struct Emitter {double next=0;int remaining=0;uint32_t seed=0;};
struct Puff {UPawn* owner;vec3 origin,velocity;double born;float scale;};
inline ULevel* level=nullptr;inline double lastTime=0;inline unsigned frames=0,emitted=0;
inline std::map<UPawn*,Emitter> emitters;
inline std::vector<Puff> puffs;
inline void Draw(VisibleFrame* frame) {
    if(frame->ShadowView || frame->PortalDepth || frame->MirrorFlag || !engine->LaunchInfo.IsRune())return;
    const double now=engine->TotalTime;
    if(level!=engine->Level || now<lastTime){level=engine->Level;emitters.clear();puffs.clear();frames=emitted=0;}
    lastTime=now;
    if(!RuneBreath::Enabled.load() || !RuneVisualEffects::Enabled.load()){emitters.clear();puffs.clear();return;}
    const auto eye=frame->ViewLocation.xyz();const float tanX=std::tan(frame->Frame.FovAngle*.00872664626f),tanY=tanX*frame->Frame.FY/frame->Frame.FX;
    std::vector<std::pair<float,UPawn*>> candidates;
    for(auto* actor:engine->Level->Actors)if(auto* p=UObject::TryCast<UPawn>(actor);RuneBreath::Eligible(p)) {
        auto d=p->Location()+vec3(0,0,p->CollisionHeight()*.5f)-eye;float z=dot(d,frame->ViewRotation.XAxis),distance=length(d);
        if(distance>1100 || z<15 || std::abs(dot(d,frame->ViewRotation.YAxis))>z*tanX+60 || std::abs(dot(d,frame->ViewRotation.ZAxis))>z*tanY+60)continue;
        candidates.emplace_back(distance,p);
    }
    std::sort(candidates.begin(),candidates.end(),[](auto a,auto b){return a.first<b.first;});if(candidates.size()>32)candidates.resize(32);
    std::set<UPawn*> visible;for(auto [distance,p]:candidates)visible.insert(p);
    std::erase_if(emitters,[&](const auto& e){return !visible.contains(e.first);});
    std::erase_if(puffs,[&](const Puff& p){return !visible.contains(p.owner)||now-p.born>.8||now<p.born;});
    for(auto [distance,p]:candidates) {
        auto it=emitters.find(p);
        if(it==emitters.end()) {
            uint32_t seed=2166136261u;for(unsigned char c:p->Name.ToString())seed=(seed^c)*16777619u;
            it=emitters.emplace(p,Emitter{now+RuneSnow::Unit(seed)*2.8,0,seed}).first;
        }
        auto& e=it->second;if(now<e.next)continue;
        if(e.remaining==0)e.remaining=3;
        vec3 mouth,forward;
        if(puffs.size()<96 && RuneBreath::Mouth(p,mouth,forward) && RuneBreath::Outside(mouth)) {
            auto inherited=p->Velocity();float speed=length(inherited);if(speed>50)inherited*=50.f/speed;
            float scale=std::clamp(p->DrawScale(),.5f,2.f);
            puffs.push_back({p,mouth,forward*(24.f*scale)+vec3(0,0,7.f*scale)+inherited*.08f,now,scale});++emitted;
            if(commandline->HasArg("","--trace-rune-breath"))LogMessage("RUNE_BREATH_EMIT actor="+p->Class->Name.ToString()+" x="+std::to_string(mouth.x)+" y="+std::to_string(mouth.y)+" z="+std::to_string(mouth.z));
        }
        --e.remaining;e.next=now+(e.remaining?.12:2.4+RuneSnow::Unit(e.seed+emitted)*.8);
    }
    auto texture=RuneEffects::Texture(0,false);int drawn=0;
    for(const auto& puff:puffs) {
        if(drawn>=48 || frame->EffectQuadBudget<=0)break;
        float age=float(now-puff.born),u=std::clamp(age/.8f,0.f,1.f);auto at=puff.origin+puff.velocity*age;
        auto d=at-eye;float z=dot(d,frame->ViewRotation.XAxis);if(z<12 || !RuneBreath::Outside(at))continue;
        float fade=std::clamp(age/.07f,0.f,1.f)*std::pow(1-u,1.5f)*std::clamp((1100-length(d))/250.f,0.f,1.f);
        float radius=(1.6f+u*8.f)*puff.scale;
        RuneEffects::Quad(frame,texture,at,frame->ViewRotation.YAxis*radius,frame->ViewRotation.ZAxis*(radius*.8f),vec3(.7f,.77f,.82f),.34f*fade,PF_Highlighted|PF_Unlit);++drawn;
    }
    if(commandline->HasArg("","--trace-rune-breath") && ++frames%120==0)LogMessage("RUNE_BREATH_FRAME eligible="+std::to_string(candidates.size())+" emitted="+std::to_string(emitted)+" drawn="+std::to_string(drawn)+" active="+std::to_string(puffs.size()));
}
}
