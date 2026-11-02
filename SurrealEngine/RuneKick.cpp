#include "Precomp.h"
#include "RuneKick.h"
#include "RuneDualWield.h"
#include "RuneFeedback.h"
#include "Engine.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "VM/ScriptCall.h"
#include "Utils/Random.h"
#include "Utils/Logger.h"
#ifdef OPENRUNE_ENABLE_NETWORK
#include "RuneNetwork.h"
#endif
#include <algorithm>
#include <cmath>

namespace RuneKick {
namespace {
vec3 Forward(UPawn* p){float yaw=p->Rotation().YawRadians();return vec3(std::cos(yaw),std::sin(yaw),0);}
bool Alive(UPawn* p){return p && !p->bDeleteMe() && !p->bStatic() && !p->bHidden() && p->Health()>0;}
bool InRange(UPawn* a,UPawn* b){
    vec3 d=b->Location()-a->Location();float z=std::abs(d.z);d.z=0;float distance=length(d);
    return distance>0.01f && distance<=a->CollisionRadius()+b->CollisionRadius()+48 && z<=a->CollisionHeight()+b->CollisionHeight()*.5f && dot(Forward(a),d/distance)>=.65f;
}
void Sound(UPawn* p){if(p->HasProperty("KickSound"))if(auto* s=p->GetUObject("KickSound"))CallEvent(p,"PlaySound",{ExpressionValue::ObjectValue(s)});}
}
bool Compatible(UPawn* p){
    if(!engine->LaunchInfo.IsRune() || !Alive(p) || !(p->IsA("RunePlayer") || p->IsA("ScriptPawn")))return false;
    auto* m=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(p));
    // Generic GetSequence falls back to the first animation when a name is absent.
    if(m)for(const auto& seq:m->Sequences)if(seq.Info.Name=="neutral_kick")
        for(const auto& notify:seq.Info.Notifys)if(notify.Function=="UseNotify" && notify.Time>0 && notify.Time<1)return true;
    return false;
}
UPawn* Target(UPawn* a){
    UPawn* best=nullptr;float nearest=1e10f;
    for(auto* obj:a->XLevel()->Actors){auto* p=UObject::TryCast<UPawn>(obj);
        if(p==a || !Alive(p) || !InRange(a,p))continue;
        if(p->Physics()!=PHYS_Walking && p->Physics()!=PHYS_Falling)continue;
        if(auto* player=UObject::TryCast<UPlayerPawn>(p);player && player->ViewTarget())continue;
        if(!UObject::TryCast<UPlayerPawn>(a) && p!=a->Enemy())continue;
        float distance=length(p->Location()-a->Location());
        if(distance<nearest && a->FastTrace(p->Location()+vec3(0,0,10),a->Location()+vec3(0,0,10))){best=p;nearest=distance;}
    }return best;
}
bool Threat(UObject* obj){
    if(!engine->LaunchInfo.IsRune() || !Enabled.load())return false;
    auto* observer=UObject::TryCast<UPawn>(obj);auto* enemy=observer?observer->Enemy():nullptr;
    return Alive(observer) && Alive(enemy) && enemy->RuneKickActive && enemy->RuneKickPending && InRange(enemy,observer) && observer->FastTrace(enemy->Location(),observer->Location());
}
bool ShieldBlocks(UPawn* target,UPawn* attacker){
    if(RuneDualWield::Equipped(target))return false;
    auto* shield=UObject::TryCast<UActor>(target->GetUObject("Shield"));
    if(!shield || shield->bDeleteMe() || shield->GetInt("Health")<=0 || shield->GetStateName()!="Active")return false;
    auto d=attacker->Location()-target->Location();d.z=0;
    return length(d)>.01f && dot(Forward(target),normalize(d))>=.25f;
}
bool Start(UPawn* p){
    if(RuneBloodlust::Busy(p) || !Enabled.load() || !Compatible(p) || p->RuneKickActive || p->RuneKickCooldown>0 || p->Physics()!=PHYS_Walking)return false;
    bool player=UObject::TryCast<UPlayerPawn>(p)!=nullptr;
    if(player){auto* pp=UObject::Cast<UPlayerPawn>(p);if(pp->ViewTarget() || p->GetStateName()!="PlayerWalking")return false;}
    else if(p->GetStateName()!="Fighting")return false;
    auto* proxy=UObject::TryCast<UActor>(p->GetUObject("AnimProxy"));
    if(proxy && proxy->GetStateName()!="Idle")return false;
    if(p->GetBool("bSwingingHigh") || p->GetBool("bSwingingLow"))return false;
    if(!player && !Target(p))return false;
    if(auto* weapon=p->GetUObject("Weapon")){CallEvent(weapon,"FinishAttack");CallEvent(weapon,"DisableSwipeTrail");}
    p->SetObject("UseActor",nullptr);
    p->RuneKickActive=true;p->RuneKickPending=true;p->RuneKickCooldown=player?2.2f:4.5f;
    CallEvent(p,"PlayUninterruptedAnim",{ExpressionValue::NameValue("neutral_kick")});
    Sound(p);
    LogMessage("RUNE_KICK_START actor="+p->Class->Name.ToString()+" player="+std::to_string(player));
    return true;
}
bool Notify(UObject* obj){
    auto* actor=UObject::TryCast<UActor>(obj);if(!actor || !engine->LaunchInfo.IsRune())return false;
    auto* p=UObject::TryCast<UPawn>(actor);
    if(!p && actor->IsA("AnimationProxy"))p=UObject::TryCast<UPawn>(actor->Owner());
    if(!p || !p->RuneKickActive)return false;
    // Both body and proxy contain UseNotify. Consume both, strike only once,
    // and never dispatch an object/door UseTrigger for a combat kick.
    if(!p->RuneKickPending)return true;
    p->RuneKickPending=false;
    auto* target=Enabled.load() && Alive(p)?Target(p):nullptr;
    if(!target || target->RuneKickImmunity>0){
        LogMessage("RUNE_KICK_MISS actor="+p->Class->Name.ToString());
#ifdef OPENRUNE_ENABLE_NETWORK
        if(engine->NetworkCombatAuthority)engine->NetworkCombatAuthority->recordCombat(p,nullptr,false,false,false);
#endif
        return true;
    }
    bool blocked=ShieldBlocks(target,p);
#ifdef OPENRUNE_ENABLE_NETWORK
    if(engine->NetworkCombatAuthority && engine->NetworkCombatAuthority->deferContact(p,target,blocked,nullptr,
        [p,target](bool defended){CommitImpact(p,target,defended);}))return true;
#endif
    CommitImpact(p,target,blocked);return true;
}
void CommitImpact(UPawn* p,UPawn* target,bool blocked)
{
    int before=target->Health();
    bool crossed=!blocked && RuneDualWield::Blocks(target,p);
#ifdef OPENRUNE_ENABLE_NETWORK
    if(engine->NetworkCombatAuthority)engine->NetworkCombatAuthority->prepareCombat(p,target);
#endif
    auto d=target->Location()-p->Location();d.z=0;d=length(d)>.01f?normalize(d):Forward(p);
    float weight=std::clamp(100.0f/std::max(1.0f,target->Mass()),.25f,1.25f);
    target->RuneKickImmunity=1.2f;
    if(blocked){
        target->RuneKickRecoil=d*(6.0f*weight);target->RuneKickRecoilTime=.12f;
        CallEvent(target->GetUObject("Shield"),"PlayHitSound",{ExpressionValue::NameValue("blunt")});
    }else{
        CallEvent(target,"JointDamaged",{ExpressionValue::IntValue(5),ExpressionValue::ObjectValue(p),ExpressionValue::VectorValue(target->Location()),ExpressionValue::VectorValue(vec3(0)),ExpressionValue::NameValue("blunt"),ExpressionValue::IntValue(0)});
        if(crossed) { target->RuneKickRecoil=d*(24.f*weight);target->RuneKickRecoilTime=.18f;RuneDualWield::BreakGuard(target,.3f); }
        else if(Alive(target))CallEvent(target,"AddVelocity",{ExpressionValue::VectorValue(d*(440.0f*weight)+vec3(0,0,110.0f*weight))});
    }
    LogMessage("RUNE_KICK_HIT attacker="+p->Class->Name.ToString()+" target="+target->Class->Name.ToString()+" blocked="+std::to_string(blocked)+" health_before="+std::to_string(before)+" health_after="+std::to_string(target->Health())+" weight="+std::to_string(weight));
    RuneFeedback::Impact(p,target,blocked,false);
    p->RuneKickHits++;if(blocked)p->RuneKickBlocks++;
}
void Tick(UPawn* p,float elapsed){
    if(!engine->LaunchInfo.IsRune())return;
    bool pending=p->RuneKickPending;
    p->RuneKickCooldown=std::max(0.0f,p->RuneKickCooldown-elapsed);p->RuneKickImmunity=std::max(0.0f,p->RuneKickImmunity-elapsed);
    if(p->RuneKickActive && (!Alive(p) || p->GetStateName()!="Uninterrupted")){p->RuneKickActive=false;p->RuneKickPending=false;}
    if(!Enabled.load())p->RuneKickPending=false;
#ifdef OPENRUNE_ENABLE_NETWORK
    if(pending && !p->RuneKickPending && engine->NetworkCombatAuthority)
        engine->NetworkCombatAuthority->cancelCombat(p,openrune::net::CombatMove::Kick);
#endif
    if(UObject::TryCast<UPlayerPawn>(p) || !Enabled.load() || !Alive(p))return;
    if(RuneTactics::SuppressRandomSpecial(p))return;
    p->RuneKickThink-=elapsed;
    if(p->RuneKickThink<=0){p->RuneKickThink=.45f;if(p->GetStateName()=="Fighting" && p->RuneKickCooldown<=0 && Compatible(p) && Target(p) && FRand()<.22f)Start(p);}
}
void Recoil(UPawn* p,float elapsed){
    if(!engine->LaunchInfo.IsRune() || p->RuneKickRecoilTime<=0)return;
    if(!Alive(p)){p->RuneKickRecoilTime=0;return;}
    float dt=std::min(elapsed,p->RuneKickRecoilTime);auto delta=p->RuneKickRecoil*(dt/p->RuneKickRecoilTime);
    p->RuneKickRecoil-=delta;p->RuneKickRecoilTime-=dt;
    p->TryMove(delta); // Swept, horizontal, and no forced airborne state.
}
}
