#include "RuneBattleLaws.h"
#include "Precomp.h"
#include "RuneSpider.h"
#include "RunePhysics.h"
#include "RuneSlam.h"
#include "RuneBloodlust.h"
#include "RuneDualWield.h"
#include "RuneCorpse.h"
#include "RuneKick.h"
#include "RuneFeedback.h"
#include "Engine.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Inventory/UWeapon.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "Packages/Core/UFunction.h"
#include "VM/ScriptCall.h"
#include "Utils/Logger.h"
#ifdef OPENRUNE_ENABLE_NETWORK
#include "RuneNetwork.h"
#endif
namespace RuneSlam {
namespace {
bool Alive(UPawn* p){return p && !p->bDeleteMe() && !p->bHidden() && p->Health()>0;}
UPawn* Owner(UObject* o){if(auto* p=UObject::TryCast<UPawn>(o))return p;auto* a=UObject::TryCast<UActor>(o);return a?UObject::TryCast<UPawn>(a->Owner()):nullptr;}
int Joint(const USkelModel& m,const char* name){for(size_t i=0;i<m.Joints.size();++i)if(m.Joints[i].Name==name)return int(i);return -1;}
vec3 Forward(UPawn* p){float yaw=p->Rotation().YawRadians();return {std::cos(yaw),std::sin(yaw),0};}
float Smooth(float x){x=std::clamp(x,0.f,1.f);return x*x*(3-2*x);}
bool CombatState(UPawn* p){
    auto state=p->GetStateName();
    if(state=="Uninterrupted")return p->Bloodlust.phase==RuneBloodlust::Leaping;
    if(p->IsA("RunePlayer"))return state=="PlayerWalking";
    return state=="Fighting" || state=="Charging" || state=="TacticalMove" || state=="Retreating";
}
void Sound(UPawn* p,const char* field){
    auto* w=p->Slam.weapon;if(!w || w->bDeleteMe() || !w->HasProperty(field))return;
    auto* sound=static_cast<UObject**>(w->GetProperty(field))[0];if(sound)CallEvent(p,"PlaySound",{ExpressionValue::ObjectValue(sound)});
}
void Impact(UPawn* p,UPawn* contact=nullptr){
    UPawn* target=nullptr;float nearest=1e10f;
    for(auto* a:p->XLevel()->Actors)if(auto* other=UObject::TryCast<UPawn>(a);other && other!=p && Alive(other) && !other->bStatic()) {
        if(contact && other!=contact)continue;
        if(other->GetStateName()=="Scripting")continue;
        if(auto* player=UObject::TryCast<UPlayerPawn>(other);player && player->ViewTarget())continue;
        if(!p->IsA("RunePlayer") && other!=p->Enemy())continue;
        vec3 d=other->Location()-p->Location();float height=std::abs(d.z);d.z=0;float distance=length(d);
        if(contact) {
            // The physics sweep supplies the actual contacted pawn, including
            // a near-vertical head landing. Keep a small hull/rounding allowance.
            if(distance>p->CollisionRadius()+other->CollisionRadius()+8 || height>p->CollisionHeight()+other->CollisionHeight()+24)continue;
        } else if(distance<.01f || distance>p->CollisionRadius()+other->CollisionRadius()+100 || height>p->CollisionHeight()+other->CollisionHeight() || dot(Forward(p),d/distance)<.65f)continue;
        if(distance<nearest && p->FastTrace(other->Location(),p->Location())){nearest=distance;target=other;}
    }
    auto* weapon=p->Slam.weapon;
    if(!target || !weapon || weapon!=p->Weapon()){LogMessage("RUNE_SLAM_MISS");return;}
    bool shield=RuneKick::ShieldBlocks(target,p),cross=RuneDualWield::Blocks(target,p);
    auto commit=[p,target,weapon](bool defended) {
        if(!Alive(p) || !Alive(target) || weapon->bDeleteMe() || p->Weapon()!=weapon)return;
        if(target->GetStateName()=="Scripting")return;
        if(auto* player=UObject::TryCast<UPlayerPawn>(target);player && player->ViewTarget())return;
        bool cross=RuneDualWield::Blocks(target,p);
        int base=std::clamp(int(weapon->GetInt("Damage")),10,100);
        int damage=defended?(cross?base/3:0):int(std::lround(base*1.5f));
        int before=target->Health();
        if(damage>0)CallEvent(target,"JointDamaged",{ExpressionValue::IntValue(damage),ExpressionValue::ObjectValue(p),ExpressionValue::VectorValue(target->Location()),ExpressionValue::VectorValue(vec3(0)),ExpressionValue::NameValue(weapon->GetName("DamageType")),ExpressionValue::IntValue(0)});
        int applied=std::max(0,before-target->Health());
        if(Alive(target) && (defended || applied>0)) {
            bool resistant=(target->HasProperty("bIsBoss") && target->GetBool("bIsBoss")) || target->Mass()>200;
            float weight=std::clamp(100.f/std::max(1.f,target->Mass()),.25f,1.25f);
            target->RuneKickRecoil=Forward(p)*((defended||resistant?6.f:24.f)*weight);target->RuneKickRecoilTime=.18f;
        }
        if(applied>0)RunePhysics::Impact(p,target,Forward(p)*300.f+vec3(0,0,220),defended);
        RuneFeedback::Impact(p,target,defended,false);Sound(p,defended?"HitShield":"HitFlesh");
        ++p->Slam.hits;LogMessage("RUNE_SLAM_HIT target="+target->Name.ToString()+" blocked="+std::to_string(defended)+" requested="+std::to_string(damage)+" applied="+std::to_string(applied));
    };
#ifdef OPENRUNE_ENABLE_NETWORK
    if(engine->NetworkCombatAuthority && engine->NetworkCombatAuthority->deferContact(p,target,shield||cross,weapon,commit))return;
#endif
    commit(shield||cross);
}
}
bool Heavy(UWeapon* w){
    return w && !w->bDeleteMe() && !RuneDualWield::OneHanded(w) &&
        (w->IsA("Sword") || w->IsA("Axe") || w->IsA("Hammer") || w->IsA("PlayerSarkSword") || w->IsA("PlayerSarkAxe") || w->IsA("PlayerSarkHammer"));
}
bool Supported(UPawn* p) {
    if(RuneSpider::Busy(p))return false;
    if(!RuneBattleLaws::Allowed(RuneBattleLaws::Slam) || !Alive(p) || !engine->LaunchInfo.IsRune() || (!p->IsA("RunePlayer") && !p->IsA("Viking")) || (p->HasProperty("bIsBoss") && p->GetBool("bIsBoss")))return false;
    auto* m=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(p));return m && Joint(*m,"lhip")>=0 && Joint(*m,"rhip")>=0 && Joint(*m,"attach_hand")>=0 && Heavy(p->Weapon()) && !RuneDualWield::Equipped(p);
}
bool Busy(UPawn* p){return p && p->Slam.phase!=0;}
bool Threat(UObject* object){auto* p=UObject::TryCast<UPawn>(object);auto* enemy=p?p->Enemy():nullptr;return Alive(p) && Alive(enemy) && enemy->Slam.phase==1 && length(enemy->Location()-p->Location())<220 && p->FastTrace(enemy->Location(),p->Location());}
bool Arm(UPawn* p){
    if(!Supported(p) || !CombatState(p) || p->NetworkReplica || Busy(p) || p->Slam.cooldown>0 || p->Physics()!=PHYS_Falling || RuneCorpse::Busy(p) || p->RuneKickActive || p->RuneBashPhase)return false;
    auto* pp=UObject::TryCast<UPlayerPawn>(p);if(pp && pp->ViewTarget())return false;
    p->Slam.armed=.75f;p->Slam.weapon=p->Weapon();LogMessage("RUNE_SLAM_ARM actor="+p->Name.ToString());return true;
}
bool CanStart(UPawn* p){return Supported(p) && CombatState(p) && !p->NetworkReplica && p->Slam.armed>0 && !Busy(p) && p->Slam.cooldown<=0 && p->Physics()==PHYS_Falling && p->Weapon()==p->Slam.weapon && !RuneCorpse::Busy(p) && !p->RuneKickActive && !p->RuneBashPhase && (p->Bloodlust.phase==0 || p->Bloodlust.phase==RuneBloodlust::Leaping);}
bool Start(UPawn* p){
    if(!CanStart(p))return false;
    auto& s=p->Slam;s.phase=1;s.age=0;s.armed=0;s.cooldown=2;s.launchRestored=false;
    const auto momentum=p->Velocity();
    p->Bloodlust.phase=0;p->Bloodlust.age=0;p->Bloodlust.rushing=false;
    CallEvent(p->Weapon(),"FinishAttack");CallEvent(p->Weapon(),"DisableSwipeTrail");
    p->SetBool("bSwingingHigh",false);p->SetBool("bSwingingLow",false);CallEvent(p,"ActivateShield",{ExpressionValue::BoolValue(false)});
    CallEvent(p,"PlayUninterruptedAnim",{ExpressionValue::NameValue("neutral_idle")});
    p->SetPhysics(PHYS_Falling);
    const float up=p->Region().Zone->ZoneGravity().z>0?-1.f:1.f;
    // A single attack launch, not a repeatable second-jump input. Preserve any
    // stronger existing leap, horizontal momentum, and ordinary swept collision.
    s.launchVelocity=momentum;s.launchVelocity.z=up*std::max(up*momentum.z,LiftSpeed);p->Velocity()=s.launchVelocity;
    Sound(p,"ThroughAir");LogMessage("RUNE_SLAM_START actor="+p->Name.ToString()+" z="+std::to_string(p->Location().z)+" vz="+std::to_string(p->Velocity().z)+" gravity="+std::to_string(p->Region().Zone->ZoneGravity().z));return true;
}
void Cancel(UPawn* p){
    auto& s=p->Slam;s.phase=0;s.age=0;s.armed=0;s.weapon=nullptr;
    if(Alive(p) && p->GetStateName()=="Uninterrupted") {
        p->GotoState(p->IsA("RunePlayer")?"PlayerWalking":"Fighting","Begin");
        if(auto* proxy=p->GetUObject("AnimProxy"))proxy->GotoState("Idle","Begin");
    }
}
void Tick(UPawn* p,float elapsed){
    if(!engine->LaunchInfo.IsRune() || p->NetworkReplica)return;
    auto& s=p->Slam;float dt=std::clamp(elapsed,0.f,.05f);s.cooldown=std::max(0.f,s.cooldown-dt);s.armed=std::max(0.f,s.armed-dt);
    if(!s.phase) {
        s.air=p->Physics()==PHYS_Falling?s.air+dt:0;
        if(!p->IsA("RunePlayer") && s.air>.1f && Supported(p) && Alive(p->Enemy())) {
            auto delta=p->Enemy()->Location()-p->Location();float distance=length(delta);delta.z=0;
            if(distance<170 && length(delta)>.01f && dot(Forward(p),normalize(delta))>.65f && p->FastTrace(p->Enemy()->Location(),p->Location()) && Arm(p))Start(p);
        }
        return;
    }
    if(!Supported(p) || p->Weapon()!=s.weapon || p->RuneBashPhase || p->RuneKickActive || (p->GetStateName()!="Uninterrupted" && p->GetStateName()!="PlayerWalking" && p->GetStateName()!="Fighting") || (p->Physics()!=PHYS_Falling && p->Physics()!=PHYS_Walking && p->Physics()!=PHYS_None)){Cancel(p);return;}
    const float previousAge=s.age;s.age+=dt;p->Acceleration()=vec3(0);
    // The original Uninterrupted latent entry freezes physics. Keep real
    // gravity/collision; never move the actor to follow the cosmetic flip.
    if(p->Physics()==PHYS_None) {
        p->SetPhysics(PHYS_Falling);
        if(s.phase==1 && s.age<=.1f && !s.launchRestored) {
            // The original latent Uninterrupted entry can zero physics and
            // velocity after Start. Restore this launch once, never every tick.
            p->Velocity()=s.launchVelocity;s.launchRestored=true;
            LogMessage("RUNE_SLAM_LAUNCH_RESTORED actor="+p->Name.ToString());
        }
    }
    if(s.phase==1 && s.age>=Windup) {
        if(p->Physics()==PHYS_Walking) {
            Impact(p);s.phase=2;s.age=0;
        } else {
            if(previousAge<Windup) {
                const float up=p->Region().Zone->ZoneGravity().z>0?-1.f:1.f;
                p->Velocity().z=up*std::min(up*p->Velocity().z,-DiveSpeed);
                LogMessage("RUNE_SLAM_DIVE actor="+p->Name.ToString());
            }
            // Falling off a ledge keeps real fall physics and damage. It never
            // grants a remote hit or traps the player in the move indefinitely.
            if(s.age>=Windup+MaxDiveTime)Cancel(p);
        }
    }
    else if(s.phase==2 && s.age>=Recovery)Cancel(p);
}
void BeforeSave(){if(engine->Level)for(auto* a:engine->Level->Actors)if(auto* p=UObject::TryCast<UPawn>(a);p && Busy(p))Cancel(p);}
void Landed(UPawn* p,UActor* surface){
    if(!p || p->NetworkReplica || p->Slam.phase!=1 || p->Slam.age<Windup)return;
    Impact(p,UObject::TryCast<UPawn>(surface));
    p->Slam.phase=2;p->Slam.age=0;
    LogMessage("RUNE_SLAM_LANDING_CONTACT pawn="+std::to_string(bool(UObject::TryCast<UPawn>(surface))));
}
void Apply(const USkelModel& m,UActor* actor,Array<RuneJointPose>& pose){
    auto* p=UObject::TryCast<UPawn>(actor);if(!p || !Busy(p) || pose.empty() || !p->Weapon())return;
    auto name=p->Weapon()->GetName("A_AttackStandA");size_t seq=m.Sequences.size();
    for(size_t i=0;i<m.Sequences.size();++i)if(m.Sequences[i].Info.Name==name){seq=i;break;}
    if(seq==m.Sequences.size()) {
        for(size_t i=0;i<m.Sequences.size();++i)if(m.Sequences[i].Info.Name=="neutral_idle"){seq=i;break;}
        if(seq==m.Sequences.size())seq=0;
        if(m.Sequences.empty())return;
    }
    auto& s=p->Slam;float t=s.phase==1?std::clamp(s.age/Windup,0.f,1.f):1;
    float sample=s.phase==1?.15f+.7f*Smooth((s.age-Windup)/.12f):.85f+.15f*Smooth(s.age/.18f);
    auto attack=m.SampleFrame(seq,sample*std::max(1,m.Sequences[seq].Info.NumFrames-1),false);
    float blend=s.phase==1?Smooth(s.age/.06f):1-Smooth((s.age-.15f)/(Recovery-.15f));
    for(size_t i=0;i<pose.size();++i)pose[i]=BlendRunePose(pose[i],attack[i],blend);
    if(s.phase!=1)return;
    auto bones=m.JointMatrices(pose);int hip=Joint(m,"lhip"),other=Joint(m,"rhip");if(hip<0 || other<0)return;
    vec3 center=(vec3(bones[hip][12],bones[hip][13],bones[hip][14])+vec3(bones[other][12],bones[other][13],bones[other][14]))*.5f;
    auto meshToActor=Coords::Rotation(m.RotationOffset).ToMatrix();
    auto flip=mat4::translate(center)*mat4::transpose(meshToActor)*mat4::rotate(6.28318530718f*Smooth(t),0,1,0)*meshToActor*mat4::translate(-center);
    auto root=flip*bones[0];pose[0].Position={root[12],root[13],root[14]};
    for(int c=0;c<3;++c)for(int row=0;row<3;++row)root[c*4+row]/=std::max(.001f,pose[0].Scale[c]);
    root[12]=root[13]=root[14]=0;pose[0].Rotation=RunePoseEuler(mat4::transpose(root));
}
bool ScriptCall(UFunction* f,UObject* object,const ArrayView<ExpressionValue>&,ExpressionValue& result){
    if(!engine->LaunchInfo.IsRune())return false;auto* p=Owner(object);if(!p || p->NetworkReplica)return false;auto name=f->Name;
    if(name=="Jump" && object==p) {
#ifdef OPENRUNE_ENABLE_NETWORK
        if(engine->NetworkCombatAuthority)return false;
#endif
        if(Busy(p))return true;
        float now=engine->LevelInfo->TimeSeconds();bool twice=p->Slam.tap>=0 && now-p->Slam.tap<=.32f;
        if(twice && Arm(p)) {p->Slam.tap=-1;if(!(RuneBloodlust::Readiness(p)&1))return true;}
        else if(p->Physics()==PHYS_Walking)p->Slam.tap=now;
    }
    if((name=="Fire" || name=="Attack") && !Busy(p) && Start(p)){result=ExpressionValue::BoolValue(true);return true;}
    if(!Busy(p))return false;
    if(name=="GrabEdge" && object==p){result=ExpressionValue::BoolValue(false);return true;}
    for(auto blocked:{"Fire","Attack","AltFire","UseNotify","WeaponActivate","SwipeEffectStart","DoWeaponSwipe","Throw","ThrowWeapon","DropWeapon","SwitchWeapon","InstantStow","AnimEnd"})if(name==blocked){result=ExpressionValue::BoolValue(true);return true;}
    return false;
}
}
