#include "Precomp.h"
#include "RuneFinisher.h"
#include "RuneArmPose.h"
#include "RuneBattleLaws.h"
#include "RuneDualWield.h"
#include "RuneKick.h"
#include "RuneLeg.h"
#include "RuneSlam.h"
#include "Packages/Engine/Actors/Info/UGameInfo.h"
#include "RunePhysics.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Math/coords.h"
#include "Packages/Core/UClass.h"
#include "Packages/Core/UFunction.h"
#include "Packages/Engine/Actors/Inventory/UWeapon.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "VM/Frame.h"
#include "VM/ScriptCall.h"
#include "Utils/Logger.h"
#include "Utils/CommandLine.h"

namespace RuneFinisher {
namespace {
constexpr float PoseSeconds=.42f;
struct Contact {
    UWeapon* weapon;
    UPawn* attacker;
    UPawn* target;
    bool opening;
    bool armed=false;
    bool cut=false;
    UActor* corpse=nullptr;
};
thread_local Contact* active=nullptr;
thread_local UPawn* kickedVictim=nullptr;
thread_local const ReplayScope* replay=nullptr;
struct ContactScope {
    Contact* previous;
    explicit ContactScope(Contact& current):previous(active){active=&current;}
    ~ContactScope(){active=previous;}
};
bool handOff(Contact& contact,vec3 velocity){
    auto* t=contact.target;
    // Death scripts decide scoring, inventory drops and whether a body survives.
    if(!contact.corpse && !t->bDeleteMe() && !t->bHidden() && t->GetStateName()=="Dying" && t->GetUObject("CarcassType"))
        CallEvent(t,"ReplaceWithCarcass");
    return RunePhysics::FinishCorpse(contact.corpse,velocity);
}
bool live(UPawn* p){return p && p->Health()>0 && !p->bDeleteMe() && !p->bHidden();}
bool protectedTarget(UPawn* p){
    if(!live(p) || p->NetworkReplica || (!p->IsA("RunePlayer") && !p->IsA("ScriptPawn")))return true;
    if(p->GetStateName()=="Scripting" || (p->HasProperty("bIsBoss") && p->GetBool("bIsBoss")))return true;
    auto* player=UObject::TryCast<UPlayerPawn>(p);
    if(!player)return false;
    auto state=player->GetStateName();
    return player->ViewTarget() || (state!="PlayerWalking" && state!="PlayerSwimming" && state!="Uninterrupted" && state!="EdgeHanging");
}
vec3 forward(UPawn* p){return {std::cos(p->Rotation().YawRadians()),std::sin(p->Rotation().YawRadians()),0};}
bool held(UPawn* p,UWeapon* w){return live(p) && w && w->Owner()==p && (w==p->Weapon() || (w==p->Dual.offhand && RuneDualWield::Equipped(p)));}
}
KickContact::KickContact(UPawn* victim):previous(kickedVictim){kickedVictim=victim;}
KickContact::~KickContact(){kickedVictim=previous;}

void AimThrust(UPawn* attacker, UPawn* victim){
    if(!attacker || !victim)return;
    auto point=victim->Location()+vec3(0,0,victim->CollisionHeight()*.15f);
    auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(victim));
    int chest=model?RuneArmPose::Joint(*model,"chest"):-1;
    if(chest<0 && model)chest=RuneArmPose::Joint(*model,"spinec");
    if(chest>=0){
        auto bones=model->ActorJointMatrices(victim);
        point=RuneArmPose::JointPoint(GetRuneMeshToWorld(victim)*bones[chest]);
    }
    attacker->finisherAimWorld=point;
    attacker->finisherAimTime=engine->TotalTime;
}
Family Style(UActor* w){
    if(w->IsA("Sword"))return Family::Sword;
    if(w->IsA("Axe"))return Family::Axe;
    if(w->IsA("Hammer"))return Family::Hammer;
    if(w->IsA("Torch"))return Family::Torch;
    if(w->IsA("InvisibleWeapon"))return Family::Natural;
    return Family::Improvised;
}
bool CanFinish(UActor* weapon){
    return weapon && !weapon->IsA("Torch") && !weapon->IsA("LimbWeapon");
}
bool Opening(UPawn* a,UPawn* t){
    if(!RuneBattleLaws::Allowed(RuneBattleLaws::Finishers) || !live(a) || !live(t) || a==t)return false;
    auto delta=t->Location()-a->Location();delta.z=0;
    if(length(delta)<1)return false;
    auto dir=normalize(delta);
    if(dot(forward(a),dir)<.55f)return false;
    // A 90-degree rear opening, a shield-bash stun, or the brief recovery
    // of a committed swing that failed to hit. Merely standing idle is safe in front.
    return dot(forward(t),dir)>.7071f || t->RuneBashPhase==2 || t->finisherMissUntil>engine->TotalTime;
}
ReplayScope::ReplayScope(UActor* w,UPawn* t,bool o,bool d):weapon(w),target(t),opening(o),defended(d),previous(replay){replay=this;}
ReplayScope::~ReplayScope(){replay=previous;}
unsigned Hands(UPawn* a){
    if(!live(a) || !a->Weapon() || a->finisherPoseStart<0 ||
       (!a->finisherHeld && (a->finisherWithdraw>=0?engine->TotalTime-a->finisherWithdraw>=.16:engine->TotalTime-a->finisherPoseStart>=PoseSeconds)) || engine->TotalTime<a->finisherPoseStart)return 0;
    return a->finisherHands!=1 && !RuneDualWield::Equipped(a)?0:a->finisherHands;
}
unsigned PoseCode(UPawn* a){
    unsigned hands=a && a->finisherWithdraw<0?Hands(a):0;
    return hands && a->finisherCut?2:hands;
}
void PresentHands(UPawn* a,unsigned code,bool held){
    const bool cut=code==2;
    const unsigned hands=cut?(a->Dual.offhand?3:1):code;
    if(!hands && a->finisherHeld){
        a->finisherHeld=false;a->finisherWithdraw=engine->TotalTime;return;
    }
    if(!hands && a->finisherWithdraw>=0 && engine->TotalTime-a->finisherWithdraw<.16)return;
    if(a->finisherHands!=hands || a->finisherCut!=cut || (hands && a->finisherWithdraw>=0))
        a->finisherPoseStart=hands?engine->TotalTime:-1;
    a->finisherWithdraw=-1;
    a->finisherHeld=held;
    a->finisherHands=hands;
    a->finisherCut=cut;
}
bool Swipe(UFunction* fn,UObject* instance,const ArrayView<ExpressionValue>& args,ExpressionValue& out){
    static const NameString swipe="DoWeaponSwipe",touch="Touch",damage="DamageBodyPart";
    // The held input may keep polling Fire or an old swipe notification.
    // Only the input edge or the host action handler can draw the blade free.
    if(fn->Name=="Fire" || fn->Name=="Attack" || fn->Name=="StartAttack" || fn->Name==swipe){
        auto* actor=UObject::TryCast<UActor>(instance);
        auto* p=UObject::TryCast<UPawn>(actor);
        if(!p && actor)p=UObject::TryCast<UPawn>(actor->Owner());
        if(p && (p->finisherHeld || (p->finisherWithdraw>=0 && engine->TotalTime-p->finisherWithdraw<.16))){
            out=ExpressionValue::BoolValue(false);return true;
        }
    }
    if(fn->Name==damage && args.size()==6 && engine->LaunchInfo.IsRune() && RuneBattleLaws::Allowed(RuneBattleLaws::Finishers)){
        auto* t=UObject::TryCast<UPawn>(instance);
        if((kickedVictim && kickedVictim==t) || (active && active->target==t) || protectedTarget(t))return false;
        Contact natural{nullptr,UObject::TryCast<UPawn>(args[1].ToObject()),t,false};
        ContactScope scope(natural);
        out=Frame::Call(fn,instance,args);
        if(t->Health()<=0){
            auto impulse=args[3].ToVector()/std::max(1.f,t->Mass());
            float speed=length(impulse);
            if(!std::isfinite(speed))impulse=vec3(0);
            else if(speed>420.f)impulse*=420.f/speed;
            handOff(natural,impulse);
        }
        return true;
    }
    bool thrown=fn->Name==touch && args.size()==1;
    if((!thrown && (fn->Name!=swipe || args.size()!=6)) || !engine->LaunchInfo.IsRune() || !RuneBattleLaws::Allowed(RuneBattleLaws::Finishers))return false;
    auto* w=UObject::TryCast<UWeapon>(instance);
    if(!w || (thrown && w->GetStateName()!="Throw"))return false;
    auto* struck=UObject::TryCast<UActor>(args[0].ToObject());
    auto* t=UObject::TryCast<UPawn>(struck);
    auto* a=UObject::TryCast<UPawn>(w->Owner());
    if(!thrown && held(a,w) && struck && struck!=a && w->GetStateName()=="Swinging" &&
        (t || ((struck->IsA("Shield") || struck->IsA("Weapon")) && struck->Owner()!=a)))
        a->finisherContact=engine->TotalTime;
    if(w && t && commandline->HasArg("","--finisher-trace")){
        auto* owner=UObject::TryCast<UPawn>(w->Owner());auto* player=UObject::TryCast<UPlayerPawn>(t);
        LogMessage("FIN_TRACE weapon="+w->Name.ToString()+" source="+(owner?owner->Name.ToString():"none")+" target="+t->Name.ToString()+" state="+t->GetStateName().ToString()+" protected="+std::to_string(protectedTarget(t))+" camera="+std::to_string(player && player->ViewTarget())+" opening="+std::to_string(Opening(owner,t))+" held="+std::to_string(held(owner,w))+" weaponstate="+w->GetStateName().ToString());
    }
    if(!CanFinish(w) || (kickedVictim && kickedVictim==t) || protectedTarget(t) || (active && active->weapon==w && active->target==t))return false;
    const bool recorded=replay && replay->weapon==w && replay->target==t;
    if(!live(a) || a==t || a->NetworkReplica || w->NetworkReplica)return false;
    if(!thrown && (!held(a,w) || (!recorded && w->GetStateName()!="Swinging")))return false;
    if((recorded && replay->defended) || RuneDualWield::Blocks(t,a) || (!recorded && RuneKick::ShieldBlocks(t,a)))return false;
    vec3 impact=forward(a);
    bool opening=recorded?replay->opening:Opening(a,t);
    if(thrown){
        vec3 flight=w->Velocity();flight.z=0;
        if(length(flight)<1.f)return false;
        impact=normalize(flight);
        opening=dot(forward(t),impact)>.7071f || t->RuneBashPhase==2 || t->finisherMissUntil>engine->TotalTime;
    }
    Contact contact{w,a,t,opening};
    ContactScope scope(contact);
    out=Frame::Call(fn,instance,args);
    if(!contact.armed || t->Health()>0)return true;
    const bool dual=RuneDualWield::Equipped(a);
    if(!thrown){
        a->finisherHands=dual?3:1;
        a->finisherCut=contact.cut;
        a->finisherWithdraw=-1;
        a->finisherPoseStart=engine->TotalTime;
    }
    auto family=Style(w);
    const float impulse=family==Family::Hammer?245.f:family==Family::Axe?175.f:family==Family::Torch?85.f:family==Family::Natural?145.f:115.f;
    const bool thrust=!thrown && family==Family::Sword && !contact.cut;
    // Let gravity take the body immediately. A lateral launch pulls the torso
    // clear of the point before the attacking arm can follow through.
    const bool physical=handOff(contact,thrust?vec3(0):impact*impulse+vec3(0,0,family==Family::Hammer?105.f:25.f));
    if(thrust && physical)RunePhysics::PinImpale(a,contact.corpse);
    LogMessage("RUNE_FINISH weapon="+w->Class->Name.ToString()+" hands="+std::to_string(a->finisherHands)+" ragdoll="+std::to_string(physical));
    return true;
}
void AfterCall(UFunction* fn,UObject* obj,const ArrayView<ExpressionValue>& args,ExpressionValue& result){
    static const NameString start="StartAttack",end="FinishAttack",pass="LimbPassThrough",init="InitFor";
    if(fn->Name==start || fn->Name==end){
        auto* w=UObject::TryCast<UWeapon>(obj);
        auto* p=w?UObject::TryCast<UPawn>(w->Owner()):nullptr;
        if(!engine->LaunchInfo.IsRune() || !held(p,w))return;
        if(fn->Name==start && w->GetStateName()=="Swinging"){
            if(w==p->Weapon() || p->Weapon()->GetStateName()!="Swinging")p->finisherSwingStart=engine->TotalTime;
            p->finisherMissUntil=-1;
        } else if(fn->Name==end && p->finisherSwingStart>=0){
            if(p->finisherContact<p->finisherSwingStart && engine->TotalTime-p->finisherSwingStart>.12)
                p->finisherMissUntil=engine->TotalTime+.24;
            p->finisherSwingStart=-1;
        }
        return;
    }
    if(!active)return;
    if(fn->Name==init && args.size() && args[0].ToObject()==active->target && obj->IsA("Carcass")){
        active->corpse=UObject::TryCast<UActor>(obj);
        return;
    }
    if(!active->weapon || fn->Name!=pass || obj!=active->target || args.size()!=3 || !live(active->target))return;
    int admitted=result.ToInt();
    if(admitted<=0)return;
    active->attacker->finisherContact=engine->TotalTime;
    auto* victim=active->target;
    if(!active->opening && admitted<victim->Health())return;
    active->armed=true;
    // This value has passed the game's actual damage reductions. Preserve overkill
    // where it was already lethal, and let DamageBodyPart own death and gore.
    result=ExpressionValue::IntValue(std::max(admitted,victim->Health()));
    const int part=args[0].ToInt(),sever=args[2].ToInt();
    auto style=Style(active->weapon);
    if(style==Family::Sword && active->weapon->GetStateName()!="Throw" && !(part>=1 && part<=9 && sever>0))
        AimThrust(active->attacker,victim);
    if((style==Family::Axe || style==Family::Sword) && part>=1 && part<=9 && sever>0){
        active->cut=true;
        if(!CallEvent(victim,"BodyPartSeverable",{ExpressionValue::IntValue(part)}).ToBool()){
            if(part>=6 && part<=9 && !engine->packages->FindClass("Engine.GameInfo")->GetDefaultObject<UGameInfo>()->GetBool("bLowGore"))
                RuneLeg::SeverForFinisher(victim,part>=8?1:0);
            return;
        }
        auto* parts=static_cast<int*>(victim->GetProperty("BodyPartHealth"));
        if(parts[part]>0)parts[part]=std::min(parts[part],sever);
    }
}
void ApplyPose(const USkelModel& m,UActor* actor,Array<RuneJointPose>& pose){
    using namespace RuneArmPose;
    auto* p=UObject::TryCast<UPawn>(actor);
    if(!p || !p->finisherHands || !Hands(p))return;
    int chest=Joint(m,"chest");
    if(chest<0)chest=Joint(m,"spinec");
    if(chest<0)return;
    float age=float(engine->TotalTime-p->finisherPoseStart);
    float weight=Smooth((age+.025f)/.09f)*(1.f-Smooth((age-.22f)/.20f));
    auto original=pose;
    auto meshToActor=Coords::Rotation(m.RotationOffset).ToMatrix();
    auto toMesh=InverseAffine(meshToActor);
    auto offset=[&](vec3 v){return (toMesh*vec4(v,0)).xyz();};
    bool supporting=p->finisherHeld || p->finisherWithdraw>=0;
    bool driving=!p->finisherCut && (Style(p->Weapon())==Family::Sword ||
        (supporting && p->Dual.offhand && Style(p->Dual.offhand)==Family::Sword));
    vec3 focus{};
    if(driving){
        auto bones=m.JointMatrices(pose);
        auto center=JointPoint(bones[chest]);
        focus=center+offset(vec3(45,0,0));
        if(std::abs(p->finisherPoseStart-p->finisherAimTime)<.24){
            auto local=InverseAffine(GetRuneMeshToWorld(p))*vec4(p->finisherAimWorld,1);
            if(length(local.xyz()-center)<95.f)focus=local.xyz();
        }
        // Use a short forward step and lean from the waist. The capsule, blade
        // length and original arm lengths are unchanged.
        auto front=offset(vec3(1,0,0));
        // Plant the trailing foot through the short step.
        int leftFoot=Joint(m,"lankle"),rightFoot=Joint(m,"rankle");
        bool plantLeft=leftFoot>0 && rightFoot>0 &&
            dot(JointPoint(bones[leftFoot])-JointPoint(bones[rightFoot]),front)<0;
        int foot=plantLeft?leftFoot:rightFoot;
        int shin=Joint(m,plantLeft?"lknee":"rknee"),hip=Joint(m,plantLeft?"lhip":"rhip");
        pose[m.RootJoint].Position+=front*(p->finisherHands==3?19.f:14.f);
        if(foot>0 && shin>0 && hip>0){
            auto planted=bones[foot];
            bones=m.JointMatrices(pose);
            auto start=JointPoint(bones[hip]),bend=JointPoint(bones[shin]);
            auto solution=RuneIK::TwoBone(start,bend,JointPoint(bones[foot]),JointPoint(planted),bend+front*10.f);
            if(solution.valid){
                Rotate(m,pose,hip,Turn(bend-start,solution.joint-start)*bones[hip],bones);
                bones=m.JointMatrices(pose);
                Rotate(m,pose,shin,Turn(JointPoint(bones[foot])-solution.joint,solution.end-solution.joint)*bones[shin],bones);
                bones=m.JointMatrices(pose);
                Rotate(m,pose,foot,planted,bones);
            }
        }
        int spine=Joint(m,"torso");
        if(spine>0 && spine!=chest){
            bones=m.JointMatrices(pose);
            auto up=offset(vec3(0,0,1));
            auto turn=Turn(up,up+front*.20f);
            int head=Joint(m,"head");
            if(supporting && head>0){
                // The ordinary combat idle crouches over a low guard. Brace
                // upright under the load so raised wrists clear the face.
                auto posture=JointPoint(bones[head])-JointPoint(bones[spine]);
                turn=Turn(posture,up+front*.22f);
            }
            Rotate(m,pose,spine,turn*bones[spine],bones);
        }
        weight=Smooth((age+.014f)/.055f)*(1.f-Smooth((age-.18f)/.24f));
        if(p->finisherHeld || p->finisherWithdraw>=0){
            weight=Smooth((age+.014f)/.065f);
            if(p->finisherWithdraw>=0)weight*=1.f-Smooth(float(engine->TotalTime-p->finisherWithdraw)/.16f);
            focus+=offset(vec3(0,0,-3.f));
        }else focus+=offset(vec3(0,0,-3.f-std::min(10.f,320.f*age*age)));
    }
    for(bool left:{false,true}){
        if(!(p->finisherHands&(left?2:1)))continue;
        auto* w=left?p->Dual.offhand:p->Weapon();
        if(!w || Style(w)==Family::Natural)continue;
        int wrist=Joint(m,left?"lwrist":"rwrist");
        if(wrist<1)continue;
        auto bones=m.JointMatrices(pose);
        float side=left?-1.f:1.f;
        auto family=Style(w);
        if(supporting && family!=Family::Sword)continue;
        bool thrust=family==Family::Sword && !p->finisherCut;
        vec3 direction=thrust?vec3(1,-side*.08f,0):(family==Family::Axe || family==Family::Sword)?vec3(.5f,-side*.75f,-.45f):vec3(.55f,-side*.20f,-.82f);
        auto grip=RuneDualWield::Attachment(m,p,w,bones);
        vec3 blade=(grip*vec4(w->GetVector("SweepVector"),0)).xyz();
        auto center=JointPoint(bones[chest]);
        auto wristTarget=center+offset(vec3(thrust?31.f:25.f,side*(thrust?8.f:13.f),thrust?1.f:-8.f));
        vec3 bladeDirection=offset(direction);
        int shoulder=Joint(m,left?"lshouldb":"rshouldb");
        if(shoulder<0)shoulder=Joint(m,left?"lshould":"rshould");
        if(driving && thrust){
            auto through=focus+offset(vec3(0,p->finisherHands==3?side*4.f:0.f,p->finisherHands==3?side*2.f:0.f));
            wristTarget=through+offset(vec3(-12.f,side*2.f,0));
            auto elbow=Joint(m,left?"lelbow":"relbow");
            if(shoulder>0 && elbow>0){
                auto reach=RuneIK::TwoBone(JointPoint(bones[shoulder]),JointPoint(bones[elbow]),JointPoint(bones[wrist]),wristTarget,center+offset(vec3(0,side*30.f,-9.f)));
                if(reach.valid)bladeDirection=through-reach.end;
            }
        }
        auto aligned=Turn(blade,bladeDirection)*bones[wrist];
        Arm(m,pose,left,At(aligned,wristTarget),
            center+offset(vec3(0,side*30.f,-9.f)),shoulder);
        // Keep the original second-hand grip on large weapons as the lead wrist moves.
        if(!left && p->finisherHands==1 && RuneSlam::Heavy(w)){
            int support=Joint(m,"lwrist");
            if(support>0){
                auto next=m.JointMatrices(pose);
                auto goal=next[wrist]*InverseAffine(bones[wrist])*bones[support];
                Arm(m,pose,true,goal,center+offset(vec3(0,-30.f,-9.f)));
            }
        }
    }
    for(size_t j=0;j<pose.size();++j)pose[j]=BlendRunePose(original[j],pose[j],weight);
}
}
