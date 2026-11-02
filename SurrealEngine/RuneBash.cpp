#include "Precomp.h"
#include "RuneBash.h"
#include "RuneDualWield.h"
#include "RuneFeedback.h"
#include "RuneKick.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Package/Package.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "VM/ScriptCall.h"
#include "Utils/Random.h"
#include "Utils/Logger.h"
#ifdef OPENRUNE_ENABLE_NETWORK
#include "RuneNetwork.h"
#endif
#include <algorithm>

namespace RuneBash {
namespace {
constexpr const char* Strike = "H3_DefendAttack";
constexpr const char* Stagger = "S1_painFront";
bool Alive(UPawn* p) { return p && !p->bDeleteMe() && !p->bHidden() && !p->bStatic() && p->Health()>0; }
UActor* Shield(UPawn* p) {
    if(RuneDualWield::Equipped(p))return nullptr;
    auto* shield=UObject::TryCast<UActor>(p->GetUObject("Shield"));
    return shield && !shield->bDeleteMe() && shield->GetInt("Health")>0 && shield->Owner()==p ? shield : nullptr;
}
MeshAnimSeq* Sequence(UPawn* p,const NameString& name) {
    auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(p));
    if(model)for(auto& seq:model->Sequences)if(seq.Info.Name==name && seq.Info.NumFrames>1 && seq.Info.Rate>0)return &seq.Info;
    return nullptr;
}
UPawn* Owner(UActor* actor) {
    auto* p=UObject::TryCast<UPawn>(actor);
    if(!p && actor->IsA("AnimationProxy"))p=UObject::TryCast<UPawn>(actor->Owner());
    return p;
}
bool CombatState(UPawn* p) {
    if(auto* player=UObject::TryCast<UPlayerPawn>(p))return !player->ViewTarget() && p->GetStateName()=="PlayerWalking";
    auto state=p->GetStateName();
    return state=="Fighting" || state=="Charging" || state=="TacticalMove" || state=="Retreating";
}
void StopWeapon(UPawn* p) {
    if(auto* weapon=p->GetUObject("Weapon")) { CallEvent(weapon,"FinishAttack");CallEvent(weapon,"DisableSwipeTrail"); }
    p->SetBool("bSwingingHigh",false);p->SetBool("bSwingingLow",false);
    p->SetObject("UseActor",nullptr);
}
void BashSound(UPawn* p,UActor* source,bool blocked,bool swing=false) {
    // Original VikingShield.PlayHitSound is empty. Use the authored blunt
    // weapon cues so a bash has an impact even with that shield or no weapon.
    auto* defaults=engine->packages->GetPackage("RuneI")->GetClass("RustyMace")->GetDefaultObject<UObject>();
    UObject* sound=swing?static_cast<UObject**>(defaults->GetProperty("ThroughAir"))[0]:
        blocked?defaults->GetUObject("HitShield"):static_cast<UObject**>(defaults->GetProperty("HitFlesh"))[0];
    if(sound)CallEvent(source?source:p,"PlaySound",{ExpressionValue::ObjectValue(sound)});
}
void Commit(UPawn* p,int phase,const NameString& animation) {
    StopWeapon(p);
    CallEvent(p,"ActivateShield",{ExpressionValue::BoolValue(false)});
    p->RuneBashPhase=phase;p->RuneBashAge=0;
    CallEvent(p,"PlayUninterruptedAnim",{ExpressionValue::NameValue(animation)});
}
void Impact(UPawn* attacker) {
    attacker->RuneBashPending=false;
    auto* target=Enabled.load() && Shield(attacker)?RuneKick::Target(attacker):nullptr;
    if(!target || !CombatState(target)) {
        LogMessage("RUNE_BASH_MISS");
#ifdef OPENRUNE_ENABLE_NETWORK
        if(engine->NetworkCombatAuthority)engine->NetworkCombatAuthority->recordCombat(attacker,nullptr,true,false,false);
#endif
        return;
    }
    bool blocked=RuneKick::ShieldBlocks(target,attacker);
#ifdef OPENRUNE_ENABLE_NETWORK
    if(engine->NetworkCombatAuthority && engine->NetworkCombatAuthority->deferContact(attacker,target,blocked,nullptr,
        [attacker,target](bool defended){CommitImpact(attacker,target,defended);},true))return;
#endif
    CommitImpact(attacker,target,blocked);
}
}
void CommitImpact(UPawn* attacker,UPawn* target,bool blocked)
{
#ifdef OPENRUNE_ENABLE_NETWORK
    if(engine->NetworkCombatAuthority)engine->NetworkCombatAuthority->prepareCombat(attacker,target);
#endif
    bool resistant=(target->HasProperty("bIsBoss") && target->GetBool("bIsBoss")) || target->Mass()>200;
    bool stagger=!blocked && !resistant && target->RuneBashImmunity<=0 && Sequence(target,Stagger) && target->Physics()==PHYS_Walking;
    int before=target->Health();
    if(stagger) {
        target->RuneBashImmunity=2.0f;
        Commit(target,2,Stagger);
        target->RuneBashStuns++;
        LogMessage("RUNE_BASH_STUN actor="+target->Class->Name.ToString());
    }
    if(!blocked)CallEvent(target,"JointDamaged",{ExpressionValue::IntValue(2),ExpressionValue::ObjectValue(attacker),ExpressionValue::VectorValue(target->Location()),ExpressionValue::VectorValue(vec3(0)),ExpressionValue::NameValue("blunt"),ExpressionValue::IntValue(0)});
    if(Alive(target)) {
        vec3 direction=target->Location()-attacker->Location();direction.z=0;
        float weight=std::clamp(100.0f/std::max(1.0f,target->Mass()),.25f,1.25f);
        // Reuse the swept horizontal recoil path. No airborne launch or teleport.
        target->RuneKickRecoil=(length(direction)>.01f?normalize(direction):vec3(1,0,0))*((blocked||resistant?6.0f:20.0f)*weight);
        target->RuneKickRecoilTime=.16f;
    }
    auto* soundShield=blocked?Shield(target):Shield(attacker);
    BashSound(attacker,soundShield,blocked);
    RuneFeedback::Impact(attacker,target,blocked,true);
    attacker->RuneBashHits++;if(blocked)attacker->RuneBashBlocks++;
    LogMessage("RUNE_BASH_HIT attacker="+attacker->Class->Name.ToString()+" target="+target->Class->Name.ToString()+" blocked="+std::to_string(blocked)+" stagger="+std::to_string(stagger)+" resistant="+std::to_string(resistant)+" health_before="+std::to_string(before)+" health_after="+std::to_string(target->Health()));
}
bool Compatible(UPawn* p) {
    return engine->LaunchInfo.IsRune() && Alive(p) && (p->IsA("RunePlayer") || p->IsA("ScriptPawn")) && Sequence(p,Strike);
}
bool Start(UPawn* p) {
    if(RuneBloodlust::Busy(p) || !Enabled.load() || !Compatible(p) || !Shield(p) || p->RuneBashPhase || p->RuneKickActive || p->RuneBashCooldown>0 || p->Physics()!=PHYS_Walking || !CombatState(p))return false;
    if(p->GetBool("bSwingingHigh") || p->GetBool("bSwingingLow"))return false;
    auto* proxy=UObject::TryCast<UActor>(p->GetUObject("AnimProxy"));
    if(proxy && proxy->GetStateName()!="Idle" && proxy->GetStateName()!="Defending")return false;
    if(!UObject::TryCast<UPlayerPawn>(p) && (p->GetStateName()!="Fighting" || !RuneKick::Target(p)))return false;
    BashSound(p,Shield(p),false,true);
    p->RuneBashCooldown=4.0f;p->RuneBashPending=true;p->RuneBashTapTime=-1;
    Commit(p,1,Strike);
    LogMessage("RUNE_BASH_START actor="+p->Class->Name.ToString());
    return true;
}
void BlockPressed(UPawn* p) {
    if(!p || !engine->LaunchInfo.IsRune())return;
    float now=engine->LevelInfo->TimeSeconds();
    bool twice=p->RuneBashTapTime>=0 && now-p->RuneBashTapTime<=.3f;
    p->RuneBashTapTime=Enabled.load() && !twice?now:-1;
    if(twice)Start(p);
}
void Tick(UPawn* p,float elapsed) {
    if(!engine->LaunchInfo.IsRune())return;
    bool pending=p->RuneBashPending;
    p->RuneBashCooldown=std::max(0.0f,p->RuneBashCooldown-elapsed);
    p->RuneBashImmunity=std::max(0.0f,p->RuneBashImmunity-elapsed);
    if(p->RuneBashPhase) {
        p->RuneBashAge+=elapsed;
        if(!Alive(p) || p->GetStateName()!="Uninterrupted") {
            if(p->RuneBashPhase==2)LogMessage("RUNE_BASH_STUN_END actor="+p->Class->Name.ToString()+" seconds="+std::to_string(p->RuneBashAge)+" state="+p->GetStateName().ToString());
            p->RuneBashPhase=0;p->RuneBashPending=false;
        }
    }
    if(!Enabled.load()) { p->RuneBashPending=false;p->RuneBashTapTime=-1; }
#ifdef OPENRUNE_ENABLE_NETWORK
    if(pending && !p->RuneBashPending && engine->NetworkCombatAuthority)
        engine->NetworkCombatAuthority->cancelCombat(p,openrune::net::CombatMove::Bash);
#endif
    if(!Enabled.load())return;
    if(!Alive(p) || UObject::TryCast<UPlayerPawn>(p))return;
    if(RuneTactics::SuppressRandomSpecial(p))return;
    p->RuneBashThink-=elapsed;
    if(p->RuneBashThink<=0) { p->RuneBashThink=.45f;if(p->GetStateName()=="Fighting" && p->RuneBashCooldown<=0 && Shield(p) && RuneKick::Target(p) && FRand()<.2f)Start(p); }
}
bool Threat(UObject* observer) {
    auto* p=UObject::TryCast<UPawn>(observer);auto* enemy=p?p->Enemy():nullptr;
    return engine->LaunchInfo.IsRune() && Enabled.load() && Alive(p) && Alive(enemy) && enemy->RuneBashPhase==1 && enemy->RuneBashPending && RuneKick::Target(enemy)==p;
}
bool PrepareAnimation(UActor* actor,const NameString& sequence,float& rate,float& tween) {
    if(!engine->LaunchInfo.IsRune())return false;
    auto* p=Owner(actor);
    if(!p || !p->RuneBashPhase || sequence!=(p->RuneBashPhase==1?Strike:Stagger))return false;
    auto* seq=Sequence(p,sequence);if(!seq)return false;
    // Original Uninterrupted state owns recovery; only these two moves are retimed.
    tween=p->RuneBashPhase==1?.22f:.1f;
    rate=(seq->NumFrames-1)/(seq->Rate*(p->RuneBashPhase==1?.43f:.25f));
    return true;
}
bool AnimationFrames(UActor* actor,float from,float to) {
    if(!engine->LaunchInfo.IsRune())return false;
    auto* p=Owner(actor);if(!p || !p->RuneBashPhase)return false;
    auto expected=p->RuneBashPhase==1?Strike:Stagger;
    if(actor->AnimSequence()!=expected)return false;
    // Authored shield reaches furthest forward at frame 1 of 8, after the wind-up.
    if(actor==p && p->RuneBashPhase==1 && p->RuneBashPending && from<.125f && to>=.125f)Impact(p);
    return true; // These moves must not activate original weapon swipe notifies.
}
}
