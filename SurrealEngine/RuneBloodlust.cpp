#include "Precomp.h"
#include "RuneBloodlust.h"
#include "RuneRushPose.h"
#include "RuneDualWield.h"
#include "RuneKick.h"
#include "RuneTactics.h"
#include "RuneFeedback.h"
#ifdef OPENRUNE_ENABLE_NETWORK
#include "RuneNetwork.h"
#endif
#include "Engine.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Inventory/UWeapon.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "Packages/Core/UFunction.h"
#include "VM/Frame.h"
#include "VM/ScriptCall.h"
#include <algorithm>
#include <cmath>
namespace RuneBloodlust {
namespace {
constexpr float ChargeDistance=560.f,ChargeSpeed=900.f;
bool Alive(UPawn* p){return p && !p->bDeleteMe() && !p->bHidden() && p->Health()>0;}
bool Player(UPawn* p){return p && p->IsA("RunePlayer");}
vec3 Forward(UPawn* p){float a=p->Rotation().YawRadians();return {std::cos(a),std::sin(a),0};}
UPawn* Owner(UObject* o){if(auto* p=UObject::TryCast<UPawn>(o))return p;auto* a=UObject::TryCast<UActor>(o);return a && a->IsA("AnimationProxy")?UObject::TryCast<UPawn>(a->Owner()):nullptr;}
UActor* Shield(UPawn* p){
    if(RuneDualWield::Equipped(p))return nullptr;
    auto* s=UObject::TryCast<UActor>(p->GetUObject("Shield"));
    return s && !s->bDeleteMe() && !s->bHidden() && s->Owner()==p && s->GetInt("Health")>0 && s->GetUObject("AttachParent")==p?s:nullptr;
}
bool StateAllows(UPawn* p){
    if(auto* pp=UObject::TryCast<UPlayerPawn>(p))return !pp->ViewTarget() && p->GetStateName()=="PlayerWalking";
    return p->GetStateName()=="Fighting";
}
bool TargetAllows(UPawn* p){
    if(Player(p))return StateAllows(p);
    auto state=p->GetStateName();
    return state=="Fighting" || state=="Charging" || state=="TacticalMove" || state=="Retreating" || state=="Pain" ||
        (state=="Uninterrupted" && (Busy(p) || p->RuneKickActive || p->RuneBashPhase));
}
bool Idle(UPawn* p){
    auto* proxy=p->GetUObject("AnimProxy");
    return !p->RuneKickActive && !p->RuneBashPhase && !p->Tactics.attacking &&
        !p->GetBool("bSwingingHigh") && !p->GetBool("bSwingingLow") &&
        (!proxy || proxy->GetStateName()=="Idle" || proxy->GetStateName()=="Defending");
}
const MeshAnimSeq* Sequence(UPawn* p,const NameString& name){
    auto* m=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(p));
    if(m)for(auto& seq:m->Sequences)if(seq.Info.Name==name && seq.Info.NumFrames>1 && seq.Info.Rate>0)return &seq.Info;
    return nullptr;
}
float Meter(UPawn* p){return Player(p)?100.f*p->GetInt("Strength")/std::max(1,(int)p->GetInt("MaxStrength")):p->Bloodlust.rage;}
void Spend(UPawn* p){
    if(Active(p) && Meter(p)>=50){
    if(Player(p)) {p->SetInt("Strength",std::max(0,(int)p->GetInt("Strength")-((int)p->GetInt("MaxStrength")+1)/2));if(p->GetInt("Strength")==0){p->SetInt("Strength",1);CallEvent(p,"StrengthDecay",{ExpressionValue::FloatValue(999)});}}
    else p->Bloodlust.rage=std::max(0.f,p->Bloodlust.rage-50);
    }
    p->Bloodlust.cooldown=4;p->Bloodlust.tap=-1;
}
void Cold(UPawn* p){
    for(auto* w:{p->Weapon(),p->Dual.offhand})if(w && !w->bDeleteMe()){CallEvent(w,"FinishAttack");CallEvent(w,"DisableSwipeTrail");}
    p->SetBool("bSwingingHigh",false);p->SetBool("bSwingingLow",false);p->Dual.guard=false;
    p->Acceleration()=vec3(0);p->Velocity().x=p->Velocity().y=0;
}
void Finish(UPawn* p){
    auto& s=p->Bloodlust;s.phase=0;s.age=0;s.rushing=false;s.victims.clear();
    CallEvent(p,"ActivateShield",{ExpressionValue::BoolValue(false)});
    if(Alive(p) && p->GetStateName()=="Uninterrupted") {
        p->GotoState(Player(p)?"PlayerWalking":"Fighting","Begin");
        if(auto* proxy=p->GetUObject("AnimProxy"))proxy->GotoState("Idle","Begin");
    }
}
void Recover(UPawn* p){p->Bloodlust.phase=Recovery;p->Bloodlust.age=0;p->Velocity().x=p->Velocity().y=0;CallEvent(p,"ActivateShield",{ExpressionValue::BoolValue(false)});}
void Animate(UPawn* p,const NameString& name){
    p->Bloodlust.animation=name;
    if(Sequence(p,name))CallEvent(p,"PlayUninterruptedAnim",{ExpressionValue::NameValue(name)});
}
bool Landing(UPawn* p,vec3 dir,float distance){
    auto end=p->Location()+dir*distance;vec3 hit,normal;
    if(!p->Trace(hit,normal,end-vec3(0,0,220),end+vec3(0,0,80),false,vec3(p->CollisionRadius()*.6f,p->CollisionRadius()*.6f,0)) || normal.z<.7f)return false;
    if(hit.z>p->Location().z+60 || hit.z<p->Location().z-p->CollisionHeight()-120)return false;
    auto zone=p->Level()->GetLocZone(hit+vec3(0,0,p->CollisionHeight()),{}).Zone;
    return zone && !zone->bPainZone() && !zone->bWaterZone();
}
// Rush retains its fixed heading, but uses the walking step-height and floor
// attachment rules. Every leg of the step is swept; no collision teleporting.
CollisionHit GroundRush(UPawn* p,vec3 delta){
    const float height=std::max(0.f,p->MaxStepHeight());
    const float gravity=p->Region().Zone->ZoneGravity().z>0?1.f:-1.f;
    const vec3 up(0,0,-gravity),down=up*(-height*1.3f);
    auto hit=p->TryMove(delta);
    bool grounded=false;
    if(hit.Fraction<1 && (!hit.Actor || !hit.Actor->IsA("Pawn")) && height>0){
        auto impact=p->Location();
        p->TryMove(up*height);
        float rise=std::max(0.f,dot(p->Location()-impact,up));
        if(rise>.01f){
            auto forward=p->TryMove(delta*(1-hit.Fraction));
            grounded=p->TryStepToGround(down-up*rise);
            if(grounded)hit=forward;
            else {
                // Do not leave a failed step attempt suspended above a ledge.
                p->TryMove(-up*rise);
            }
        }
    }
    if(!grounded)grounded=p->TryStepToGround(down);
    if(!grounded){p->SetPhysics(PHYS_Falling);p->SetBase(nullptr,true);}
    return hit;
}
void Launch(UPawn* p){
    auto& s=p->Bloodlust;s.phase=Leaping;s.age=0;s.origin=p->Location();s.travel=0;
    p->SetPhysics(PHYS_Falling);p->Velocity()=s.direction*(s.limit>0?800.f:0.f)+vec3(0,0,650);p->Acceleration()=vec3(0);
    if(!Player(p))CallEvent(p,"PlayJumping",{ExpressionValue::FloatValue(.1f)});
    s.leaps++;LogMessage("RUNE_BLOOD_LEAP actor="+p->Name.ToString());
}
void Gain(UPawn* p,float amount){
    if(!Eligible(p) || Player(p) || p->Bloodlust.active || amount<=0)return;
    auto& s=p->Bloodlust;s.rage=std::min(100.f,s.rage+amount);
    if(s.rage>=100){s.active=true;
        LogMessage("RUNE_ENEMY_BLOODLUST actor="+p->Name.ToString());}
}
}
bool Eligible(UPawn* p){
    if(!engine || !engine->LaunchInfo.IsRune() || !Alive(p) || p->NetworkReplica)return false;
#ifdef OPENRUNE_ENABLE_NETWORK
    if(engine->NetworkClient)return false;
#endif
    if(Player(p))return true;
    if(!p->IsA("DarkViking") || (p->HasProperty("bIsBoss") && p->GetBool("bIsBoss")))return false;
    // Actor indices compact as pickups/effects disappear. Use the saved actor
    // name so eligibility cannot change in the middle of a move or after load.
    uint32_t hash=2166136261u;for(unsigned char c:p->Name.ToString())hash=(hash^(c>='a'&&c<='z'?c-32:c))*16777619u;
    return hash%3==1;
}
bool Busy(UPawn* p){return p && p->Bloodlust.phase!=0;}
bool Active(UPawn* p){return Alive(p) && (p->NetworkReplica?p->Bloodlust.active:Eligible(p) && (Player(p)?p->GetBool("bBloodLust"):p->Bloodlust.active));}
unsigned Readiness(UPawn* p){
    if(!p || p->NetworkReplica || RuneCorpse::Busy(p))return 0;
    if(!Enabled.load() || !Eligible(p) || (!AllowNormal.load() && (!Active(p) || Meter(p)<50)) || Busy(p) || p->Bloodlust.cooldown>0 || !StateAllows(p) || !Idle(p))return 0;
    unsigned mask=(p->Physics()==PHYS_Walking || (p->Physics()==PHYS_Falling && p->Bloodlust.tap>=0 && engine->LevelInfo->TimeSeconds()-p->Bloodlust.tap<=.32f))?1:0;
    if(p->Physics()==PHYS_Walking && Shield(p) && RuneRushPose::Supported(p))mask|=2;
    return mask;
}
bool Leap(UPawn* p,vec3 direction){
    if(!(Readiness(p)&1))return false;
    direction.z=0;direction=length(direction)>.1f?normalize(direction):Forward(p);
    float distance=600.f;
    if(!Player(p) && p->Enemy()){auto d=p->Enemy()->Location()-p->Location();d.z=0;distance=std::clamp(length(d)-60.f,160.f,600.f);}
    if(Player(p)) {
        // Include the actual swept approach to a wall before trying coarse
        // fallback distances. Rounding that approach down to 150/300 units
        // stops outside grab reach even when the ledge is climbable.
        const auto wall=p->TryMove(direction*distance,true);
        const float approach=distance*wall.Fraction;
        bool supported=wall.Fraction<1 && std::abs(wall.Normal.z)<.2f &&
            approach>0 && Landing(p,direction,approach);
        if(supported)distance=approach;
        else for(float candidate:{600.f,450.f,300.f,150.f,60.f,0.f}) {
            if((candidate==0 || p->FastTrace(p->Location()+direction*candidate,p->Location())) && Landing(p,direction,candidate)) {
                distance=candidate;supported=true;break;
            }
        }
        if(!supported)return false;
    } else if(!Landing(p,direction,distance))return false;
    auto& s=p->Bloodlust;Spend(p);Cold(p);s.rushing=false;s.direction=direction;s.limit=distance;s.age=0;s.origin=p->Location();
    if(Player(p))Launch(p);
    else {s.phase=LeapWindup;Animate(p,"H3_DefendAttack");LogMessage("RUNE_BLOOD_LEAP_WINDUP actor="+p->Name.ToString());}
    return true;
}
bool Charge(UPawn* p){
    if(!(Readiness(p)&2))return false;
    auto& s=p->Bloodlust;Spend(p);Cold(p);s.phase=ChargeWindup;s.rushing=true;s.age=0;s.travel=0;s.blocked=0;s.direction=Forward(p);s.victims.clear();
    Animate(p,"neutral_idle");CallEvent(p,"ActivateShield",{ExpressionValue::BoolValue(false)});
    LogMessage("RUNE_BLOOD_CHARGE_WINDUP actor="+p->Name.ToString());return true;
}
bool JumpPressed(UPawn* p){
    if(!Enabled.load() || !Eligible(p))return false;
    if(Busy(p))return true;
    auto& s=p->Bloodlust;float now=engine->LevelInfo->TimeSeconds();
    bool twice=s.tap>=0 && now-s.tap<=.32f;
    if(twice){
        vec3 direction=p->Acceleration();direction.z=0;
        if(Leap(p,direction))return true;
        s.tap=-1;
    } else if(p->Physics()==PHYS_Walking)s.tap=now;
    return false; // First tap always remains the original responsive jump.
}
void Tick(UPawn* p,float elapsed){
    if(!engine->LaunchInfo.IsRune() || p->NetworkReplica)return;
    if(Player(p)){
        ReadyMask.store(0);
        if(auto* eyes=UObject::TryCast<UActor>(p->GetUObject("BloodLustEyes")))eyes->bHidden()=true;
    }
    auto& s=p->Bloodlust;float dt=std::clamp(elapsed,0.f,.1f);s.cooldown=std::max(0.f,s.cooldown-dt);
    if(s.active && (!Alive(p) || !Enabled.load() || (s.rage=std::max(0.f,s.rage-5*dt))<=0)){s.active=false;}
    if(Busy(p)) {
        // PlayerPawn.Uninterrupted freezes physics in its latent Begin label.
        // These moves still need walking collision/gravity after that label runs.
        if(Alive(p) && p->GetStateName()=="Uninterrupted" && p->Physics()==PHYS_None)p->SetPhysics(PHYS_Walking);
        if(!Enabled.load() || !Eligible(p) || (s.phase!=Leaping && p->Physics()!=PHYS_Walking) || (p->GetStateName()!="Uninterrupted" && !StateAllows(p))) {LogMessage("RUNE_BLOOD_CANCEL actor="+p->Name.ToString()+" phase="+std::to_string(s.phase)+" state="+p->GetStateName().ToString()+" physics="+std::to_string(p->Physics())+" health="+std::to_string(p->Health())+" hidden="+std::to_string((bool)p->bHidden())+" index="+std::to_string(p->Index)+" eligible="+std::to_string(Eligible(p))+" enabled="+std::to_string(Enabled.load())+" walking="+std::to_string(PHYS_Walking));Finish(p);return;}
        s.age+=dt;p->Acceleration()=vec3(0);
        if(s.phase==LeapWindup) {p->Velocity()=vec3(0);if(s.age>=.45f)Launch(p);}
        else if(s.phase==ChargeWindup || s.phase==Charging) {
            p->Velocity()=vec3(0);p->SetRotation(Rotator::FromVector(s.direction));
            if(!Shield(p)){Recover(p);return;}
            CallEvent(p,"ActivateShield",{ExpressionValue::BoolValue(s.phase==Charging || s.age>=.4f)});
            if(s.phase==ChargeWindup && s.age>=(Player(p)?RuneRushPose::PlayerPrepare:RuneRushPose::EnemyPrepare)){s.phase=Charging;s.age=0;s.charges++;LogMessage("RUNE_BLOOD_CHARGE actor="+p->Name.ToString());}
        } else if(s.phase==Leaping) {
            if(p->Physics()!=PHYS_Falling && s.age>.08f){Recover(p);}
            else if(s.age>2){Finish(p);}
        } else if(s.phase==Recovery && s.age>=(s.rushing?RuneRushPose::RecoverSeconds:.3f))Finish(p);
    } else if(Enabled.load() && Eligible(p) && !Player(p) && (Active(p) || AllowNormal.load()) && StateAllows(p) && Idle(p)) {
        s.think-=dt;
        if(s.think<=0){s.think=.6f;auto* enemy=p->Enemy();
            if(Alive(enemy) && RuneTactics::MayCommit(p) && p->FastTrace(enemy->Location(),p->Location())) {
                auto d=enemy->Location()-p->Location();d.z=0;float distance=length(d);
                if(distance>130 && distance<400 && dot(Forward(p),normalize(d))>.9f && Shield(p))Charge(p);
                else if(distance>180 && distance<380)Leap(p,d);
            }
        }
    }
    if(Player(p))ReadyMask.store(Readiness(p));
}
void Move(UPawn* p,float elapsed){
    auto& s=p->Bloodlust;if(!Busy(p) || !Alive(p))return;
    float dt=std::clamp(elapsed,0.f,.05f);
    if(s.phase==Leaping){
        auto d=p->Location()-s.origin;d.z=0;s.travel=length(d);
        if(s.travel>=s.limit || s.age>1.2f)p->Velocity().x=p->Velocity().y=0;
        return;
    }
    if(s.phase!=Charging)return;
    p->Acceleration()=vec3(0);p->Velocity()=vec3(0);p->SetRotation(Rotator::FromVector(s.direction));
    float step=std::min(ChargeSpeed*dt,ChargeDistance-s.travel);bool stop=false;
    // Damage can spawn particles and grow Actors; never retain an iterator
    // across a script callback, and do not hit actors spawned by this charge.
    for(size_t i=0,count=p->XLevel()->Actors.size();i<count;++i){auto* target=UObject::TryCast<UPawn>(p->XLevel()->Actors[i]);
        if(!Alive(target) || target==p || target->bStatic() || s.victims.size()>=32 || std::find(s.victims.begin(),s.victims.end(),target)!=s.victims.end())continue;
        if(!TargetAllows(target) || (!Player(p) && target!=p->Enemy()) || (Player(p) && !Player(target) && target->Enemy()!=p))continue;
        auto d=target->Location()-p->Location();float radius=p->CollisionRadius()+target->CollisionRadius();float along=dot(d,s.direction);auto side=d-s.direction*along;side.z=0;
        if(along<0 || along>step+radius || length(side)>radius || std::abs(d.z)>p->CollisionHeight()+target->CollisionHeight()*.5f || !p->FastTrace(target->Location(),p->Location()))continue;
        s.victims.push_back(target);s.hits++;
        bool blocked=RuneKick::ShieldBlocks(target,p);
        bool resistant=(target->HasProperty("bIsBoss") && target->GetBool("bIsBoss")) || target->Mass()>200;
        auto impact=[p,target,resistant,direction=s.direction,sourceShield=Shield(p)](bool defended) {
            if(!sourceShield || Shield(p)!=sourceShield)return;
            if(!resistant)CallEvent(target,"JointDamaged",{ExpressionValue::IntValue(defended?3:15),ExpressionValue::ObjectValue(p),ExpressionValue::VectorValue(target->Location()),ExpressionValue::VectorValue(vec3(0)),ExpressionValue::NameValue("blunt"),ExpressionValue::IntValue(0)});
            if(Alive(target)){target->RuneKickRecoil=direction*(defended||resistant?8.f:100.f);target->RuneKickRecoilTime=.18f;}
            RuneFeedback::Impact(p,target,defended,true);
            if((defended || resistant) && p->Bloodlust.phase==Charging)Recover(p);
        };
#ifdef OPENRUNE_ENABLE_NETWORK
        bool deferred=engine->NetworkCombatAuthority && engine->NetworkCombatAuthority->deferContact(p,target,blocked,nullptr,impact,false,true);
        if(!deferred)impact(blocked);
#else
        impact(blocked);
#endif
        stop=stop||blocked||resistant;
        LogMessage("RUNE_BLOOD_CHARGE_HIT target="+target->Name.ToString()+" blocked="+std::to_string(blocked)+" resistant="+std::to_string(resistant));
    }
    auto before=p->Location();auto hit=GroundRush(p,s.direction*step);float moved=std::max(0.f,dot(p->Location()-before,s.direction));s.travel+=moved;
    s.blocked=moved<step*.2f?s.blocked+dt:0;
    if(stop || p->Physics()!=PHYS_Walking || (hit.Fraction<1 && (!hit.Actor || !hit.Actor->IsA("Pawn"))) || s.blocked>.1f || s.travel>=ChargeDistance-1 || s.age>=ChargeDistance/ChargeSpeed+.15f)Recover(p);
}
bool ScriptCall(UFunction* f,UObject* object,const ArrayView<ExpressionValue>& args,ExpressionValue& result){
    auto name=f->Name;
    static const NameString calls[]={"Jump","Attack","JointDamaged","DamageBodyPart","PawnDamageModifier","PlayerMove","WeaponActivate","SwipeEffectStart","UseNotify","Throw","ThrowWeapon","DropWeapon","SwitchWeapon","InstantStow"};
    if(std::find(std::begin(calls),std::end(calls),name)==std::end(calls))return false;
    auto* p=Owner(object);if(!p || !Enabled.load() || !Eligible(p))return false;
    if(p->Bloodlust.rushing && Busy(p) && (name=="Throw" || name=="ThrowWeapon" || name=="DropWeapon" || name=="SwitchWeapon" || name=="InstantStow"))return true;
    if(name=="Jump" && object==p) {
#ifdef OPENRUNE_ENABLE_NETWORK
        if(engine->NetworkCombatAuthority)return false;
#endif
        return JumpPressed(p);
    }
    if(name=="Attack" && object!=p){if(Busy(p)){result=ExpressionValue::BoolValue(false);return true;}if(p->GetByte("bAltFire") &&
#ifdef OPENRUNE_ENABLE_NETWORK
        !engine->NetworkCombatAuthority &&
#endif
        Charge(p)){result=ExpressionValue::BoolValue(true);return true;}}
    if(Busy(p) && (name=="WeaponActivate" || name=="SwipeEffectStart" || name=="UseNotify" || (name=="PlayerMove" && p->Bloodlust.phase!=Leaping)))return true;
    if(name=="JointDamaged" && args.size()>=6 && !p->Bloodlust.damageHook) {
        auto type=args[4].ToName();if(type!="blunt" && type!="sever" && type!="bluntsever")return false;
        int health=p->Health();auto* attacker=UObject::TryCast<UPawn>(args[1].ToObject());
        struct Scope{bool& v;Scope(bool& x):v(x){v=true;}~Scope(){v=false;}} scope(p->Bloodlust.damageHook);
        result=Frame::Call(f,object,args);int damage=std::max(0,health-p->Health());Gain(p,(float)damage);Gain(attacker,damage*.5f);return true;
    }
    if(!Player(p) && Active(p) && !p->Bloodlust.bodyHook && name=="DamageBodyPart" && args.size()>=6 && args[0].ToInt()>0) {
        auto& s=p->Bloodlust;float damage=(float)args[0].ToInt();float absorbed=std::min(damage,s.rage*2.f);s.rage-=absorbed*.5f;
        if(s.rage<=0){s.active=false;}
        if(absorbed>=damage){result=ExpressionValue::BoolValue(true);return true;}
        std::vector<ExpressionValue> adjusted;for(auto arg:args){arg.Load();adjusted.push_back(arg);}
        adjusted[0]=ExpressionValue::IntValue((int)std::ceil(damage-absorbed));
        struct Scope{bool& v;Scope(bool& x):v(x){v=true;}~Scope(){v=false;}} scope(s.bodyHook);
        result=Frame::Call(f,object,{adjusted.data(),adjusted.size()});return true;
    }
    if(!Player(p) && Active(p) && !p->Bloodlust.damageHook && name=="PawnDamageModifier") {
        struct Scope{bool& v;Scope(bool& x):v(x){v=true;}~Scope(){v=false;}} scope(p->Bloodlust.damageHook);
        result=ExpressionValue::FloatValue(Frame::Call(f,object,args).ToFloat()+1.f);return true;
    }
    return false;
}
bool PrepareAnimation(UActor* a,const NameString& name,float& rate,float& tween){
    auto* p=Owner(a);if(!p || !Busy(p) || name!=p->Bloodlust.animation)return false;
    auto* seq=Sequence(p,name);if(!seq)return false;
    tween=.12f;rate=(seq->NumFrames-1)/(seq->Rate*1.6f);return true;
}
bool AnimationFrames(UActor* a){auto* p=Owner(a);return p && Busy(p) && p->Bloodlust.phase!=Leaping;}
}
