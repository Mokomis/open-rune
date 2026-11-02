#pragma once
#include "RuneBloodPresentation.h"
#include "Package/PackageManager.h"
#include "VisibleRuneEffects.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Collision/TopLevel/CollisionHit.h"

namespace RuneBloodPresentation {
struct Tracked {UActor* actor=nullptr;Wound wound;vec3 position=vec3(0);unsigned seen=0;};
struct Drop {bool live=false;vec3 p=vec3(0),v=vec3(0);float radius=1,age=0;};
inline std::map<int,Tracked> tracked;
inline std::array<Drop,24> drops{};
struct Stain {bool live=false;vec3 p=vec3(0);float radius=1;};
inline std::array<Stain,256> stains{};
inline size_t stainCursor=0;
inline void Land(const Drop& d) {stains[stainCursor++%stains.size()]={true,d.p,d.radius};}
inline void ClearTransient() {tracked.clear();drops={};}
inline size_t cursor=0;inline double previous=-1;inline unsigned serial=0,bursts=0,emitted=0,landed=0;
inline void Reset() {ClearTransient();stains={};stainCursor=0;previous=-1;serial=bursts=emitted=landed=0;cursor=0;}
inline CollisionHit Trace(vec3 a,vec3 b) {TraceFlags f;f.world=true;return engine->Level->Collision.TraceFirstHit(a,b,nullptr,vec3(0),f);}
inline void Draw(VisibleFrame* frame) {
    if(frame->ShadowView || frame->PortalDepth || frame->MirrorFlag || !engine->LaunchInfo.IsRune())return;
    auto* rules=engine->packages->FindClass("Engine.GameInfo")->GetDefaultObject<UObject>();
    if(rules->GetBool("bVeryLowGore")){Reset();return;}
    if(!RuneVisualEffects::Enabled.load()){ClearTransient();previous=-1;return;}
    double now=engine->LevelInfo->TimeSeconds();
    float dt=previous<0?0:float(std::clamp(now-previous,0.,.05));bool gap=previous>=0 && (now-previous>.5 || now<previous);
    previous=now;++serial;
    if(gap){ClearTransient();}
    // A real authored red-blood impact must coincide with the health loss.
    // This excludes blocked blows, falls, poison and bloodless/green enemies.
    std::vector<vec3> hits;hits.reserve(16);
    for(auto* a:engine->Level->Actors)if(a && !a->bDeleteMe() && !a->bHidden() && int(a->DrawType())==8 && RuneParticles::IsSystem(a) && RedEmitter(a) &&
        a->GetFloat("CurrentTime")<.20f && hits.size()<64)hits.push_back(a->Location());
    int spawnBudget=8;
    int airborne=int(std::count_if(drops.begin(),drops.end(),[](const Drop& d){return d.live;}));
    for(auto* a:engine->Level->Actors) {
        auto* p=UObject::TryCast<UPawn>(a);
        if(!p || p->bDeleteMe() || p->bHidden() || length(p->Location()-frame->ViewLocation.xyz())>1800)continue;
        auto it=tracked.find(p->Index);if(it==tracked.end() && tracked.size()>=64)continue;
        auto& t=tracked[p->Index];bool reset=t.actor!=p || length(t.position-p->Location())>256;
        bool water=p->Region().Zone && p->Region().Zone->bWaterZone();
        bool hit=std::any_of(hits.begin(),hits.end(),[&](vec3 h){return length(h-p->Location())<96;});
        if(t.wound.Observe(p->Health(),now,hit,reset||water))++bursts;
        t.actor=p;t.position=p->Location();t.seen=serial;
        if(!water && spawnBudget>0 && airborne<24 && t.wound.Emit(now)) {
            --spawnBudget;++airborne;++cursor;
            // Reuse an inactive droplet slot; never evict a floor stain at spawn.
            auto& d=*std::find_if(drops.begin(),drops.end(),[](const Drop& d){return !d.live;});d={};d.live=true;
            float angle=float(cursor)*2.399963f;
            d.p=p->Location()+vec3(std::cos(angle)*8,std::sin(angle)*8,-p->CollisionHeight()*.2f);
            d.v=p->Velocity()*.3f+vec3(std::cos(angle)*25,std::sin(angle)*25,-35);
            d.radius=1.2f+float(cursor%3)*.35f;++emitted;
        }
    }
    for(auto it=tracked.begin();it!=tracked.end();)if(it->second.seen!=serial)it=tracked.erase(it);else ++it;
    auto tex=RuneEffects::Texture(0,false);
    for(auto& d:drops)if(d.live) {
        d.age+=dt;if(d.age>1.5f){d.live=false;continue;}
        {
            d.v.z-=700*dt;vec3 to=d.p+d.v*dt;auto hit=Trace(d.p,to);
            if(hit.Actor && hit.Fraction<1) {
                if(hit.Actor!=engine->LevelInfo || hit.Normal.z<.85f){d.live=false;continue;}
                vec3 point=d.p+(to-d.p)*hit.Fraction;float r=d.radius*1.5f;bool safe=true;
                // Reject ledges/steps: never let a spot float over empty space.
                for(vec3 o:{vec3(r,r,0),vec3(-r,r,0),vec3(r,-r,0),vec3(-r,-r,0)}) {
                    auto test=Trace(point+o+vec3(0,0,2),point+o-vec3(0,0,2));
                    if(test.Actor!=engine->LevelInfo || test.Fraction>=1 || std::abs(test.Fraction-.5f)>.15f){safe=false;break;}
                }
                if(!safe){d.live=false;continue;}
                d.p=point+vec3(0,0,.35f);d.radius=r;Land(d);d.live=false;++landed;continue;
            } else d.p=to;
        }
        if(frame->EffectQuadBudget<=0 || length(d.p-frame->ViewLocation.xyz())>1800)continue;
        float opacity=std::clamp((1.5f-d.age)*4,0.f,1.f);
        vec3 side=frame->ViewRotation.YAxis*d.radius;
        vec3 up=frame->ViewRotation.ZAxis*d.radius*2.f;
        RuneEffects::Quad(frame,tex,d.p,side,up,vec3(.48f,.060f,.035f),opacity*.85f,PF_Highlighted);
    }
    // Map-local history: fixed storage, oldest landing replaced on overflow.
    // Draw only the nearest visible 96; distant stains remain in the history.
    std::vector<std::pair<float,const Stain*>> visible;visible.reserve(stains.size());
    for(const auto& stain:stains)if(stain.live) {
        vec3 delta=stain.p-frame->ViewLocation.xyz();float distance=dot(delta,delta);
        if(distance<1800.f*1800.f && dot(delta,frame->ViewRotation.XAxis)>0)visible.emplace_back(distance,&stain);
    }
    std::sort(visible.begin(),visible.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    int count=0;
    for(const auto& entry:visible) {
        if(count++>=96 || frame->EffectQuadBudget<=0)break;
        const auto& stain=*entry.second;
        RuneEffects::Quad(frame,tex,stain.p,vec3(stain.radius,0,0),vec3(0,stain.radius,0),vec3(.48f,.060f,.035f),.85f,PF_Highlighted);
    }
}
}
