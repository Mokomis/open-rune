#include "Precomp.h"
#include "RuneShieldTaunt.h"
#include "Animation/RuneIKMath.h"
#include "RunePhysics.h"
#include "RuneDualWield.h"
#include "Engine.h"
#include "GameWindow.h"
#include "Package/PackageManager.h"
#include "Package/Package.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Inventory/UWeapon.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UGameInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "Packages/Engine/Subsystems/USurrealAudioDevice.h"
#include "VM/ScriptCall.h"
#include "VM/Frame.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include <iostream>
#include <fstream>
#include <set>
#ifdef OPENRUNE_ENABLE_NETWORK
#include "RuneNetwork.h"
#endif
namespace RuneShieldTaunt {
namespace {
mat4 InverseAffine(const mat4& m){
    auto inv=mat3::inverse(mat3(m));auto out=mat4::identity();
    for(int c=0;c<3;++c)for(int r=0;r<3;++r)out[c*4+r]=inv[c*3+r];
    auto t=inv*vec3(-m[12],-m[13],-m[14]);out[12]=t.x;out[13]=t.y;out[14]=t.z;return out;
}
int Joint(const USkelModel& m,const char* name){for(size_t i=0;i<m.Joints.size();++i)if(m.Joints[i].Name==name)return (int)i;return -1;}
int Sequence(const USkelModel& m,const NameString& name){for(size_t i=0;i<m.Sequences.size();++i)if(m.Sequences[i].Info.Name==name)return (int)i;return -1;}
float Smooth(float t){t=std::clamp(t,0.f,1.f);return t*t*(3-2*t);}
vec3 JointPoint(const mat4& m){return {m[12],m[13],m[14]};}
mat4 At(mat4 m,vec3 p){m[12]=p.x;m[13]=p.y;m[14]=p.z;return m;}
mat4 Turn(vec3 a,vec3 b){
    if(length(a)<.001f||length(b)<.001f)return mat4::identity();
    a=normalize(a);b=normalize(b);float d=std::clamp(dot(a,b),-1.f,1.f);
    if(d>.99999f)return mat4::identity();
    if(d<-.99999f){auto axis=cross(a,vec3(1,0,0));if(length(axis)<.01f)axis=cross(a,vec3(0,1,0));axis=normalize(axis);return mat4::quaternion(axis.x,axis.y,axis.z,0);}
    auto axis=cross(a,b);float k=1.f/std::sqrt(2*(1+d));return mat4::quaternion(axis.x*k,axis.y*k,axis.z*k,(1+d)*k);
}
RuneJointPose Local(const mat4& m){
    RuneJointPose p;p.Position=JointPoint(m);mat4 rotation=m;
    for(int c=0;c<3;++c){p.Scale[c]=length(vec3(m[c*4],m[c*4+1],m[c*4+2]));if(p.Scale[c]>.001f)for(int r=0;r<3;++r)rotation[c*4+r]/=p.Scale[c];}
    p.Rotation=RunePoseEuler(rotation);return p;
}
void Rotate(const USkelModel& m,Array<RuneJointPose>& pose,int j,const mat4& worldRotation,const Array<mat4>& bones){
    auto desired=At(worldRotation,JointPoint(bones[j]));
    auto local=Local(InverseAffine(bones[m.Joints[j].Parent])*desired);
    pose[j].Rotation=local.Rotation; // retain actual bone lengths and scales
}
void Arm(const USkelModel& m,Array<RuneJointPose>& pose,bool left,const mat4& wristTarget,vec3 pole){
    int shoulder=Joint(m,left?"lshouldb":"rshouldb"),elbow=Joint(m,left?"lelbow":"relbow"),wrist=Joint(m,left?"lwrist":"rwrist");
    if(shoulder<1 || elbow<1 || wrist<1)return;
    auto bones=m.JointMatrices(pose);auto start=JointPoint(bones[shoulder]),target=JointPoint(wristTarget);
    auto solution=RuneIK::TwoBone(start,JointPoint(bones[elbow]),JointPoint(bones[wrist]),target,pole);
    if(!solution.valid)return;
    target=solution.end;auto bend=solution.joint;
    Rotate(m,pose,shoulder,Turn(JointPoint(bones[elbow])-start,bend-start)*bones[shoulder],bones);
    bones=m.JointMatrices(pose);Rotate(m,pose,elbow,Turn(JointPoint(bones[wrist])-JointPoint(bones[elbow]),target-JointPoint(bones[elbow]))*bones[elbow],bones);
    bones=m.JointMatrices(pose);Rotate(m,pose,wrist,wristTarget,bones);
}

bool Alive(UPawn* p){return p && !p->bDeleteMe() && !p->bHidden() && p->Health()>0;}
bool AI(UPawn* p){return p && (p->ShieldTaunt.bot || (!p->bIsPlayer()&&!UObject::TryCast<UPlayerPawn>(p)&&p->IsA("ScriptPawn")));}
bool Skeletal(UPawn* p){
    for(auto name:{"Zombie","Zombie2","DarkWarrior","SarkSpawn"})if(p->IsA(name))return true;
    // Multiplayer bots are RunePlayer subclasses; recognize the actual visual.
    auto* model=GetRuneDisplayMesh(p);auto variant=p->GetByte("SkelMesh");
    for(auto name:{"PlayerZombie","PlayerZombie2","PlayerDarkWarrior","PlayerSarkSpawn"}){
        auto* d=engine->packages->GetPackage("RuneI")->GetClass(name)->GetDefaultObject<UPawn>();
        if(GetRuneDisplayMesh(d)==model && d->GetByte("SkelMesh")==variant)return true;
    }
    return false;
}
UActor* Shield(UPawn* p){
    auto* s=UObject::TryCast<UActor>(p->GetUObject("Shield"));
    return s && !s->bDeleteMe()&&!s->bHidden()&&s->Owner()==p&&s->GetInt("Health")>0&&s->GetUObject("AttachParent")==p?s:nullptr;
}
bool Armed(UPawn* p){
    auto* w=p->Weapon();return w&&!w->bDeleteMe()&&!w->bHidden()&&w->Owner()==p&&w->GetUObject("AttachParent")==p&&w->GetByte("MeleeType")==0&&Shield(p)&&!RuneDualWield::Equipped(p);
}
bool Free(UPawn* p){
    if(p->Physics()!=PHYS_Walking || RunePhysics::Controls(p)||RuneCorpse::Busy(p)||RuneBloodlust::Busy(p)||
       RuneSpider::Busy(p)||p->RuneKickActive||p->RuneBashPhase||p->Slam.phase||p->Dual.guard||p->Tactics.attacking||p->Tactics.blocking||p->GetBool("bSwingingHigh")||p->GetBool("bSwingingLow"))return false;
    if(p->HasProperty("bShieldActive")&&p->GetBool("bShieldActive"))return false;
    auto state=p->GetStateName();return state=="PlayerWalking"||state=="Fighting";
}
float Ready(float t){return Smooth(t/.3f)*(1-Smooth((t-1.8f)/.4f));}
Array<RuneJointPose> Pose(const USkelModel& m,int idle,int guard,float seconds){
    auto rest=m.DecodeFrame(idle,0),pose=rest;auto ref=m.JointMatrices(rest);
    auto bones=m.JointMatrices(pose);int chest=Joint(m,"chest"),lw=Joint(m,"lwrist"),rw=Joint(m,"rwrist"),hand=Joint(m,"attach_hand"),shield=Joint(m,"attach_shielda");
    if(shield<0)return rest;
    auto meshToActor=Coords::Rotation(m.RotationOffset).ToMatrix(),actorToMesh=InverseAffine(meshToActor);
    auto offset=[&](vec3 v){return (actorToMesh*vec4(v,0)).xyz();};auto center=JointPoint(bones[chest]);
    auto facing=mat4::identity();facing[0]=0;facing[1]=1;facing[2]=0;facing[4]=0;facing[5]=0;facing[6]=1;facing[8]=1;facing[9]=0;facing[10]=0;
    // The authored boss protrudes along negative local Z; that face must point outward.
    auto shieldTarget=At(actorToMesh*facing*mat4::rotate(3.141592653589793f,0,1,0),center+offset(vec3(17,-8,1)));
    auto leftTarget=shieldTarget*InverseAffine(InverseAffine(ref[lw])*ref[shield]);
    Arm(m,pose,true,leftTarget,center+offset(vec3(3,-28,-12)));
    // Rune's equipment frames can be separate rig branches, not wrist children.
    // Drive the complete attachment frame from the solved hand, not just its rotation.
    bones=m.JointMatrices(pose);auto heldShield=bones[lw]*(InverseAffine(ref[lw])*ref[shield]);
    pose[shield]=Local(InverseAffine(bones[m.Joints[shield].Parent])*heldShield);
    float strike=0;
    for(float beat:Beats){float d=seconds-beat;if(d>=-.22f&&d<0)strike=Smooth((d+.22f)/.22f);else if(d>=0&&d<.24f)strike=1-Smooth(d/.24f);}
    // Wind back above/right; strike diagonally against the raised shield.
    // Wrist and weapon attachment use the same rigid target; bone lengths never change.
    // The blade runs along attachment Y and its broadside normal is X.
    // Keep that broadside parallel to the shield face, rather than driving the tip through it.
    vec3 normal(1,0,0),blade=normalize(mix(vec3(0,.8f,.6f),vec3(0,-1,0),strike));auto side=cross(normal,blade);
    auto swordFacing=mat4::identity();for(int k=0;k<3;++k){swordFacing[k]=normal[k];swordFacing[4+k]=blade[k];swordFacing[8+k]=side[k];}
    bones=m.JointMatrices(pose);auto actualShield=JointPoint(bones[shield]);
    auto contact=actualShield+offset(vec3(6.67f,17,0));
    auto swordTarget=At(actorToMesh*swordFacing,mix(center+offset(vec3(30,25,13)),contact,strike));
    auto rightTarget=swordTarget*InverseAffine(InverseAffine(ref[rw])*ref[hand]);
    Arm(m,pose,false,rightTarget,center+offset(vec3(4,28,3)));
    bones=m.JointMatrices(pose);auto heldSword=bones[rw]*(InverseAffine(ref[rw])*ref[hand]);
    pose[hand]=Local(InverseAffine(bones[m.Joints[hand].Parent])*heldSword);
    float raised=Ready(seconds);for(size_t j=0;j<pose.size();++j)pose[j]=BlendRunePose(rest[j],pose[j],raised);
    // Blend the body first, then derive equipment from the resulting hands.
    // Independently blending attachment branches creates gaps during raise/lower.
    bones=m.JointMatrices(pose);pose[shield]=Local(InverseAffine(bones[m.Joints[shield].Parent])*bones[lw]*(InverseAffine(ref[lw])*ref[shield]));
    bones=m.JointMatrices(pose);pose[hand]=Local(InverseAffine(bones[m.Joints[hand].Parent])*bones[rw]*(InverseAffine(ref[rw])*ref[hand]));
    return pose;
}
void Resume(UPawn* p){
    // Keep the original AI state and physical response. A hit/death/new animation wins.
    auto* upper=UObject::TryCast<UActor>(p->GetUObject("AnimProxy"));
    if(Alive(p)&&p->AnimSequence()==Clip&&(!upper||upper->AnimSequence()==Clip)&&!RunePhysics::Controls(p)){
        p->LoopAnim("weapon1_idle",1,.12f,0);
        if(auto* proxy=UObject::TryCast<UActor>(p->GetUObject("AnimProxy"))){proxy->GotoState("Idle","Begin");proxy->LoopAnim("weapon1_idle",1,.12f,0);}
    }
}
}
void Install(USkelModel& m){
    if(Sequence(m,Clip)>=0)return;
    for(auto name:{"chest","lshouldb","lelbow","lwrist","rshouldb","relbow","rwrist","attach_hand","attach_shielda"})if(Joint(m,name)<1)return;
    int idle=Sequence(m,"weapon1_idle");if(idle<0)idle=Sequence(m,"neutral_idle");int guard=Sequence(m,"H3_DefendAttack");if(idle<0||guard<0)return;
    RuneSequence seq;seq.Info.Name=Clip;seq.Info.Group="Taunt";seq.Info.StartFrame=int(m.Frames.size());seq.Info.NumFrames=67;seq.Info.Rate=30;
    auto shortValue=[&](int n){seq.Data.push_back(uint8_t(n));seq.Data.push_back(uint8_t(n>>8));};
    auto append=[&](const RuneJointPose& p){seq.Data.push_back(255);for(int axis=0;axis<3;++axis)shortValue(int(std::lround(p.Position[axis]*256)));shortValue(int(std::lround(p.Scale.x*256)));shortValue(int(std::lround(p.Scale.z*256)));shortValue(int(std::lround(p.Scale.y*256)));shortValue(p.Rotation.Pitch);shortValue(p.Rotation.Yaw);shortValue(p.Rotation.Roll);};
    for(int f=0;f<seq.Info.NumFrames;++f){auto pose=Pose(m,idle,guard,f/30.f);for(auto& p:pose)append(p);
        RuneAnimFrame frame;frame.Sequence=int16_t(m.Sequences.size());frame.Event="None";frame.Bounds=m.BoundingBox;frame.PreprocessJoints=pose;m.Frames.push_back(std::move(frame));}
    m.AnimSeqs.push_back(seq.Info);m.Sequences.push_back(std::move(seq));
}
bool Eligible(UPawn* p){return engine&&engine->LaunchInfo.IsRune()&&Alive(p)&&!p->NetworkReplica&&p->Role()==ROLE_Authority&&AI(p)&&Skeletal(p)&&Armed(p);}
bool Start(UPawn* p){
    if(!Eligible(p)||!Free(p)||p->ShieldTaunt.active||(!p->bAnimLoop()&&!p->bAnimFinished()&&p->AnimRate()>0))return false;
    auto* m=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(p));if(!m||Sequence(*m,Clip)<0)return false;
    auto& s=p->ShieldTaunt;s.active=true;s.health=p->Health();s.sounded=0;++s.started;s.cooldown=20.f+unsigned(p->Index)%11;
    p->Acceleration()=vec3(0);p->Velocity().x=p->Velocity().y=0;
    CallEvent(p,"ActivateShield",{ExpressionValue::BoolValue(false)});
    CallEvent(p->Weapon(),"FinishAttack");CallEvent(p->Weapon(),"DisableSwipeTrail");
    p->PlayAnim(Clip,1,.12f);
    if(auto* proxy=UObject::TryCast<UActor>(p->GetUObject("AnimProxy")))proxy->PlayAnim(Clip,1,.12f);
    if(p->AnimSequence()!=Clip){s.active=false;return false;}
    LogMessage("RUNE_SHIELD_TAUNT_START actor="+p->Name.ToString());return true;
}
bool Controls(UActor* actor){
    auto* p=UObject::TryCast<UPawn>(actor);if(!p&&actor&&actor->IsA("AnimationProxy"))p=UObject::TryCast<UPawn>(actor->Owner());
    return Alive(p)&&p->ShieldTaunt.active&&p->AnimSequence()==Clip&&!RunePhysics::Controls(p);
}
void Tick(UPawn* p,float dt){
    if(!engine||!engine->LaunchInfo.IsRune()||p->NetworkReplica)return;
    auto& s=p->ShieldTaunt;s.cooldown=std::max(0.f,s.cooldown-std::clamp(dt,0.f,.25f));
    if(s.active){
        auto* proxy=UObject::TryCast<UActor>(p->GetUObject("AnimProxy"));
        if((proxy&&proxy->AnimSequence()!=Clip)||p->GetBool("bSwingingHigh")||p->GetBool("bSwingingLow")||!Eligible(p)||!Armed(p)||p->Health()!=s.health||p->Physics()!=PHYS_Walking||RunePhysics::Controls(p)||p->RuneKickActive||p->RuneBashPhase||p->AnimSequence()!=Clip||p->bAnimFinished()){
            if(commandline->HasArg("","--rune-shield-taunt-probe"))std::cout<<"TAUNT_CANCEL eligible="<<Eligible(p)<<" health="<<p->Health()<<" expected="<<s.health<<" physics="<<int(p->Physics())<<" finished="<<bool(p->bAnimFinished())<<" body="<<p->AnimSequence().ToString()<<" proxy="<<(proxy?proxy->AnimSequence().ToString():"none")<<" kick="<<p->RuneKickActive<<" bash="<<p->RuneBashPhase<<std::endl;
            s.active=false;Resume(p);return;
        }
        p->Acceleration()=vec3(0);p->Velocity().x=p->Velocity().y=0;return;
    }
    if(s.cooldown>0 || !Eligible(p)||!Free(p))return;
    // Check occasionally and only against a visible opponent outside immediate
    // melee range. Friendly pawns, scripted scenes and unarmed idle loops do not provoke it.
    s.cooldown=.6f;UPawn* target=nullptr;float best=800;
    bool teams=engine->GameInfo && engine->GameInfo->HasProperty("bTeamGame")&&engine->GameInfo->GetBool("bTeamGame");
    auto* own=p->GetUObject("PlayerReplicationInfo");
    for(auto* q=engine->LevelInfo->PawnList();q;q=q->nextPawn()){
        if(q==p||!Alive(q))continue;if(!s.bot&&q!=p->Enemy())continue;
        auto* info=q->GetUObject("PlayerReplicationInfo");if(teams&&own&&info&&own->GetByte("Team")==info->GetByte("Team"))continue;
        float d=length(q->Location()-p->Location());if(d<240||d>=best)continue;
        auto direction=q->Location()-p->Location();auto yaw=p->Rotation().YawRadians();if(dot(normalize(direction),vec3(std::cos(yaw),std::sin(yaw),0))<.25f||!p->LineOfSightTo(q,false))continue;
        target=q;best=d;
    }
    if(target)Start(p);
}
bool AnimationFrames(UActor* actor,float from,float to){
    if(actor->AnimSequence()!=Clip)return false;
    auto* p=UObject::TryCast<UPawn>(actor);if(!p || !p->ShieldTaunt.active||!Eligible(p))return true;
    auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(p));int seq=model?Sequence(*model,Clip):-1;if(seq<0)return true;
    for(unsigned beat=0;beat<3;++beat){float phase=Beats[beat]*30/model->Sequences[seq].Info.NumFrames;
        if(from<phase&&to>=phase && !(p->ShieldTaunt.sounded&(1u<<beat))){
            p->ShieldTaunt.sounded|=1u<<beat;auto* sound=p->Weapon()->GetUObject("HitShield");
            if(!sound)sound=engine->packages->GetPackage("RuneI")->GetClass("VikingBroadSword")->GetDefaultObject<UObject>()->GetUObject("HitShield");
            if(sound)CallEvent(Shield(p),"PlaySound",{ExpressionValue::ObjectValue(sound),ExpressionValue::ByteValue(SLOT_Interact),ExpressionValue::FloatValue(.9f),ExpressionValue::BoolValue(false),ExpressionValue::FloatValue(950.f),ExpressionValue::FloatValue(.97f+beat*.03f)});
            LogMessage("RUNE_SHIELD_TAUNT_CLANG beat="+std::to_string(beat+1));
        }
    }
    return true;
}
}

namespace RuneShieldTaunt {
namespace {
void Grant(Engine& game,UPawn* p,const char* cls){
    auto* item=p->Spawn(game.packages->GetPackage("RuneI")->GetClass(cls),{},{},p->Location()+vec3(0,0,100),{});
    if(!item)throw std::runtime_error("Taunt fixture inventory spawn");
    auto* copy=CallEvent(item,"SpawnCopy",{ExpressionValue::ObjectValue(p)}).ToObject();
    CallEvent(p,"AcquireInventory",{ExpressionValue::ObjectValue(copy)});copy->GotoState("Idle","Begin");
}
void Equip(Engine& game,UPawn* p){
    if(p->Weapon()){p->Weapon()->Destroy();p->Weapon()=nullptr;}
    p->Dual.offhand=nullptr;
    Grant(game,p,"VikingShortSword");if(!Shield(p))Grant(game,p,"VikingShield");
    p->GotoState("PlayerWalking","Begin");p->Velocity()=p->Acceleration()=vec3(0);p->SetPhysics(PHYS_Walking);
    p->SetBool("bSwingingHigh",false);p->SetBool("bSwingingLow",false);
    if(auto* proxy=UObject::TryCast<UActor>(p->GetUObject("AnimProxy")))proxy->GotoState("Idle","Begin");
    p->LoopAnim("weapon1_idle",1,0,0);
    vec3 floor,normal;auto at=p->Location();
    if(p->Trace(floor,normal,at-vec3(0,0,350),at,false,vec3(p->CollisionRadius()*.5f,p->CollisionRadius()*.5f,0))&&normal.z>.7f)p->SetLocation(floor+vec3(0,0,p->CollisionHeight()+.25f));
}
}
void Probe(Engine& game){
    if(!commandline->HasArg("","--save-directory"))throw std::runtime_error("Taunt probe requires isolated saves");
    auto check=[](bool b,const char* why){if(!b)throw std::runtime_error(std::string("shield taunt: ")+why);};
    auto url=game.GetDefaultURL("DM-Hildir.run");url.AddOrReplaceOption("game=RuneI.RuneMultiPlayer");game.NetworkAuthority=true;game.LoadMap(url);
    unsigned preserved=0,samples=0;
    for(int modelID:{5,6,18,20}) {
        auto* p=game.LoginNetworkPlayer("Taunt test",-1,modelID,0);Equip(game,p);check(!Eligible(p)&&!Start(p),"human model automatically taunted");p->ShieldTaunt.bot=true;check(Eligible(p),"equipped skeletal bot excluded");
        auto* sword=p->Weapon();p->Weapon()=nullptr;check(!Eligible(p)&&!Start(p),"unarmed taunt");p->Weapon()=sword;
        auto* shield=p->GetUObject("Shield");p->SetObject("Shield",nullptr);check(!Eligible(p)&&!Start(p),"shieldless taunt");p->SetObject("Shield",shield);
        auto* m=UObject::Cast<USkelModel>(GetRuneDisplayMesh(p));int seq=Sequence(*m,Clip);check(seq>=0,"clip missing");m->ValidateAnimations();auto base=m->DecodeFrame(seq,0);
        for(int f=0;f<m->Sequences[seq].Info.NumFrames;++f){auto pose=m->DecodeFrame(seq,f);for(size_t j=0;j<pose.size();++j){
            check((m->Joints[j].Name=="attach_hand"||m->Joints[j].Name=="attach_shielda"||length(base[j].Position-pose[j].Position)<.004f) && length(base[j].Scale-pose[j].Scale)<.004f,"stretched anatomical joints");++preserved;
            auto bones=m->JointMatrices(pose);for(auto& b:bones)for(int k=0;k<16;++k)check(std::isfinite(b[k]),"invalid pose");}
            auto restBones=m->JointMatrices(base),bones=m->JointMatrices(pose);
            for(auto pair:{std::pair{Joint(*m,"lwrist"),Joint(*m,"attach_shielda")},std::pair{Joint(*m,"rwrist"),Joint(*m,"attach_hand")}}){auto before=InverseAffine(restBones[pair.first])*restBones[pair.second],after=InverseAffine(bones[pair.first])*bones[pair.second];for(int k=0;k<16;++k)check(std::abs(before[k]-after[k])<.03f,"attachment floats away from grip");}
            ++samples;}
        check(Start(p),"taunt did not start");check(p->AnimSequence()==Clip,"wrong body animation");
#ifdef OPENRUNE_ENABLE_NETWORK
        RuneNetworkAuthority authority(game);check(authority.bind(1,p,1),"taunt authority bind");
#endif
        for(float dt:{.016f,.033f,.11f}) {
            p->ShieldTaunt.sounded=0;float phase=0,step=30.f/67.f*dt;
            while(phase<1){AnimationFrames(p,phase,phase+step);phase+=step;}
            check(p->ShieldTaunt.sounded==7,"not exactly three clang beats");
            auto bits=p->ShieldTaunt.sounded;AnimationFrames(p,0,1);check(p->ShieldTaunt.sounded==bits,"replayed beat");
        }
#ifdef OPENRUNE_ENABLE_NETWORK
        auto sounds=authority.drainSounds();check(sounds.size()==9,"spatial sound replication count");for(auto& cue:sounds){check(cue.object>0&&!cue.package.empty(),"invalid sound reference");check(cue.channel==SLOT_Interact*2,"clang incorrectly uses ambient audio");}
        openrune::net::World world;check(authority.capture(world),"taunt snapshot capture");bool clipCaptured=false;
        for(auto& e:world)if(e.ref==authority.identity(p))clipCaptured=e.bodyAnimation==seq+1;check(clipCaptured,"taunt not replicated");
        // Exercise the receiving sampler too, rather than only inspecting an outgoing index.
        p->AnimFrame()=Beats[0]*30/67;p->AnimRate()=30.f/67;
        if(auto* proxy=UObject::TryCast<UActor>(p->GetUObject("AnimProxy"))){check(proxy->AnimSequence()==Clip,"torso did not join taunt");proxy->AnimFrame()=p->AnimFrame();}
        check(authority.capture(world),"contact snapshot capture");
        RuneNetworkReplica replica;UActor* remote=nullptr;std::vector<UActor*> copies;
        for(const auto& e:world){UActor* source=nullptr;for(auto* actor:game.Level->Actors)if(actor&&authority.identity(actor)==e.ref){source=actor;break;}check(source!=nullptr,"replica archetype");
            auto* copy=UObject::Cast<UActor>(game.packages->GetTransientPackage()->NewObject("TauntReplica"+std::to_string(modelID)+"_"+std::to_string(e.ref.id),source->Class,ObjectFlags::Transient));
            copy->XLevel()=game.Level;copy->Level()=game.LevelInfo;copy->Index=int(game.Level->Actors.size());game.Level->Actors.push_back(copy);copies.push_back(copy);
            if(source->HasProperty("AnimProxy"))if(auto* original=source->GetUObject("AnimProxy")){auto* proxy=UObject::Cast<UActor>(game.packages->GetTransientPackage()->NewObject("TauntProxy"+std::to_string(modelID)+"_"+std::to_string(e.ref.id),original->Class,ObjectFlags::Transient));proxy->Owner()=copy;proxy->NetworkReplica=true;copy->SetObject("AnimProxy",proxy);}
            check(replica.bind(e.ref,copy),"replica binding");if(e.ref==authority.identity(p))remote=copy;
        }
        openrune::net::Snapshot snapshot;snapshot.id=1;snapshot.tick=1;snapshot.world=world;
        check(replica.install(snapshot),"replica rejected clip");replica.present(1);check(remote&&remote->AnimSequence()==Clip,"remote taunt missing");
        if(modelID==5){
            auto bones=m->ActorJointMatrices(p);auto world=GetRuneMeshToWorld(p);auto actor=Coords::Rotation(p->Rotation()).ToMatrix();auto inverse=InverseAffine(world);
            auto hand=JointPoint(world*bones[Joint(*m,"attach_hand")]);
            auto* wm=GetRuneGeometry(p->Weapon());auto vertices=wm->SkinFrame(p->Weapon()->GetByte("SkelMesh"),0,0,p->Weapon());auto ww=GetRuneMeshToWorld(p->Weapon());vec3 tip=hand;float farthest=0;
            for(auto v:vertices){auto at=(ww*vec4(v,1)).xyz();float distance=length(at-hand);if(distance>farthest){farthest=distance;tip=at;}}
            auto axis=(InverseAffine(bones[Joint(*m,"attach_hand")])*inverse*vec4(tip-hand,0)).xyz();
            auto* shield=Shield(p);auto* sm=GetRuneGeometry(shield);auto sv=sm->SkinFrame(shield->GetByte("SkelMesh"),0,0,shield);auto sw=GetRuneMeshToWorld(shield);vec3 low(1e9),high(-1e9);
            auto bounds=[&](const char* label,const Array<vec3>& vertices,const mat4& world,const mat4& attachment){vec3 lo(1e9),hi(-1e9);for(auto v:vertices){auto at=(InverseAffine(attachment)*world*vec4(v,1)).xyz();for(int k=0;k<3;++k){lo[k]=std::min(lo[k],at[k]);hi[k]=std::max(hi[k],at[k]);}}std::cout<<"TAUNT_LOCAL_BOUNDS "<<label<<" min="<<lo.x<<","<<lo.y<<","<<lo.z<<" max="<<hi.x<<","<<hi.y<<","<<hi.z<<std::endl;};
            bounds("sword",vertices,ww,world*bones[Joint(*m,"attach_hand")]);
            float bladeBack=1000;for(auto v:vertices){auto at=(InverseAffine(world*bones[Joint(*m,"attach_hand")])*ww*vec4(v,1)).xyz();if(at.y>10&&at.y<30)bladeBack=std::min(bladeBack,at.x);}std::cout<<"TAUNT_BLADE_BACK "<<bladeBack<<std::endl;bounds("shield",sv,sw,world*bones[shield->GetByte("AttachParentJoint")]);
            float shieldFront=-1e9f,swordBack=1e9f;auto toActor=InverseAffine(actor);
            for(auto v:sv)shieldFront=std::max(shieldFront,(toActor*sw*vec4(v,1)).x);
            for(auto v:vertices)swordBack=std::min(swordBack,(toActor*ww*vec4(v,1)).x);
            std::cout<<"TAUNT_CONTACT_CLEARANCE "<<swordBack-shieldFront<<std::endl;
            for(auto v:sv){auto at=(InverseAffine(actor)*vec4((sw*vec4(v,1)).xyz()-p->Location(),0)).xyz();for(int k=0;k<3;++k){low[k]=std::min(low[k],at[k]);high[k]=std::max(high[k],at[k]);}}
            std::cout<<"TAUNT_GEOMETRY sword_axis="<<axis.x<<","<<axis.y<<","<<axis.z<<" shield_joint="<<m->Joints[shield->GetByte("AttachParentJoint")].Name.ToString()<<" shield_center="<<(low.x+high.x)/2<<","<<(low.y+high.y)/2<<","<<(low.z+high.z)/2<<std::endl;
        }
        auto actual=m->SampleAnimation(remote),expected=m->SampleFrame(seq,p->AnimFrame(),false);for(size_t j=0;j<actual.size();++j)check(length((m->JointMatrices(actual)[j]*vec4(0,0,0,1)-m->JointMatrices(expected)[j]*vec4(0,0,0,1)).xyz())<.1f,"remote interpolated pose differs");
        for(auto* copy:copies)copy->Destroy();
        std::cout<<"RUNE_SHIELD_TAUNT_NETWORK model="<<modelID<<" sounds="<<sounds.size()<<" body_index="<<seq+1<<" replica=1"<<std::endl;
#endif
        --p->Health();Tick(p,.016f);check(!p->ShieldTaunt.active,"hit did not interrupt taunt");p->ShieldTaunt.cooldown=0;
        Equip(game,p);p->Health()=100;check(Start(p),"repeat start");auto priorState=p->GetStateName();
        for(int frame=0;frame<160;++frame){p->Tick(1.f/60);if(auto* proxy=UObject::TryCast<UActor>(p->GetUObject("AnimProxy")))proxy->Tick(1.f/60);}
        std::cout<<"TAUNT_FULL_TICK active="<<p->ShieldTaunt.active<<" beats="<<p->ShieldTaunt.sounded<<" physics="<<int(p->Physics())<<" state="<<p->GetStateName().ToString()<<" body="<<p->AnimSequence().ToString()<<" armed="<<Armed(p)<<std::endl;
        check(!p->ShieldTaunt.active&&p->ShieldTaunt.sounded==7,"full tick taunt did not complete with three beats");check(p->GetStateName()==priorState&&p->Physics()==PHYS_Walking,"taunt changed combat state/grounding");
        Equip(game,p);check(Start(p),"death fixture start");p->Health()=0;auto death=p->AnimSequence();Tick(p,.016f);check(!p->ShieldTaunt.active&&p->AnimSequence()==death,"death overwritten");p->Destroy();
    }
    auto* living=game.LoginNetworkPlayer("Living bot",-1,0,0);Equip(game,living);living->ShieldTaunt.bot=true;check(!Eligible(living)&&!Start(living),"living bot taunted");
    UPawn* npc=nullptr;
    for(float distance:{110.f,180.f,260.f})for(int i=0;i<16&&!npc;++i){float angle=i*6.2831853f/16;auto at=living->Location()+vec3(std::cos(angle),std::sin(angle),0)*distance;
        if(living->CheckLocation(at,22,42,true).first)npc=UObject::TryCast<UPawn>(living->Spawn(game.packages->GetPackage("RuneI")->GetClass("DarkWarrior"),{},{},at,{}));}
    check(npc!=nullptr,"campaign fixture lane");
    Equip(game,npc);npc->GotoState("Fighting","Begin");check(Eligible(npc)&&Start(npc),"campaign enemy taunt absent");
    npc->ShieldTaunt.active=false;Resume(npc);vec3 first,second;bool lane=false;
    for(auto* a:game.Level->Actors)if(a&&a->IsA("NavigationPoint"))for(auto* b:game.Level->Actors)if(!lane&&b&&b->IsA("NavigationPoint")){
        auto delta=b->Location()-a->Location();if(length(delta)<260||length(delta)>650||std::abs(delta.z)>20||!living->FastTrace(b->Location(),a->Location()))continue;
        if(!living->CheckLocation(a->Location(),22,42,true).first||!living->CheckLocation(b->Location(),22,42,true).first)continue;
        first=a->Location();second=b->Location();lane=true;
    }
    check(lane,"automatic taunt fixture lane");living->SetLocation(second);npc->SetLocation(first);npc->SetRotation(Rotator::FromVector(second-first));npc->Enemy()=living;npc->ShieldTaunt.cooldown=0;npc->LoopAnim("weapon1_idle",1,0,0);Tick(npc,.016f);check(npc->ShieldTaunt.active,"campaign auto taunt absent");npc->Destroy();
    auto* automatic=game.LoginNetworkPlayer("Automatic bot",-1,5,0);Equip(game,automatic);automatic->ShieldTaunt.bot=true;automatic->SetLocation(first);automatic->SetRotation(Rotator::FromVector(second-first));
    automatic->GetUObject("PlayerReplicationInfo")->SetByte("Team",0);living->GetUObject("PlayerReplicationInfo")->SetByte("Team",0);game.GameInfo->SetBool("bTeamGame",true);
    automatic->ShieldTaunt.cooldown=0;Tick(automatic,.016f);check(!automatic->ShieldTaunt.active,"taunted a teammate");
    living->GetUObject("PlayerReplicationInfo")->SetByte("Team",1);automatic->ShieldTaunt.cooldown=0;Tick(automatic,.016f);check(automatic->ShieldTaunt.active,"multiplayer bot auto taunt absent");
    std::cout<<"RUNE_SHIELD_TAUNT_AUTO_OK campaign=1 multiplayer=1 teammates_excluded=1"<<std::endl;
    std::cout<<"RUNE_SHIELD_TAUNT_OK models=4 samples="<<samples<<" preserved_lengths="<<preserved<<" players_excluded=1 missing_equipment=1 interrupts=1 campaign_ai=1"<<std::endl;
}
void Demo(Engine& game,int frame){
    if(!commandline->HasArg("","--shield-taunt-demo"))return;
    if(!commandline->HasArg("","--save-directory")||!commandline->HasArg("","--maxframes"))throw std::runtime_error("Taunt demo requires bounded isolated saves");
    static UPawn* p=nullptr;static vec3 camera,look;static bool live=false;static double lastTaunt=-100;static unsigned lastBeats=0;static int auditDelay=0;
    if(frame==60){
        auto* viewer=game.viewport->Actor();vec3 at=viewer->Location(),forward(1,0,0);bool found=false;
        for(int i=0;i<16&&!found;++i){float angle=i*6.2831853f/16;forward={std::cos(angle),std::sin(angle),0};found=viewer->CheckLocation(at+forward*160.f,22,42,true).first&&viewer->FastTrace(at+forward*160.f,at);}
        if(!found)throw std::runtime_error("No taunt demo lane");
        bool authority=game.NetworkAuthority;game.NetworkAuthority=true;p=game.LoginNetworkPlayer("Shield taunt",-1,5,0);game.NetworkAuthority=authority;Equip(game,p);p->SetLocation(at+forward*160.f);p->SetRotation(Rotator::FromVector(-forward));p->SetCollision(false,false,false);p->SetPhysics(PHYS_None);p->ShieldTaunt.cooldown=100000;p->bUnlit()=true;p->Weapon()->bUnlit()=true;if(auto* shield=Shield(p))shield->bUnlit()=true;
        for(auto* event:{"Tick","PlayerTick","Timer","AnimEnd","AnimProxyEnd"})p->DisableEvent(event);if(p->StateFrame)p->StateFrame->SetLatentState(LatentRunState::Stop);
        auto side=vec3(-forward.y,forward.x,0);camera=at+side*45.f+vec3(0,0,25);look=p->Location()+vec3(0,0,15);
        live=commandline->GetArg("","--shield-taunt-demo","")=="live";
        if(live){
            p->ShieldTaunt.bot=true;p->SetPhysics(PHYS_Walking);vec3 floor,normal;auto at=p->Location();
            if(p->Trace(floor,normal,at-vec3(0,0,350),at,false,vec3(10,10,0))&&normal.z>.7f)p->SetLocation(floor+vec3(0,0,p->CollisionHeight()+.25f));
            p->LoopAnim("weapon1_idle",1,0,0);
            // A close-up animation review, isolated from ordinary game lighting/settings.
            p->bUnlit()=true;p->Weapon()->bUnlit()=true;if(auto* shield=Shield(p))shield->bUnlit()=true;
            camera=p->Location()-forward*120.f+side*25.f+vec3(0,0,18);look=p->Location()+vec3(0,0,12);
        }else p->PlayAnim(Clip,0,0);LogMessage("SHIELD_TAUNT_DEMO_READY");
    }
    if(p){
        p->Velocity()=p->Acceleration()=vec3(0);
        if(live&&!p->ShieldTaunt.active&&game.TotalTime-lastTaunt>=3.4){
            if(Start(p)){lastTaunt=game.TotalTime;p->ShieldTaunt.cooldown=100000;}
        }
        if(!live){float t=std::clamp(std::stof(commandline->GetArg("","--shield-taunt-demo",".55")),0.f,Duration);p->AnimFrame()=t*30/67;p->AnimRate()=0;}
        if(auto* proxy=UObject::TryCast<UActor>(p->GetUObject("AnimProxy"))){proxy->AnimSequence()=p->AnimSequence();proxy->AnimFrame()=p->AnimFrame();proxy->AnimRate()=p->AnimRate();proxy->bAnimLoop()=false;}
        if(live&&p->ShieldTaunt.sounded&&p->ShieldTaunt.sounded!=lastBeats)auditDelay=2;
        lastBeats=p->ShieldTaunt.sounded;
        if(live&&auditDelay>0&&!--auditDelay&&game.audiodev&&game.audiodev->GetDevice()){
            Array<std::string> stats;game.audiodev->AddStats(stats);auto* sound=p->Weapon()->GetUObject("HitShield");
            for(size_t i=0;i<stats.size();++i)if(sound&&stats[i].find(sound->Name.ToString())!=std::string::npos)LogMessage("TAUNT_AUDIO_BACKEND playing="+std::to_string(game.audiodev->GetDevice()->IsPlaying(int(i)))+" "+stats[i]);
        }
        game.CameraLocation=camera;game.CameraRotation=Rotator::FromVector(look-camera);
    }
}
}
