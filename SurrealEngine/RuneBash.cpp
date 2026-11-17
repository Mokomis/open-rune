#include "RuneBattleLaws.h"
#include "Precomp.h"
#include "RuneSpider.h"
#include "RunePhysics.h"
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
    auto* target=RuneBattleLaws::Enabled(RuneBattleLaws::Bash,Enabled.load()) && Shield(attacker)?RuneKick::Target(attacker):nullptr;
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
    if(!RuneBattleLaws::Enabled(RuneBattleLaws::Bash,Enabled.load()))return;
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
    if(!blocked)PresentImpact(target,stagger);
    if(!blocked)CallEvent(target,"JointDamaged",{ExpressionValue::IntValue(2),ExpressionValue::ObjectValue(attacker),ExpressionValue::VectorValue(target->Location()),ExpressionValue::VectorValue(vec3(0)),ExpressionValue::NameValue("blunt"),ExpressionValue::IntValue(0)});
    if(Alive(target)) {
        vec3 direction=target->Location()-attacker->Location();direction.z=0;
        float weight=std::clamp(100.0f/std::max(1.0f,target->Mass()),.25f,1.25f);
        // Reuse the swept horizontal recoil path. No airborne launch or teleport.
        target->RuneKickRecoil=(length(direction)>.01f?normalize(direction):vec3(1,0,0))*((blocked||resistant?6.0f:20.0f)*weight);
        target->RuneKickRecoilTime=.16f;
    }
    if(target->Health()<before){auto d=target->Location()-attacker->Location();d.z=0;RunePhysics::Impact(attacker,target,(length(d)>.01f?normalize(d):vec3(1,0,0))*240.f+vec3(0,0,85),blocked);}
    auto* soundShield=blocked?Shield(target):Shield(attacker);
    BashSound(attacker,soundShield,blocked);
    RuneFeedback::Impact(attacker,target,blocked,true);
    attacker->RuneBashHits++;if(blocked)attacker->RuneBashBlocks++;
    LogMessage("RUNE_BASH_HIT attacker="+attacker->Class->Name.ToString()+" target="+target->Class->Name.ToString()+" blocked="+std::to_string(blocked)+" stagger="+std::to_string(stagger)+" resistant="+std::to_string(resistant)+" health_before="+std::to_string(before)+" health_after="+std::to_string(target->Health()));
}
bool DamageReaction(UPawn* target,UPawn* instigator,int healthLost) {
    // Original damage can mark a grounded pawn Falling before the next landing
    // tick. Recognize that contact without turning a genuinely airborne hit
    // into a frozen standing animation.
    bool grounded=target && target->Physics()==PHYS_Walking;
    if(target && target->Physics()==PHYS_Falling && std::abs(target->Velocity().z)<160.f) {
        vec3 hit,normal;auto at=target->Location();
        grounded=target->Trace(hit,normal,at-vec3(0,0,target->CollisionHeight()+6.f),at,false,vec3(target->CollisionRadius()*.5f,target->CollisionRadius()*.5f,0)) && normal.z>.7f;
    }
    // Committed health loss only: shield contacts, zero-damage hits and lethal
    // hits never start a stagger. Share bash's immunity to prevent stun chains.
    if(!engine || !engine->LaunchInfo.IsRune() || RunePhysics::RequestedMode.load()==0 ||
       !Alive(target) || !Alive(instigator) || target==instigator || target->NetworkReplica ||
       healthLost<18 || target->RuneBashImmunity>0 || target->RuneBashPhase ||
       !grounded || (!CombatState(target) && target->GetStateName()!="Pain") ||
       RunePhysics::Controls(target) || RunePhysics::Carrying(target) || RuneCorpse::Busy(target) ||
       RuneBloodlust::Busy(target) || target->RuneKickActive ||
       (target->HasProperty("bIsBoss") && target->GetBool("bIsBoss")) || target->Mass()>200 ||
       !Sequence(target,Stagger))return false;
    target->RuneBashImmunity=2.f;
    target->Dual.guard=false;
    // Keep the original directional pain clip when the damage script chose one.
    auto animation=target->AnimSequence();
    if(animation.ToString().find("pain")==std::string::npos || !Sequence(target,animation))animation=Stagger;
    Commit(target,3,animation);
    // Damage's Pain state can leave NextState unset. The new short stagger
    // must always return to live locomotion rather than strand a pawn in None.
    target->NextState()=target->IsA("RunePlayer")?"PlayerWalking":"Fighting";
    PresentImpact(target,true);
    LogMessage("RUNE_DAMAGE_STAGGER actor="+target->Name.ToString()+" damage="+std::to_string(healthLost));
    return true;
}
void PresentImpact(UPawn* target,bool staggered) {
    if(!Alive(target) || !engine || !engine->LevelInfo)return;
    target->RuneBashImpactAt=engine->LevelInfo->TimeSeconds();
    target->RuneBashImpactStrength=staggered?1.f:.45f;
}
void ApplyPose(const USkelModel& model,UActor* actor,Array<RuneJointPose>& pose) {
    auto* p=UObject::TryCast<UPawn>(actor);
    // Preview/model probes can query joints before a level exists.
    if(!Alive(p) || !engine || !engine->LevelInfo || p->RuneBashImpactStrength<=0 || RunePhysics::Controls(p) || pose.size()!=model.Joints.size())return;
    float age=engine->LevelInfo->TimeSeconds()-p->RuneBashImpactAt;
    if(age<0 || age>=.34f)return;
    // Fast recoil, smooth return. Only rotations change: bone lengths/scales remain intact.
    float t=age<.045f?age/.045f:1-(age-.045f)/.295f;
    t=std::clamp(t,0.f,1.f);t=t*t*(3-2*t)*p->RuneBashImpactStrength;
    auto basis=Coords::Rotation(model.RotationOffset).ToMatrix();
    auto inv=mat3::inverse(mat3(basis));auto inverse=mat4::identity();
    for(int c=0;c<3;++c)for(int r=0;r<3;++r)inverse[c*4+r]=inv[c*3+r];
    for(size_t j=1;j<model.Joints.size();++j) {
        float angle=model.Joints[j].Name=="head"?-.52f:0.f;
        if(!angle)continue;
        auto bones=model.JointMatrices(pose);
        auto parentInverse=mat3::inverse(mat3(bones[model.Joints[j].Parent]));
        auto turn=mat3(inverse*mat4::rotate(angle*t,0,1,0)*basis);
        auto local=parentInverse*turn*mat3(bones[model.Joints[j].Parent])*mat3(RunePoseRotation(pose[j].Rotation));
        auto rotation=mat4::identity();
        for(int c=0;c<3;++c){vec3 axis(local[c*3],local[c*3+1],local[c*3+2]);
            float n=length(axis);if(n>.0001f)axis/=n;
            for(int r=0;r<3;++r)rotation[c*4+r]=axis[r];}
        pose[j].Rotation=RunePoseEuler(rotation);
    }
}
bool Compatible(UPawn* p) {
    if(RuneSpider::Busy(p))return false;
    if(!RuneBattleLaws::Enabled(RuneBattleLaws::Bash,Enabled.load()))return false;
    return engine->LaunchInfo.IsRune() && Alive(p) && (p->IsA("RunePlayer") || p->IsA("ScriptPawn")) && Sequence(p,Strike);
}
bool Start(UPawn* p) {
    if(RuneCorpse::Busy(p))return false;
    if(RuneBloodlust::Busy(p) || !RuneBattleLaws::Enabled(RuneBattleLaws::Bash,Enabled.load()) || !Compatible(p) || !Shield(p) || p->RuneBashPhase || p->RuneKickActive || p->RuneBashCooldown>0 || p->Physics()!=PHYS_Walking || !CombatState(p))return false;
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
    p->RuneBashTapTime=RuneBattleLaws::Enabled(RuneBattleLaws::Bash,Enabled.load()) && !twice?now:-1;
    if(twice)Start(p);
}
void Tick(UPawn* p,float elapsed) {
    if(!engine->LaunchInfo.IsRune())return;
    bool pending=p->RuneBashPending;
    p->RuneBashCooldown=std::max(0.0f,p->RuneBashCooldown-elapsed);
    p->RuneBashImmunity=std::max(0.0f,p->RuneBashImmunity-elapsed);
    if(RuneSpider::Busy(p))return;
    // A subsequent full knockdown supersedes the cosmetic/short stagger. Its
    // physics recovery must keep ownership of the actor state.
    if(p->RuneBashPhase==3 && RunePhysics::Controls(p))p->RuneBashPhase=0;
    if(p->RuneBashPhase) {
        p->RuneBashAge+=elapsed;
        if(p->RuneBashPhase==3 && Alive(p) && p->RuneBashAge>=.4f &&
           (p->GetStateName()=="Uninterrupted" || p->GetStateName().IsNone() || p->GetStateName()=="Pain")) {
            p->GotoState(p->IsA("RunePlayer")?"PlayerWalking":"Fighting","Begin");
            if(auto* proxy=p->GetUObject("AnimProxy"))proxy->GotoState("Idle","Begin");
        }
        if(!Alive(p) || p->GetStateName()!="Uninterrupted") {
            if(p->RuneBashPhase==2)LogMessage("RUNE_BASH_STUN_END actor="+p->Class->Name.ToString()+" seconds="+std::to_string(p->RuneBashAge)+" state="+p->GetStateName().ToString());
            p->RuneBashPhase=0;p->RuneBashPending=false;
        }
    }
    if(!RuneBattleLaws::Enabled(RuneBattleLaws::Bash,Enabled.load())) { p->RuneBashPending=false;p->RuneBashTapTime=-1; }
#ifdef OPENRUNE_ENABLE_NETWORK
    if(pending && !p->RuneBashPending && engine->NetworkCombatAuthority)
        engine->NetworkCombatAuthority->cancelCombat(p,openrune::net::CombatMove::Bash);
#endif
    if(!RuneBattleLaws::Enabled(RuneBattleLaws::Bash,Enabled.load()))return;
    if(!Alive(p) || UObject::TryCast<UPlayerPawn>(p))return;
    if(RuneTactics::SuppressRandomSpecial(p))return;
    p->RuneBashThink-=elapsed;
    if(p->RuneBashThink<=0) { p->RuneBashThink=.45f;if(p->GetStateName()=="Fighting" && p->RuneBashCooldown<=0 && Shield(p) && RuneKick::Target(p) && FRand()<.2f)Start(p); }
}
bool Threat(UObject* observer) {
    auto* p=UObject::TryCast<UPawn>(observer);auto* enemy=p?p->Enemy():nullptr;
    return engine->LaunchInfo.IsRune() && RuneBattleLaws::Enabled(RuneBattleLaws::Bash,Enabled.load()) && Alive(p) && Alive(enemy) && enemy->RuneBashPhase==1 && enemy->RuneBashPending && RuneKick::Target(enemy)==p;
}
bool PrepareAnimation(UActor* actor,const NameString& sequence,float& rate,float& tween) {
    if(!engine->LaunchInfo.IsRune())return false;
    auto* p=Owner(actor);
    if(!p || !p->RuneBashPhase)return false;
    if(p->RuneBashPhase==3 ? sequence.ToString().find("pain")==std::string::npos : sequence!=(p->RuneBashPhase==1?Strike:Stagger))return false;
    auto* seq=Sequence(p,sequence);if(!seq)return false;
    // Original Uninterrupted state owns recovery; only these two moves are retimed.
    tween=p->RuneBashPhase==1?.22f:p->RuneBashPhase==3?.06f:.1f;
    rate=(seq->NumFrames-1)/(seq->Rate*(p->RuneBashPhase==1?.43f:p->RuneBashPhase==3?.32f:.25f));
    return true;
}
bool AnimationFrames(UActor* actor,float from,float to) {
    if(!engine->LaunchInfo.IsRune())return false;
    auto* p=Owner(actor);if(!p || !p->RuneBashPhase)return false;
    if(p->RuneBashPhase==3)return actor->AnimSequence().ToString().find("pain")!=std::string::npos;
    auto expected=p->RuneBashPhase==1?Strike:Stagger;
    if(actor->AnimSequence()!=expected)return false;
    // Authored shield reaches furthest forward at frame 1 of 8, after the wind-up.
    if(actor==p && p->RuneBashPhase==1 && p->RuneBashPending && from<.125f && to>=.125f)Impact(p);
    return true; // These moves must not activate original weapon swipe notifies.
}
}
