#include "RuneBattleLaws.h"
#include "Precomp.h"
#include "RuneDualWield.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Package/Package.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Inventory/UWeapon.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "Packages/Core/UFunction.h"
#include "Packages/Core/UClass.h"
#include "VM/ScriptCall.h"
#include "VM/Frame.h"

namespace RuneDualWield {
namespace {
constexpr int FollowupStarted=-2;
int Joint(const USkelModel* m,const NameString& name) {
    if(m)for(size_t i=0;i<m->Joints.size() && i<50;++i)if(m->Joints[i].Name==name)return (int)i;
    return -1;
}
USkelModel* Model(UPawn* p) { return UObject::TryCast<USkelModel>(GetRuneDisplayMesh(p)); }
UPawn* Pawn(UObject* o) {
    if(auto* p=UObject::TryCast<UPawn>(o))return p;
    auto* a=UObject::TryCast<UActor>(o);
    return a && a->IsA("AnimationProxy")?UObject::TryCast<UPawn>(a->Owner()):nullptr;
}
bool Owned(UPawn* p,UWeapon* w) {
    if(!w || w->bDeleteMe() || w->Owner()!=p)return false;
    int count=0;for(auto* item=p->Inventory();item && count++<128;item=item->Inventory())if(item==w)return true;
    return false;
}
bool SafeState(UPawn* p) {
    if(auto* player=UObject::TryCast<UPlayerPawn>(p))return !player->ViewTarget() && p->GetStateName()=="PlayerWalking";
    return p->GetStateName()=="Fighting" || p->GetStateName()=="Waiting" || p->GetStateName()=="Charging";
}
bool Busy(UPawn* p) {
    auto* proxy=p->GetUObject("AnimProxy");
    return RuneCorpse::Busy(p) || RuneBloodlust::Busy(p) || p->RuneKickActive || p->RuneBashPhase || p->GetBool("bSwingingHigh") || p->GetBool("bSwingingLow") ||
        p->Tactics.attacking || (proxy && proxy->GetStateName()!="Idle");
}
void Cold(UWeapon* w) {
    if(!w || w->bDeleteMe())return;
    if(w->GetStateName()=="Swinging")CallEvent(w,"FinishAttack");
    if(w->GetUObject("Swipe"))CallEvent(w,"DisableSwipeTrail");
}
void Detach(UPawn* p,UWeapon* w) {
    if(w->GetUObject("AttachParent")==p)CallEvent(p,"DetachActorFromJoint",{ExpressionValue::IntValue(w->GetByte("AttachParentJoint"))});
}
void ClearStored(UPawn* p,UWeapon* w) {
    if(p->HasProperty("StowSpot")) {
        auto** slots=static_cast<UObject**>(p->GetProperty("StowSpot"));
        for(int i=0;i<3;++i)if(slots[i]==w)slots[i]=nullptr;
    } else if(p->HasProperty("StowWeapon") && p->GetUObject("StowWeapon")==w)p->SetObject("StowWeapon",nullptr);
}
void RememberStored(UPawn* p,UWeapon* w) {
    if(p->HasProperty("StowSpot") && w->GetByte("MeleeType")<3)
        static_cast<UObject**>(p->GetProperty("StowSpot"))[w->GetByte("MeleeType")]=w;
    else if(p->HasProperty("StowWeapon"))p->SetObject("StowWeapon",w);
}
UWeapon* Spare(UPawn* p) {
    if(Owned(p,p->Dual.offhand) && p->Dual.offhand!=p->Weapon())return p->Dual.offhand;
    if(Owned(p,p->Dual.drawAfterRelease) && p->Dual.drawAfterRelease!=p->Weapon())return p->Dual.drawAfterRelease;
    int count=0;for(auto* item=p->Inventory();item && count++<128;item=item->Inventory()) {
        auto* w=UObject::TryCast<UWeapon>(item);
        if(w && w!=p->Weapon() && Owned(p,w) && !w->IsA("NonStow") && !w->IsA("InvisibleWeapon") && w->GetByte("MeleeType")<3)return w;
    }
    return nullptr;
}
mat4 InverseAffine(const mat4& m) {
    auto r=mat3::inverse(mat3(m));auto out=mat4::identity();
    for(int c=0;c<3;++c)for(int row=0;row<3;++row)out[c*4+row]=r[c*3+row];
    auto t=r*(-vec3(m[12],m[13],m[14]));out[12]=t.x;out[13]=t.y;out[14]=t.z;return out;
}
RuneJointPose Mirror(RuneJointPose p) {
    auto reflection=mat4::scale(-1,1,1);
    p.Position.x=-p.Position.x;
    p.Rotation=RunePoseEuler(reflection*RunePoseRotation(p.Rotation)*reflection);
    return p;
}
}
bool OneHanded(UWeapon* w) {
    // Deliberate light-weapon allowlist. Shield compatibility alone also admits
    // some large weapons; never guess eligibility from that flag.
    return w && !w->bDeleteMe() && (w->IsA("VikingShortSword") || w->IsA("RomanSword") ||
        w->IsA("HandAxe") || w->IsA("GoblinAxe") || w->IsA("RustyMace"));
}
FollowupDamageScope::FollowupDamageScope(UWeapon* value):weapon(value) {
    if(!weapon)return;
    originalDamage=weapon->GetInt("Damage");
    weapon->SetInt("Damage",(originalDamage+1)/2);
}
FollowupDamageScope::~FollowupDamageScope() {
    if(weapon)weapon->SetInt("Damage",originalDamage);
}
bool Eligible(UPawn* p) {
    if(!engine || !engine->LaunchInfo.IsRune() || !p || p->bDeleteMe() || p->NetworkReplica)return false;
#ifdef OPENRUNE_ENABLE_NETWORK
    if(engine->NetworkClient)return false;
#endif
    if(!p->IsA("RunePlayer") && !p->IsA("Viking"))return false;
    auto* m=Model(p);
    return m && Joint(m,"lhand")>=0 && Joint(m,"lwrist")>=0 && Joint(m,"rwrist")>=0 && Joint(m,"attach_hand")>=0 &&
        !(p->HasProperty("bIsBoss") && p->GetBool("bIsBoss"));
}
bool Equipped(UPawn* p) {
    if(p && p->NetworkReplica) {
        auto* w=p->Dual.offhand;
        return p->Health()>0 && OneHanded(p->Weapon()) && OneHanded(w) && w!=p->Weapon() &&
            w->Owner()==p && w->GetUObject("AttachParent")==p && w->GetByte("AttachParentJoint")==Joint(Model(p),"lhand");
    }
    return p && p->Dual.offhand && RuneBattleLaws::Enabled(RuneBattleLaws::Dual,Enabled.load()) && Eligible(p) && OneHanded(p->Weapon()) &&
        static_cast<int*>(p->GetProperty("BodyPartHealth"))[1]>0 && static_cast<int*>(p->GetProperty("BodyPartHealth"))[3]>0 &&
        p->Weapon()!=p->Dual.offhand && Owned(p,p->Dual.offhand) && OneHanded(p->Dual.offhand) &&
        p->Dual.offhand->GetUObject("AttachParent")==p &&
        p->Dual.offhand->GetByte("AttachParentJoint")==Joint(Model(p),"lhand");
}
bool Equip(UPawn* p,UWeapon* w) {
    if(!RuneBattleLaws::Enabled(RuneBattleLaws::Dual,Enabled.load()) || !Eligible(p) || !OneHanded(p->Weapon()) || !OneHanded(w) || w==p->Weapon() ||
        !Owned(p,w) || p->Health()<=0 || static_cast<int*>(p->GetProperty("BodyPartHealth"))[1]<=0 ||
        static_cast<int*>(p->GetProperty("BodyPartHealth"))[3]<=0 || Busy(p))return false;
    if(p->Dual.offhand==w && Equipped(p))return true;
    Unequip(p);Detach(p,w);ClearStored(p,w);
    p->Dual.offhand=w;p->Dual.left=p->Dual.nextLeft=false;
    w->bHidden()=false;w->GotoState("Active","Begin");
    CallEvent(p,"AttachActorToJoint",{ExpressionValue::ObjectValue(w),ExpressionValue::IntValue(Joint(Model(p),"lhand"))});
    if(auto* shield=UObject::TryCast<UActor>(p->GetUObject("Shield"));shield && shield->Owner()==p) {
        shield->GotoState("Idle","Begin");shield->bHidden()=true;
    }
    LogMessage("RUNE_DUAL_EQUIP actor="+p->Name.ToString()+" weapon="+w->Class->Name.ToString());
    return true;
}
void Unequip(UPawn* p,bool drop) {
    auto* w=p->Dual.offhand;
    if(w && p->Dual.left)if(auto* proxy=p->GetUObject("AnimProxy");proxy && proxy->GetStateName()=="Attacking")CallEvent(proxy,"StopAttack");
    p->Dual.followPrimed=false;
    p->Dual.followAvailable=false;
    p->Dual.followStartFrame=-1;
    p->Dual.lastWholeBody=-1;
    p->Dual.offhand=nullptr;p->Dual.guard=false;p->Dual.guardBlend=0;p->Dual.left=false;
    if(!w)return;
    if(auto* shield=UObject::TryCast<UActor>(p->GetUObject("Shield"));shield && !shield->bDeleteMe() && shield->Owner()==p)shield->bHidden()=false;
    if(w->bDeleteMe() || w==p->Weapon() || w->Owner()!=p)return;
    Cold(w);
    Detach(p,w);
    if(drop) {
        ClearStored(p,w);
        CallEvent(w,"DropFrom",{ExpressionValue::VectorValue(p->Location()+vec3(0,0,10))});
        CallEvent(p,"DeleteInventory",{ExpressionValue::ObjectValue(w)});
        w->SetPhysics(PHYS_Falling);w->Velocity()=vec3(0,0,60);w->GotoState("Drop","Begin");
    } else {
        // Preserve inventory/stow slots: no cloned weapon and no lost item.
        auto type=w->GetByte("MeleeType");auto name=type==0?"attatch_sword":type==1?"attach_hammer":"attach_axe";
        int joint=Joint(Model(p),name);
        auto** children=static_cast<UObject**>(p->GetProperty("JointChild"));
        if(joint>=0 && (!children[joint] || children[joint]==w)) {
            CallEvent(p,"AttachActorToJoint",{ExpressionValue::ObjectValue(w),ExpressionValue::IntValue(joint)});
            w->bHidden()=false;RememberStored(p,w);
        } else w->bHidden()=true;
        w->GotoState("Stow","Begin");
    }
}
void BeginSwing(UPawn* p) {
    if(!Equipped(p))return;
    Cold(p->Weapon());Cold(p->Dual.offhand);
    p->Dual.guard=false;p->Dual.left=p->Dual.nextLeft;p->Dual.nextLeft=!p->Dual.nextLeft;
    // The opening right attack is single. Subsequent right attacks can
    // carry a trailing offhand cut after a left strike has been committed.
    p->Dual.followAvailable=!p->Dual.left && p->Dual.followPrimed;
    if(p->Dual.left)p->Dual.followPrimed=true;
    p->Dual.followStartFrame=-1;
    if(!p->Dual.left)p->Dual.lastWholeBody=-1;
    if(p->Dual.left)p->Dual.leftSwings++;else p->Dual.rightSwings++;
}
void Guard(UPawn* p,bool raised) { p->Dual.guard=raised && RuneBattleLaws::Allowed(RuneBattleLaws::Cross) && Equipped(p) && p->Dual.recovery<=0 && p->Health()>0 && SafeState(p) && !Busy(p); }
bool Blocks(UPawn* p,UPawn* attacker) {
    if(!RuneBattleLaws::Allowed(RuneBattleLaws::Cross))return false;
    if(p && p->Dual.contactGuardOverride)return p->Dual.contactGuard;
    if(!attacker || !Equipped(p) || !p->Dual.guard || p->Dual.guardBlend<.8f || p->Dual.recovery>0 || p->Health()<=0 || !SafeState(p) || Busy(p))return false;
    auto d=attacker->Location()-p->Location();d.z=0;
    return length(d)>.01f && dot(normalize(d),vec3(std::cos(p->Rotation().YawRadians()),std::sin(p->Rotation().YawRadians()),0))>=.5f;
}
void BreakGuard(UPawn* p,float seconds) { p->Dual.guard=false;p->Dual.recovery=std::max(p->Dual.recovery,seconds); }
UActor* FullBodyPoseSource(UActor* actor) {
    auto* p=UObject::TryCast<UPawn>(actor);
    if(!p || !Equipped(p) || (!p->NetworkReplica && !SafeState(p)))return nullptr;
    auto* proxy=UObject::TryCast<UActor>(p->GetUObject("AnimProxy"));
    if(!proxy || proxy->bDeleteMe())return nullptr;
    // Standing strikes (including their spins) are authored on the whole skeleton.
    // Their torso proxy owns attack speed/notifies, so its phase is authoritative.
    // Do not couple running attacks to this clock: their legs remain locomotion.
    for(auto field:{"A_AttackStandA","A_AttackStandB"}) {
        auto seq=p->Weapon()->GetName(field);if(seq.IsNone())continue;
        if(proxy->AnimSequence()==seq)return proxy;
    }
    // Check both live attacks first: a new B attack overrides the body's A return.
    for(auto field:{"A_AttackStandA","A_AttackStandB"}) {
        auto seq=p->Weapon()->GetName(field);if(seq.IsNone())continue;
        if(p->AnimSequence()==seq || p->AnimSequence().ToString()==seq.ToString()+"Return")return p;
    }
    if(!proxy->AnimSequence().IsNone() && p->AnimSequence()==proxy->AnimSequence()) {
        for(auto field:{"A_AttackA","A_AttackB","A_AttackC","A_AttackD","A_AttackBackupA","A_AttackBackupB","A_AttackStrafeRight","A_AttackStrafeLeft","A_JumpAttack"})
            if(proxy->AnimSequence()==p->Weapon()->GetName(field))return proxy;
    }
    return nullptr;
}
void UpdatePoseState(UPawn* p) {
    if(!p || !engine)return;
    if(!Equipped(p)){p->Dual.lastWholeBody=-1;return;}
    if(p->Dual.left && FullBodyPoseSource(p))
        p->Dual.lastWholeBody=engine->TotalTime;
}
void Tick(UPawn* p,float dt) {
    if(!Eligible(p))return;
    auto& s=p->Dual;
    if(!RuneBattleLaws::Allowed(RuneBattleLaws::Cross)){s.guard=false;s.guardBlend=0;}
    if(!s.initialized) {
        s.initialized=true;
        auto** children=static_cast<UObject**>(p->GetProperty("JointChild"));
        auto* w=UObject::TryCast<UWeapon>(children[Joint(Model(p),"lhand")]);
        if(Owned(p,w)) {
            s.offhand=w;Cold(w);Cold(p->Weapon());
            if(auto* proxy=p->GetUObject("AnimProxy");proxy && proxy->GetStateName()=="Attacking")proxy->GotoState("Idle","Begin");
        }
    }
    if(s.spawnPending && p->GetStateName()=="Fighting" && !Busy(p)) {
        s.spawnPending=false;
        if(RuneBattleLaws::Enabled(RuneBattleLaws::Dual,Enabled.load()) && p->IsA("DarkViking") && p->Index%3==0 && OneHanded(p->Weapon())) {
            auto* item=p->Spawn(engine->packages->GetPackage("RuneI")->GetClass("HandAxe"),{},{},p->Location()+vec3(0,0,100),{});
            if(item) {
                item->SetFloat("RespawnTime",0); // Grant one real item even in maps using multiplayer pickup rules.
                auto* w=UObject::TryCast<UWeapon>(CallEvent(item,"SpawnCopy",{ExpressionValue::ObjectValue(p)}).ToObject());
                if(w) {w->GotoState("Active","Begin");Equip(p,w);}
            }
        }
    }
    if(s.drawAfterRelease && p->Health()<=0)s.drawAfterRelease=nullptr;
    if(s.drawAfterRelease && SafeState(p) && !Busy(p)) {
        auto* spare=s.drawAfterRelease;s.drawAfterRelease=nullptr;
        if(!p->Weapon() && Owned(p,spare) && p->IsA("RunePlayer") && static_cast<int*>(p->GetProperty("BodyPartHealth"))[1]>0) {
            auto type=spare->GetByte("MeleeType");int joint=Joint(Model(p),type==0?"attatch_sword":type==1?"attach_hammer":"attach_axe");
            auto** children=static_cast<UObject**>(p->GetProperty("JointChild"));
            if(joint>=0) {
                if(auto* previous=UObject::TryCast<UWeapon>(children[joint]);previous && previous!=spare) {Detach(p,previous);previous->bHidden()=true;}
                Detach(p,spare);RememberStored(p,spare);
                CallEvent(p,"AttachActorToJoint",{ExpressionValue::ObjectValue(spare),ExpressionValue::IntValue(joint)});
                spare->bHidden()=false;spare->GotoState("Stow","Begin");
                CallEvent(p,"SwitchWeapon",{ExpressionValue::ByteValue(type+2)});
                LogMessage("RUNE_SPARE_DRAW actor="+p->Name.ToString()+" weapon="+spare->Name.ToString());
            }
        }
    }
    s.recovery=std::max(0.f,s.recovery-std::max(0.f,dt));
    if(s.offhand && (!Equipped(p) || p->Health()<=0))Unequip(p,p->Health()<=0);
    if(!s.offhand && RuneBattleLaws::Enabled(RuneBattleLaws::Dual,Enabled.load()) && p->Health()>0 && SafeState(p) && !Busy(p) && OneHanded(p->Weapon())) {
        int count=0;for(auto* item=p->Inventory();item && count++<128;item=item->Inventory()) {
            auto* w=UObject::TryCast<UWeapon>(item);
            if(w!=p->Weapon() && OneHanded(w) && Equip(p,w))break;
        }
    }
    if(!Equipped(p))return;
    if(!SafeState(p)) {s.lastWholeBody=-1;s.guard=false;s.guardBlend=0;s.left=false;s.followAvailable=false;Cold(s.offhand);return;}
    if(UObject::TryCast<UPlayerPawn>(p))Guard(p,p->GetByte("bAltFire")!=0);
    else if(Busy(p))s.guard=false;
    s.guardBlend=std::clamp(s.guardBlend+(s.guard?1.f:-1.f)*dt/0.12f,0.f,1.f);
    if(!Busy(p)) { s.left=false;s.followAvailable=false;Cold(s.offhand); }
    UpdatePoseState(p);
}
bool ScriptCall(UFunction* f,UObject* object,const ArrayView<ExpressionValue>& args,ExpressionValue& result) {
    auto name=f->Name;
    static const NameString intercepted[]={"LimbSevered","PostBeginPlay","Died","Destroyed","PlayAttack","AcquireInventory","SwitchWeapon",
        "DropShield","Throw","ThrowWeapon","DropWeapon","DeleteInventory","InstantStow","Attack","Defend","ActivateShield","JointDamaged","WeaponActivate","WeaponDeactivate",
        "SwipeEffectStart","SwipeEffectEnd","ClearSwipeArray","FrameNotify","StopAttack"};
    if(std::find(std::begin(intercepted),std::end(intercepted),name)==std::end(intercepted))return false;
    auto* p=Pawn(object);if(!p || !Eligible(p))return false;
    if(name=="DeleteInventory" && args.size()) {
        if(auto* w=UObject::TryCast<UWeapon>(args[0].ToObject()))ClearStored(p,w);
        return false;
    }
    if((name=="Throw" || name=="ThrowWeapon" || name=="DropWeapon") && object==p && p->IsA("RunePlayer") && p->Health()>0 && p->Weapon()) {
        p->Dual.drawAfterRelease=Spare(p);
        if(p->Dual.offhand)Unequip(p);
        return false;
    }
    if(!p->Dual.offhand && name!="PostBeginPlay")return false;
    if(name=="LimbSevered" && args.size() && args[0].ToInt()>=1 && args[0].ToInt()<=4) {Unequip(p,true);return false;}
    if(name=="PostBeginPlay") {p->Dual.spawnPending=true;return false;}
    if(name=="Died" || name=="Destroyed") { Unequip(p,true);return false; }
    if(!Equipped(p))return false;
    bool proxy=object!=p;
    if(proxy && name=="PlayAttack") {
        auto animation=object->GetName("TorsoAnim");
        for(auto field:{"A_AttackA","A_AttackB","A_AttackC","A_AttackD","A_AttackStandA","A_AttackStandB","A_AttackBackupA","A_AttackBackupB","A_AttackStrafeRight","A_AttackStrafeLeft","A_JumpAttack"})
            if(!animation.IsNone() && animation==p->Weapon()->GetName(field)) {BeginSwing(p);break;}
    }
    // Prevent original single-weapon pickup/switch logic from inheriting a live
    // left-hand attachment. The real item remains in its normal inventory slot.
    if(name=="AcquireInventory" || name=="SwitchWeapon" || name=="DropShield" || name=="Throw" || name=="InstantStow") { Unequip(p);return false; }
    if(proxy && name=="Attack" && (p->Dual.guard || p->Dual.recovery>0)) { result=ExpressionValue::BoolValue(false);return true; }
    if(proxy && name=="Defend") { Guard(p,true);result=ExpressionValue::BoolValue(true);return true; }
    if(name=="ActivateShield" && args.size()!=0) { Guard(p,args[0].ToBool());return true; }
    if(name=="JointDamaged" && args.size()>=6 && !p->Dual.damageHook && args[0].ToInt()>0) {
        auto* attacker=UObject::TryCast<UPawn>(args[1].ToObject());auto type=args[4].ToName();
        if((type=="blunt" || type=="sever" || type=="bluntsever") && Blocks(p,attacker)) {
            std::vector<ExpressionValue> adjusted;for(auto& arg:args) {auto value=arg;value.Load();adjusted.push_back(value);}
            adjusted[0]=ExpressionValue::IntValue(std::max(1,(int)std::ceil(args[0].ToInt()*.35f)));
            adjusted[3]=ExpressionValue::VectorValue(args[3].ToVector()*.3f);
            struct Scope {bool& value;Scope(bool& v):value(v){value=true;}~Scope(){value=false;}} scope(p->Dual.damageHook);
            p->Dual.blocks++;result=Frame::Call(f,object,{adjusted.data(),adjusted.size()});return true;
        }
    }
    auto* w=p->Dual.offhand;
    if(!p->Dual.left) {
        if(name=="WeaponDeactivate" || name=="StopAttack") {
            p->Dual.followAvailable=false;
            Cold(w);
        }
        if(name=="FrameNotify" && args.size() && p->Dual.followAvailable &&
           (proxy || !p->IsA("RunePlayer")) && p->Weapon()->GetStateName()=="Swinging") {
            int frame=args[0].ToInt();
            if(p->Dual.followStartFrame==-1)p->Dual.followStartFrame=frame;
            if(p->Dual.followStartFrame==FollowupStarted || frame>=p->Dual.followStartFrame+2) {
                if(p->Dual.followStartFrame!=FollowupStarted) {
                    p->Dual.followStartFrame=FollowupStarted;
                    CallEvent(w,"StartAttack");
                    CallEvent(w,"EnableSwipeTrail");
                }
                if(w->GetStateName()=="Swinging") {
                    FollowupDamageScope damage(w);
                    CallEvent(w,"FrameNotify",{args[0]});
                }
            }
        }
        return false; // The primary hand still receives its normal notify.
    }
    if(name=="WeaponActivate") { CallEvent(w,"StartAttack");return true; }
    if(name=="WeaponDeactivate") {Cold(w);return true;}
    if(name=="SwipeEffectStart") {CallEvent(w,"EnableSwipeTrail");return true;}
    if(name=="SwipeEffectEnd") {CallEvent(w,"DisableSwipeTrail");return true;}
    if(name=="ClearSwipeArray") {CallEvent(w,"ClearSwipeArray");return true;}
    if(name=="FrameNotify" && args.size()) {CallEvent(w,"FrameNotify",{args[0]});return true;}
    return false;
}
void ApplyPose(const USkelModel& model,UActor* actor,Array<RuneJointPose>& pose,int sweepFrame) {
    auto* p=UObject::TryCast<UPawn>(actor);if(!p || !Equipped(p) || (!p->NetworkReplica && !SafeState(p)))return;
    auto source=pose;
    // Original scripts play standing/spinning attacks on both animation groups.
    // Mirror that complete pose; reversing only the torso twists it against the hips.
    bool authoredWholeBody=FullBodyPoseSource(p)!=nullptr;
    bool wholeBody=p->Dual.left && authoredWholeBody;
    // The proxy can enter idle before the body's return animation ends. Fade
    // the lower-body reflection instead of switching the feet across the hips
    // in one frame. This clock is updated by simulation/network presentation,
    // so joint queries, weapon sweeps and rendering sample the same pose.
    float bodyBlend=wholeBody?1.f: p->Dual.lastWholeBody<0?0.f:
        std::clamp(1.f-float(engine->TotalTime-p->Dual.lastWholeBody)/.22f,0.f,1.f);
    // The two legs use different local skinning axes. Mirror animation motion
    // through the reference skeleton instead of copying opposite-leg rotations.
    // Cache only immutable reference data, never an actor's animated pose.
    if(bodyBlend>0 && model.LegMirrorBasis.size()!=source.size()) {
        size_t reference=0;
        for(size_t i=0;i<model.Sequences.size();++i)if(model.Sequences[i].Info.Name=="neutral_idle"){reference=i;break;}
        auto restBones=model.JointMatrices(model.SampleFrame(reference,0,true));
        auto reflection=mat4::scale(-1,1,1);
        Array<mat3> correction;correction.assign(source.size(),mat3::identity());
        model.LegMirrorBasis.resize(source.size());
        for(size_t j=0;j<source.size();++j) {
            auto& basis=model.LegMirrorBasis[j];auto name=model.Joints[j].Name.ToString();basis.Source=(int)j;
            if(name.size()>1 && (name[0]=='l' || name[0]=='r')) {auto other=name;other[0]=name[0]=='l'?'r':'l';int k=Joint(&model,other);if(k>=0)basis.Source=k;}
            int parent=model.Joints[j].Parent;
            basis.Leg=name=="lhip" || name=="rhip" || (parent>=0 && model.LegMirrorBasis[parent].Leg);
            if(basis.Leg)correction[j]=mat3::inverse(mat3(reflection*restBones[basis.Source]*reflection))*mat3(restBones[j]);
            basis.Before=parent>=0?mat3::inverse(correction[parent]):mat3::identity();
            basis.After=correction[j];
        }
    }
    for(size_t j=0;j<pose.size();++j) {
        auto name=model.Joints[j].Name.ToString();
        // Fade the entire authored recovery together, including the chest.
        bool body=(authoredWholeBody && bodyBlend>0) || j==0 || model.Joints[j].Group==0;
        bool reflectBody=body && bodyBlend>0;
        bool left=name.size()>1 && name[0]=='l' && (reflectBody || !body);
        bool right=name.size()>1 && name[0]=='r' && (reflectBody || (!body && p->Dual.left));
        RuneJointPose target=source[j];bool paired=false;
        if(left || right) {
            auto otherName=name;otherName[0]=left?'r':'l';int other=Joint(&model,otherName);
            if(other>=0){target=Mirror(source[other]);paired=true;}
        }
        if(!paired && (reflectBody || (!body && p->Dual.left)) && !name.starts_with("attach") && !name.starts_with("attatch"))
            target=Mirror(source[j]);
        if(bodyBlend>0 && model.LegMirrorBasis[j].Leg) {
            const auto& basis=model.LegMirrorBasis[j];target=Mirror(source[basis.Source]);
            target.Position=basis.Before*target.Position;
            auto rotation=basis.Before*mat3(RunePoseRotation(target.Rotation))*basis.After;
            mat4 corrected=mat4::identity();
            for(int c=0;c<3;++c)for(int row=0;row<3;++row)corrected[c*4+row]=rotation[c*3+row];
            target.Rotation=RunePoseEuler(corrected);
        }
        pose[j]=body?BlendRunePose(source[j],target,bodyBlend):target;
    }
    // On a right-hand swing, trail the left arm by two authored frames.
    // Use the same sample for exact-frame collision and interpolated display.
    auto* clock=UObject::TryCast<UActor>(p->GetUObject("AnimProxy"));
    if(!clock)clock=p;
    if(!p->Dual.left && p->Dual.followAvailable) {
        for(size_t seq=0;seq<model.Sequences.size();++seq) {
            const auto& info=model.Sequences[seq].Info;
            if(info.Name!=clock->AnimSequence() || info.NumFrames<3)continue;
            float phase=sweepFrame>=0?float(sweepFrame)/info.NumFrames:clock->AnimFrame();
            auto delayed=model.SampleFrame(seq,std::max(0.f,phase-2.f/info.NumFrames),false);
            const auto delayedBones=model.JointMatrices(delayed);
            const auto currentBones=model.JointMatrices(pose);
            const auto mirror=mat4::scale(-1,1,1);
            for(auto leftName:{"lclav","lshoulda","lshouldb","lelbow","lwrist","lhand"}) {
                int left=Joint(&model,leftName);
                int right=Joint(&model,std::string("r")+std::string(leftName+1));
                if(left<0 || right<0)continue;
                pose[left]=Mirror(delayed[right]);
                const auto parent=model.Joints[left].Parent;
                if(parent>0 && !model.Joints[parent].Name.ToString().starts_with("l")) {
                    // Both arms share one animated torso. Rebase the trailing
                    // shoulder into that torso to keep the cut in front of the pawn.
                    auto local=InverseAffine(currentBones[parent])*mirror*delayedBones[right]*mirror;
                    for(int c=0;c<3;++c) {
                        float scale=length(vec3(local[c*4],local[c*4+1],local[c*4+2]));
                        if(scale>.001f)for(int r=0;r<3;++r)local[c*4+r]/=scale;
                    }
                    pose[left].Rotation=RunePoseEuler(local);
                }
            }
            break;
        }
    }
    const float visualGuard=p->NetworkReplica && p->RuneCombatPreviewGuard>=0?p->RuneCombatPreviewGuard:p->Dual.guardBlend;
    if(visualGuard>0) {
        auto guard=pose;
        auto initial=model.JointMatrices(guard);int chest=Joint(&model,"chest");
        if(chest<0)return;
        auto turn=[](vec3 from,vec3 to) {
            from=normalize(from);to=normalize(to);float d=std::clamp(dot(from,to),-1.f,1.f);
            if(d>.99999f)return mat4::identity();
            if(d<-.99999f){auto axis=cross(from,vec3(1,0,0));if(length(axis)<.01f)axis=cross(from,vec3(0,1,0));axis=normalize(axis);return mat4::quaternion(axis.x,axis.y,axis.z,0);}
            auto axis=cross(from,to);float inv=1.f/std::sqrt(2.f*(1.f+d));return mat4::quaternion(axis.x*inv,axis.y*inv,axis.z*inv,(1.f+d)*inv);
        };
        auto point=[](const mat4& m){return (m*vec4(0,0,0,1)).xyz();};
        auto rotateJoint=[&](int joint,const mat4& delta,const Array<mat4>& bones) {
            auto target=delta*bones[joint];auto position=point(bones[joint]);
            target[12]=position.x;target[13]=position.y;target[14]=position.z;
            auto local=InverseAffine(bones[model.Joints[joint].Parent])*target;
            for(int c=0;c<3;++c) {float scale=length(vec3(local[c*4],local[c*4+1],local[c*4+2]));if(scale>.001f)for(int r=0;r<3;++r)local[c*4+r]/=scale;}
            guard[joint].Rotation=RunePoseEuler(local);
        };
        // Two-bone arm solve: keep the authored shoulder/arm lengths, raise both
        // grips ahead of the chest and angle the actual weapons into an X.
        for(bool left:{false,true}) {
            int shoulder=Joint(&model,left?"lshouldb":"rshouldb"),elbow=Joint(&model,left?"lelbow":"relbow"),wrist=Joint(&model,left?"lwrist":"rwrist");
            if(shoulder<0 || elbow<0 || wrist<0)continue;
            float side=left?1.f:-1.f;
            auto bones=model.JointMatrices(guard);auto start=point(bones[shoulder]);
            vec3 target=(initial[chest]*vec4(side*6,11,-18,1)).xyz();
            vec3 pole=(initial[chest]*vec4(side*22,1,-7,1)).xyz()-start;
            float a=length(guard[elbow].Position),b=length(guard[wrist].Position);
            if(a<.01f || b<.01f || length(target-start)<.01f)continue;
            auto direction=normalize(target-start);float distance=std::clamp(length(target-start),std::abs(a-b)+.01f,a+b-.01f);
            target=start+direction*distance;pole-=direction*dot(pole,direction);
            if(length(pole)<.01f)continue;
            float cosine=std::clamp((a*a+distance*distance-b*b)/(2*a*distance),-1.f,1.f);
            auto bend=start+direction*(cosine*a)+normalize(pole)*(std::sqrt(1-cosine*cosine)*a);
            rotateJoint(shoulder,turn(point(bones[elbow])-start,bend-start),bones);
            bones=model.JointMatrices(guard);
            rotateJoint(elbow,turn(point(bones[wrist])-point(bones[elbow]),target-point(bones[elbow])),bones);
            bones=model.JointMatrices(guard);
            auto* weapon=left?p->Dual.offhand:p->Weapon();
            auto grip=Attachment(model,p,weapon,bones);
            auto current=(grip*vec4(weapon->GetVector("SweepVector"),0)).xyz();
            auto desired=(initial[chest]*vec4(-side*.52f,.85f,0,0)).xyz();
            if(length(current)>.01f)rotateJoint(wrist,turn(current,desired),bones);
        }
        for(size_t j=0;j<pose.size();++j)pose[j]=BlendRunePose(pose[j],guard[j],visualGuard);
    }
}
mat4 Attachment(const USkelModel& m,UActor* parent,UActor* child,const Array<mat4>& bones) {
    int j=child->GetByte("AttachParentJoint");
    // Derive the grip from the actual authored right-hand attachment. Preserve
    // the weapon's handedness; only the grip frame is reflected across the body.
    if(auto* p=UObject::TryCast<UPawn>(parent);p && child->IsA("Weapon") && j==Joint(&m,"lhand")) {
        int lw=Joint(&m,"lwrist"),rw=Joint(&m,"rwrist"),grip=Joint(&m,"attach_hand");
        if(lw>=0 && rw>=0 && grip>=0) {auto mirror=mat4::scale(-1,1,1);return bones[lw]*mirror*InverseAffine(bones[rw])*bones[grip]*mirror;}
    }
    return bones[j];
}
}
