#include "Precomp.h"
#include "RuneTactics.h"
#include "RuneDualWield.h"
#include "RuneTacticsPolicy.h"
#include "RuneKick.h"
#include "RuneBash.h"
#include "Engine.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Inventory/UWeapon.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UGameInfo.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "VM/ScriptCall.h"
#include "VM/Frame.h"
#include "Utils/Random.h"
#include <cmath>

namespace RuneTactics {
namespace {
bool Alive(UPawn* p) { return p && !p->bDeleteMe() && !p->bHidden() && !p->bStatic() && p->Health()>0; }
vec3 Forward(UPawn* p) { float yaw=p->Rotation().YawRadians();return {std::cos(yaw),std::sin(yaw),0}; }
bool Armed(UPawn* p) { return p->Weapon() && !p->Weapon()->bDeleteMe() && p->Weapon()->Owner()==p; }
bool HasShield(UPawn* p) {
    if(RuneDualWield::Equipped(p))return false;
    auto* s=UObject::TryCast<UActor>(p->GetUObject("Shield"));
    return s && !s->bDeleteMe() && s->Owner()==p && s->GetInt("Health")>0;
}
bool Swinging(UPawn* p) {
    return p->GetBool("bSwingingHigh") || p->GetBool("bSwingingLow") ||
        p->RuneKickPending || p->RuneBashPending || RuneBloodlust::Busy(p);
}
const MeshAnimSeq* Sequence(UPawn* p,const NameString& name) {
    auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(p));
    if(model)for(const auto& seq:model->Sequences)
        if(seq.Info.Name==name && seq.Info.NumFrames>1 && seq.Info.Rate>0)return &seq.Info;
    return nullptr;
}
NameString AttackSequence(UPawn* p) {
    if(p->IsA("Viking"))return Armed(p)?p->Weapon()->GetName("A_AttackStandA"):NameString("None");
    if(p->IsA("Dwarf"))return Armed(p)?NameString("attackA"):NameString("None");
    return Armed(p)?"attackb":"swipe";
}
void Shield(UPawn* p,bool raised) {
    CallEvent(p,"ActivateShield",{ExpressionValue::BoolValue(raised)});
    RuneDualWield::Guard(p,raised);
    p->Tactics.blocking=raised && (HasShield(p) || RuneDualWield::Equipped(p));
}
void Stop(UPawn* p) { p->Acceleration()=vec3(0);p->Velocity().x=0;p->Velocity().y=0;p->Tactics.moveTime=0; }
void ColdWeapon(UPawn* p) {
    CallEvent(p,"WeaponDeactivate");
    if(Armed(p))CallEvent(p->Weapon(),"DisableSwipeTrail");
    p->SetBool("bSwingingHigh",false);p->SetBool("bSwingingLow",false);
}
void Idle(UPawn* p) { CallEvent(p,"PlayWaiting",{ExpressionValue::FloatValue(.15f)}); }
void Release(UPawn* p,bool restart) {
    auto& s=p->Tactics;
    if(!s.active)return;
    s.active=false;
    p->Acceleration()=vec3(0);s.moveTime=0;
    // A kick, fall or pain state can revoke control. Preserve its externally
    // applied momentum; stopping our old walking intent must not cancel it.
    if(p->Physics()==PHYS_Walking && p->GetStateName()=="Fighting") {
        p->Velocity().x=0;p->Velocity().y=0;
    }
    if(s.blocking)Shield(p,false);
    if(s.attacking)ColdWeapon(p);
    s.attacking=false;s.special=false;s.observed=nullptr;
    s.visibleFor=s.threatFor=s.blockingFor=s.counterWindow=0;s.sawThreat=s.sawBlock=false;
    // Never overwrite a pain/death/cinematic state entered by the original scripts.
    if(restart && p->GetStateName()=="Fighting")p->GotoState("Fighting","Begin");
}
bool Walk(UPawn* p,const vec3& direction,Choice choice,float stepLength=48.0f) {
    if(length(direction)<.01f)return false;
    auto unit=normalize(direction);
    // Validate the entire short step and its landing support. The normal walking
    // solver still sweeps every physical movement; no position warps or cliff jumps.
    auto end=p->Location()+unit*stepLength;
    auto zone=p->Level()->GetLocZone(end,{}).Zone;
    if(!zone || zone->bPainZone() || zone->bWaterZone())return false;
    vec3 hit,normal;
    vec3 extent(p->CollisionRadius(),p->CollisionRadius(),p->CollisionHeight()*.8f);
    if(p->Trace(hit,normal,end,p->Location(),true,extent))return false;
    if(!p->Trace(hit,normal,end-vec3(0,0,p->CollisionHeight()+32),end,false,
                 vec3(p->CollisionRadius()*.6f,p->CollisionRadius()*.6f,0)) || normal.z<.7f)return false;
    p->DesiredSpeed()=std::min(.45f,stepLength/(std::max(p->GroundSpeed(),1.0f)*.12f));
    p->Acceleration()=unit*p->AccelRate();
    if(p->Tactics.move!=choice || p->Tactics.moveTime<=0)
        CallEvent(p,choice==Choice::Retreat?"PlayBackup":choice==Choice::Flank?
            (p->Tactics.side>0?"PlayStrafeRight":"PlayStrafeLeft"):"PlayMoving",
            {ExpressionValue::FloatValue(.15f)});
    p->Tactics.move=choice;p->Tactics.moveTime=.12f;p->Tactics.moves++;
    return true;
}
void Attack(UPawn* p) {
    auto name=AttackSequence(p);
    std::vector<NameString> variants{name};
    if(p->IsA("Viking") && Armed(p)) {
        auto alternate=p->Weapon()->GetName("A_AttackStandB");
        if(alternate!=name && Sequence(p,alternate))variants.push_back(alternate);
    } else if(p->IsA("Dwarf")) {
        for(const char* alternate:{"attackB","attackC"})if(Sequence(p,alternate))variants.push_back(alternate);
    }
    int variant=int(FRand()*variants.size())%int(variants.size());
    if(variants.size()>1 && variant==p->Tactics.lastAttackVariant)variant=(variant+1)%int(variants.size());
    p->Tactics.lastAttackVariant=variant;name=variants[variant];
    auto* seq=Sequence(p,name);if(!seq)return;
    auto& s=p->Tactics;
    Stop(p);Shield(p,false);
    p->DesiredRotation()=p->Rotation();
    RuneDualWield::BeginSwing(p);
    s.attacking=true;s.actionAge=0;
    s.actionLimit=.15f+(seq->NumFrames-1)/seq->Rate+.15f;
    p->SetBool("bSwingingHigh",true);
    if(p->IsA("Viking")) {
        CallEvent(p,"WeaponActivate");
        CallEvent(p->Weapon(),"EnableSwipeTrail");
    }
    // Existing authored notifies own collision/damage, at the original rate.
    p->PlayAnim(name,1,.15f);s.attacks++;
}
}

bool Eligible(UPawn* p) {
#ifdef OPENRUNE_ENABLE_NETWORK
    if(engine && engine->NetworkClient)return false;
#endif
    return engine && engine->LaunchInfo.IsRune() && Alive(p) && !p->bIsPlayer() &&
        !UObject::TryCast<UPlayerPawn>(p) && !p->NetworkReplica && p->Role()==ROLE_Authority && p->IsA("ScriptPawn") &&
        (p->IsA("Viking") || p->IsA("Goblin") || p->IsA("Dwarf")) &&
        !p->GetBool("bIsBoss") && p->Physics()==PHYS_Walking &&
        (!p->StateFrame || p->StateFrame->GetLatentState()!=LatentRunState::Stop);
}
bool Controls(UObject* obj) {
    auto* p=UObject::TryCast<UPawn>(obj);
    return p && ((RuneCorpse::Busy(p) && !p->IsA("RunePlayer")) || (p->Tactics.active && Eligible(p) && p->GetStateName()=="Fighting"));
}
bool SuppressRandomSpecial(UPawn* p) {
    if(!Eligible(p) || p->GetStateName()!="Fighting")return false;
    return p->Tactics.active || (Enabled.load() && Sequence(p,AttackSequence(p)));
}
bool MayCommit(UPawn* p) {
    if(!Enabled.load() || !Eligible(p) || !Alive(p->Enemy()))return true;
    const auto temperament=ForDifficulty(engine->GameInfo?engine->GameInfo->GetByte("Difficulty"):1);
    int attackers=0,checked=0;
    for(auto* ally=p->Level()->PawnList();ally && checked++<64;ally=ally->nextPawn()) {
        if(ally==p || ally==p->Enemy() || !Alive(ally) || ally->Enemy()!=p->Enemy() || length(ally->Location()-p->Location())>400)continue;
        if(ally->Tactics.attacking || ally->RuneKickPending || ally->RuneKickActive || ally->RuneBashPending || ally->RuneBashPhase || RuneBloodlust::Busy(ally))++attackers;
    }
    return attackers<temperament.attackers;
}
void Tick(UPawn* p,float elapsed) {
    if(!engine->LaunchInfo.IsRune() || RuneBloodlust::Busy(p))return;
    if(!p->IsA("RunePlayer") && RuneCorpse::Busy(p)){p->Acceleration()=vec3(0);p->Velocity().x=p->Velocity().y=0;return;}
    auto& s=p->Tactics;
    const float dt=std::clamp(elapsed,0.0f,.1f);
    auto state=p->GetStateName();
    // Our kick/bash returns through the original Uninterrupted state. Do not
    // interrupt it, nor change any scripted Uninterrupted animation.
    if(s.active && s.special && state=="Uninterrupted" && Alive(p))return;
    if(!Eligible(p) || state!="Fighting") { Release(p,false);return; }
    if(!Enabled.load() && !s.attacking) { Release(p,true);return; }
    auto* enemy=p->Enemy();
    if(!Alive(enemy)) { Release(p,true);return; }
    if(!s.active) {
        if(!Enabled.load() || !Sequence(p,AttackSequence(p)))return;
        // Let an in-progress original attack, equipment draw, or restored save
        // animation finish. Taking over must never truncate its hit window.
        if(Swinging(p) && p->bAnimFinished() && !p->RuneKickPending && !p->RuneBashPending)ColdWeapon(p);
        if(Swinging(p) || (!p->bAnimLoop() && !p->bAnimFinished() && p->AnimRate()>0))return;
        s.active=true;s.attacking=false;s.blocking=false;s.special=false;
        s.think=FRand()*.1f;s.visibleFor=0;s.recovery=.25f;
        s.side=FRand()<.5f?-1:1;s.observed=enemy;
        Stop(p);Shield(p,false);Idle(p);
    }
    const auto temperament=ForDifficulty(engine->GameInfo?engine->GameInfo->GetByte("Difficulty"):1);
    if(s.observed!=enemy) {
        s.observed=enemy;s.visibleFor=s.threatFor=s.blockingFor=s.counterWindow=0;s.sawThreat=s.sawBlock=false;
    }
    if(s.special) { s.special=false;s.recovery=temperament.recovery+FRand()*.16f;Idle(p); }
    if(s.attacking) {
        s.actionAge+=dt;Stop(p);
        // Facing stays committed. Damage/pain/death can still interrupt naturally.
        if(p->bAnimFinished() || s.actionAge>s.actionLimit) {
            ColdWeapon(p);s.attacking=false;s.recovery=temperament.recovery+FRand()*.16f;Idle(p);
            // Do not occupy the same attack lane indefinitely. A short, swept
            // recovery step makes room for another fighter without canceling
            // the swing or shortening its recovery.
            if(s.attacks%2==0 && p->CanSee(enemy)) {
                auto delta=enemy->Location()-p->Location();delta.z=0;
                if(length(delta)>.01f) {
                    auto direction=normalize(delta);auto tangent=vec3(-direction.y,direction.x,0)*float(s.side);
                    if(!Walk(p,tangent,Choice::Flank)) { s.side=-s.side;Walk(p,-tangent,Choice::Flank); }
                }
            }
        }
        return;
    }
    s.recovery=std::max(0.0f,s.recovery-dt);
    s.repositionCooldown=std::max(0.f,s.repositionCooldown-dt);
    s.counterWindow=std::max(0.f,s.counterWindow-dt);
    if(s.moveTime>0) { s.moveTime-=dt;if(s.moveTime<=0) { Stop(p);Idle(p); } }
    s.think-=dt;if(s.think>0)return;
    s.think=.1f;s.decisions++;
    if(!Sequence(p,AttackSequence(p))) { Release(p,true);return; }
    if(!CallEvent(p,"ValidEnemy").ToBool()) { Release(p,true);return; }
    if(auto* player=UObject::TryCast<UPlayerPawn>(enemy);player && player->ViewTarget()) { Release(p,true);return; }
    auto delta=enemy->Location()-p->Location();float z=std::abs(delta.z);delta.z=0;
    float distance=length(delta);
    bool visible=z<=p->CollisionHeight()+enemy->CollisionHeight() && p->CanSee(enemy);
    if(!visible) {
        s.visibleFor=s.threatFor=s.blockingFor=0;s.sawThreat=s.sawBlock=false;
        Stop(p);if(s.blocking)Shield(p,false);s.lostSight+=.1f;
        // Original hunting/pathfinding searches the last seen location.
        if(s.lostSight>=.5f) { Release(p,false);p->GotoState("Hunting","Begin"); }
        return;
    }
    s.lostSight=0;s.visibleFor+=.1f;
    if(distance<.01f)return;
    vec3 direction=delta/distance;
    p->DesiredRotation().Yaw=Rotator::FromVector(direction).Yaw;
    p->bRotateToDesired()=true;
    bool threat=Swinging(enemy) && dot(Forward(enemy),-direction)>.4f &&
        distance<=p->CollisionRadius()+enemy->CollisionRadius()+(RuneBloodlust::Busy(enemy)?450.f:std::max(enemy->MeleeRange(),48.0f));
    if(threat && !s.sawThreat)s.defenseRoll=FRand();
    if(!threat && s.sawThreat)s.counterWindow=.65f;
    s.threatFor=threat?(s.sawThreat?s.threatFor+.1f:0):0;s.sawThreat=threat;
    bool blocking=RuneKick::ShieldBlocks(enemy,p) || RuneDualWield::Blocks(enemy,p);
    s.blockingFor=blocking?(s.sawBlock?s.blockingFor+.1f:0):0;s.sawBlock=blocking;
    if(s.blocking) {
        s.actionAge+=.1f;
        if(s.actionAge<.55f && (HasShield(p) || RuneDualWield::Equipped(p)))return;
        Shield(p,false);s.recovery=.22f;Idle(p);
    }
    if(s.recovery>0)return;
    Observation observation;
    observation.visible=true;observation.visibleFor=s.visibleFor;
    observation.facing=dot(Forward(p),direction)>.85f;
    observation.threat=threat;observation.threatFor=s.threatFor;
    observation.shield=HasShield(p) || RuneDualWield::Equipped(p);observation.targetBlocking=blocking;observation.blockingFor=s.blockingFor;
    observation.distance=distance;
    observation.wounded=p->Health()<p->Class->GetDefaultObject<UPawn>()->Health()*.3f;
    observation.repositionReady=s.repositionCooldown<=0;
    observation.counterOpening=s.counterWindow>0;
    observation.reach=p->CollisionRadius()+enemy->CollisionRadius()+std::clamp(p->MeleeRange(),20.0f,120.0f);
    if(RuneDualWield::Equipped(p))observation.reach=p->CollisionRadius()+enemy->CollisionRadius()+12;
    observation.contact=p->CollisionRadius()+enemy->CollisionRadius()+48;
    observation.kick=RuneKick::Enabled.load() && p->RuneKickCooldown<=0 && RuneKick::Compatible(p);
    observation.bash=RuneBash::Enabled.load() && HasShield(p) && p->RuneBashCooldown<=0 && RuneBash::Compatible(p);
    // Bounded local coordination: other species can still use their own AI.
    int checked=0;
    for(auto* ally=p->Level()->PawnList();ally && checked++<64;ally=ally->nextPawn()) {
        if(ally==p || ally==enemy || !Alive(ally) || ally->Enemy()!=enemy)continue;
        float nearValue=length(ally->Location()-p->Location());
        if(nearValue>400)continue;
        if(ally->Tactics.attacking || ally->RuneKickPending || ally->RuneKickActive ||
           ally->RuneBashPending || ally->RuneBashPhase || RuneBloodlust::Busy(ally))observation.otherAttackers++;
        auto offset=ally->Location()-p->Location();offset.z=0;
        if(nearValue<p->CollisionRadius()+ally->CollisionRadius()+12 && length(offset)>.01f &&
           dot(normalize(offset),direction)>.4f)observation.crowded=true;
        const float along=dot(offset,direction);
        if(along>0 && along<distance && length(offset-direction*along)<ally->CollisionRadius()+p->CollisionRadius()*.7f)
            observation.crowded=true;
    }
    auto choice=Decide(observation,temperament,s.defenseRoll);
    switch(choice) {
    case Choice::Attack: Attack(p);break;
    case Choice::Kick:
    case Choice::Bash:
        Stop(p);Shield(p,false);
        if(choice==Choice::Kick?RuneKick::Start(p):RuneBash::Start(p)) {
            s.special=true;if(choice==Choice::Kick)s.kicks++;else s.bashes++;
        } else s.recovery=.2f;
        break;
    case Choice::Block:
        Stop(p);Shield(p,true);CallEvent(p,"PlayBlockLow",{ExpressionValue::FloatValue(.15f)});
        s.actionAge=0;s.blocks++;break;
    case Choice::Approach:
    case Choice::Retreat:
    case Choice::Flank: {
        vec3 step=choice==Choice::Approach?direction:choice==Choice::Retreat?-direction:vec3(-direction.y,direction.x,0)*float(s.side);
        float stepLength=choice==Choice::Approach?std::clamp(distance-observation.reach*.82f,8.0f,48.0f):48.0f;
        if(choice==Choice::Flank && observation.wounded)s.repositionCooldown=1.8f+FRand()*.8f;
        bool moved=Walk(p,step,choice,stepLength);
        if(!moved && choice!=Choice::Retreat) {
            auto tangent=vec3(-direction.y,direction.x,0)*float(s.side);
            if(choice==Choice::Approach)moved=Walk(p,tangent,Choice::Flank);
            if(!moved) { s.side=-s.side;moved=Walk(p,-tangent,Choice::Flank); }
        }
        if(!moved) {
            Stop(p);s.side=-s.side;s.recovery=.15f;Idle(p);
            // Pathfinding, rather than repeated wall pushing, handles blocked approaches.
            if(choice==Choice::Approach) { Release(p,false);p->GotoState("Charging","Begin"); }
        }
        break;
    }
    default: Stop(p);break;
    }
}
}
