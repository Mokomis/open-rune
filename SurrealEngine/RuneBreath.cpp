#include "Precomp.h"
#include "RuneBreath.h"
#include "RunePhysics.h"
#include "RuneCharacters.h"
#include "Engine.h"
#include "GameWindow.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "VM/ScriptCall.h"
#include "VM/Frame.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace RuneBreath {
namespace {
bool nonlivingName(std::string name) {
    for(auto& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));
    for(const char* token:{"zombie","undead","skeleton","draugr","ghoul","ghost","sark","darkwarrior","dwarfmech","stoneguard"})
        if(name.find(token)!=std::string::npos)return true;
    return false;
}
bool sky(const CollisionHit& hit) {
    return hit.Actor==engine->LevelInfo && hit.Node && hit.Node->Surf>=0 &&
        size_t(hit.Node->Surf)<engine->Level->Model->Surfaces.size() &&
        (engine->Level->Model->Surfaces[hit.Node->Surf].PolyFlags&PF_FakeBackdrop);
}
struct Visual {USkelModel* model;uint8_t mesh;};
std::vector<Visual> deadVisuals;Engine* visualEngine=nullptr;ULevelInfo* visualLevel=nullptr;
void visuals() {
    if(visualEngine==engine && visualLevel==engine->LevelInfo && !deadVisuals.empty())return;
    visualEngine=engine;visualLevel=engine->LevelInfo;deadVisuals.clear();
    for(const auto& c:openrune::Characters)if(nonlivingName(c.className)) {
        auto* p=engine->packages->FindClass(c.className)->GetDefaultObject<UPawn>();
        if(auto* m=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(p));m && p->HasProperty("SkelMesh"))deadVisuals.push_back({m,p->GetByte("SkelMesh")});
    }
}
}
bool Undead(UPawn* p) {
    if(!p || !p->Class)return true;
    for(auto* c=p->Class;c;c=UObject::TryCast<UClass>(c->BaseStruct))if(nonlivingName(c->Name.ToString()))return true;
    for(const char* flag:{"bIsUndead","bUndead","bIsSark"})if(p->HasProperty(flag) && p->GetBool(flag))return true;
    if(engine && engine->LaunchInfo.IsRune() && p->HasProperty("SkelMesh")) {
        // The same undead visual must not breathe after a model swap onto a
        // nominally living player/bot class. Do not rely only on the login name.
        visuals();auto* m=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(p));
        for(auto v:deadVisuals)if(v.model==m && v.mesh==p->GetByte("SkelMesh"))return true;
    }
    return false;
}
namespace {
bool idleSequence(const NameString& sequence) {
    auto name=sequence.ToString();
    for(auto& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));
    // Cinematic/rope/swimming poses are authored movement, not a breathing idle.
    for(const char* excluded:{"cine", "crucified", "climb", "tread", "powerup"})
        if(name.find(excluded)!=std::string::npos)return false;
    return name.find("idle")!=std::string::npos || name.find("breathe")!=std::string::npos;
}
// Deliberately non-breathing gestures: long rests, a crooked glance, a hand
// spasm, or a silent head-back scream. No simulation RNG, sounds or new state.
struct IdleGesture {float pitch=0,yaw=0,roll=0,hand=0,jaw=0;};
float ease(float x){x=std::clamp(x,0.f,1.f);return x*x*(3-2*x);}
float gestureEnvelope(float t,float rise,float hold,float fall) {
    if(t<0||t>=rise+hold+fall)return 0;
    return t<rise?ease(t/rise):t<rise+hold?1.f:1.f-ease((t-rise-hold)/fall);
}
uint32_t idleSeed(UPawn* p) {
    uint32_t h=2166136261u;
    for(unsigned char c:p->Name.ToString()){h^=c;h*=16777619u;}
    if(p->HasProperty("SkelMesh")){h^=p->GetByte("SkelMesh");h*=16777619u;}
    return h;
}
IdleGesture gestureAt(uint32_t seed,double seconds) {
    IdleGesture g;if(!std::isfinite(seconds)||seconds<0)return g;
    const double period=11.0+(seed%501)*.01;
    double clock=seconds+(seed>>9)%10000*.001,cycle=std::floor(clock/period);
    float t=float(clock-cycle*period)-2.f,sign=((uint64_t(cycle)+(seed>>24))&1)?-1.f:1.f;
    switch((uint64_t(cycle)+(seed>>16))%3) {
    case 0:{float a=gestureEnvelope(t,.45f,.9f,.75f);g.yaw=.30f*a*sign;g.roll=.24f*a*sign;break;}
    case 1:{float a=gestureEnvelope(t,.13f,.06f,.25f)+.7f*gestureEnvelope(t-.62f,.1f,.02f,.28f);g.hand=.18f*a;g.yaw=.08f*gestureEnvelope(t,.25f,.25f,.65f)*sign;break;}
    default:{float a=gestureEnvelope(t,.8f,1.1f,.9f);g.pitch=-.48f*a;g.jaw=.34f*a;g.roll=.07f*a*sign;break;}
    }
    return g;
}
bool gestureJoint(const NameString& n){return n=="Head"||n=="Jaw"||n=="lwrist";}
bool gestureCarrier(const USkelModel& model,size_t j) {
    for(size_t steps=0;steps<model.Joints.size();++steps){if(gestureJoint(model.Joints[j].Name))return true;if(!j)break;j=model.Joints[j].Parent;}
    return false;
}
bool canGesture(UPawn* p) {
    // Class defaults expose property storage only, not native UPawn members.
    // Character cards must never read runtime combat fields from that adapter.
    if(UObject::TryCast<UClass>(p))return true;
    if(length(p->Velocity())>8.f || (p->Physics()!=PHYS_None&&p->Physics()!=PHYS_Walking) ||
       p->RuneKickActive||p->RuneBashPhase||p->Slam.phase||p->Bloodlust.active||p->Dual.guard||
       p->Tactics.attacking||p->Tactics.blocking)return false;
    if(!p->AnimSequence().IsNone()&&!idleSequence(p->AnimSequence()))return false;
    auto* proxy=UObject::TryCast<UActor>(p->GetUObject("AnimProxy"));
    return !proxy || (proxy->RuneCombatPreviewWeight<=0 && (proxy->AnimSequence().IsNone()||idleSequence(proxy->AnimSequence())));
}
void applyGesture(const USkelModel& model,Array<RuneJointPose>& pose,const IdleGesture& g) {
    if(!(g.pitch||g.yaw||g.roll||g.hand||g.jaw))return;
    // Same actor-axis conversion as head recoil. Rotate about joint pivots;
    // positions/scales and limb lengths are never changed.
    auto basis=Coords::Rotation(model.RotationOffset).ToMatrix();auto inv=mat3::inverse(mat3(basis));auto inverse=mat4::identity();
    for(int c=0;c<3;++c)for(int row=0;row<3;++row)inverse[c*4+row]=inv[c*3+row];
    for(size_t j=1;j<model.Joints.size();++j) {
        auto n=model.Joints[j].Name;mat4 delta;
        if(n=="Head")delta=mat4::rotate(g.yaw,0,0,1)*mat4::rotate(g.pitch,0,1,0)*mat4::rotate(g.roll,1,0,0);
        else if(n=="Jaw"&&g.jaw)delta=mat4::rotate(g.jaw,0,1,0);
        else if(n=="lwrist"&&g.hand)delta=mat4::rotate(g.hand,1,0,0);
        else continue;
        auto bones=model.JointMatrices(pose);auto parent=mat3(bones[model.Joints[j].Parent]);
        auto local=mat3::inverse(parent)*mat3(inverse*delta*basis)*parent*mat3(RunePoseRotation(pose[j].Rotation));auto rotation=mat4::identity();
        for(int c=0;c<3;++c){vec3 axis(local[c*3],local[c*3+1],local[c*3+2]);float scale=length(axis);if(scale>.0001f)axis/=scale;for(int row=0;row<3;++row)rotation[c*4+row]=axis[row];}
        pose[j].Rotation=RunePoseEuler(rotation);
    }
}
bool sameJoint(const RuneJointPose& a,const RuneJointPose& b) {
    return a.Position.x==b.Position.x && a.Position.y==b.Position.y && a.Position.z==b.Position.z &&
        a.Scale.x==b.Scale.x && a.Scale.y==b.Scale.y && a.Scale.z==b.Scale.z && a.Rotation==b.Rotation;
}
}
void IdlePose(const USkelModel& model,UActor* actor,size_t sequence,Array<RuneJointPose>& pose,double idleSeconds) {
    if(sequence>=model.Sequences.size() || !idleSequence(model.Sequences[sequence].Info.Name))return;
    auto* p=UObject::TryCast<UPawn>(actor);
    if(!p && actor && actor->IsA("AnimationProxy"))p=UObject::TryCast<UPawn>(actor->Owner());
    // Menu previews use Rune class default-property objects, not spawned pawns.
    if(!p)if(auto* cls=UObject::TryCast<UClass>(actor))
        for(auto* base=cls;base;base=UObject::TryCast<UClass>(base->BaseStruct))
            if(base->Name=="Pawn"){p=cls->GetDefaultObject<UPawn>();break;}
    if(!p || p->Health()<=0 || !Undead(p))return;
    // Breathing is authored across the entire idle: pelvis motion carries the
    // chest, and shoulders/arms continue the same cycle. Holding only spine
    // joints leaves visible breathing. Keep this idle's first-frame stance;
    // sparse non-breathing gestures replace the loop. Transitions, independent
    // aim/guard, IK and hit reactions remain authoritative. Walking, attacks,
    // climbing, swimming, cinematics and corpses are untouched.
    pose=model.DecodeFrame(sequence,0);
    if(canGesture(p)) {
        double seconds=idleSeconds>=0?idleSeconds:engine&&engine->LevelInfo?engine->LevelInfo->TimeSeconds():0;
        applyGesture(model,pose,gestureAt(idleSeed(p),seconds));
    }
}
bool Eligible(UPawn* p) {
    return engine && engine->LaunchInfo.IsRune() && p && !p->bDeleteMe() && !p->bHidden() && p->Health()>0 &&
        p->XLevel()==engine->Level && !Undead(p) && UObject::TryCast<USkelModel>(GetRuneDisplayMesh(p));
}
bool Mouth(UPawn* p,vec3& position,vec3& forward) {
    if(!Eligible(p))return false;
    auto* m=UObject::Cast<USkelModel>(GetRuneDisplayMesh(p));int head=-1;
    for(size_t j=0;j<m->Joints.size();++j)if(m->Joints[j].Name=="head"){head=int(j);break;}
    auto scale=std::clamp(p->DrawScale(),.25f,4.f);
    forward={std::cos(p->Rotation().YawRadians()),std::sin(p->Rotation().YawRadians()),0};
    if(head>=0) {auto bones=m->ActorJointMatrices(p);position=(GetRuneMeshToWorld(p)*bones[head]*vec4(0,0,0,1)).xyz()+forward*(5.f*scale)-vec3(0,0,3.f*scale);}
    else position=p->Location()+vec3(0,0,p->CollisionHeight()*.65f)+forward*(p->CollisionRadius()*.6f);
    return std::isfinite(position.x)&&std::isfinite(position.y)&&std::isfinite(position.z);
}
bool Outside(const vec3& at) {
    if(!engine || !engine->Level || !engine->Level->Model || !std::isfinite(at.x+at.y+at.z))return false;
    auto* zone=engine->LevelInfo->GetLocZone(at,{}).Zone;
    if(!zone || zone->bWaterZone() || zone->bPainZone() || zone->IsA("SkyZoneInfo"))return false;
    // Exact mouth/puff ray: no zone-name guesses, delayed indoor cache or
    // dependence on the snow setting. Only world and solid scenery count as
    // shelter; another pawn is not a building roof.
    for(const auto& hit:engine->Level->Collision.Trace(at,at+vec3(0,0,32768),0,0,true,true,false)) {
        if(!hit.Actor || hit.Actor->IsA("Pawn") || hit.Actor->IsA("Inventory"))continue;
        if(hit.Actor==engine->LevelInfo || hit.Actor->IsA("Mover") ||
           (hit.Actor->bBlockPlayers() && !hit.Actor->Owner()))return sky(hit);
    }
    return false;
}
void Probe(Engine& game) {
    auto check=[](bool b,const char* why){if(!b)throw std::runtime_error(std::string("breath: ")+why);};
    unsigned living=0,excluded=0,open=0,closed=0;
    for(size_t i=0;i<openrune::Characters.size();++i)for(int s=0;s<openrune::Characters[i].skins;++s) {
        auto* p=game.packages->FindClass(openrune::Characters[i].className)->GetDefaultObject<UPawn>();
        const bool dead=i==5||i==6||i==18||(i>=20&&i<=25);
        check(Undead(p)==dead,"roster classification");if(dead)++excluded;else ++living;
    }
    for(const char* map:{"DM-Seaside","DM-Thorstadt","DM-Hildir"}) {
        auto url=game.GetDefaultURL(std::string(map)+".run");url.AddOrReplaceOption("game=RuneI.RuneMultiPlayer");game.NetworkAuthority=true;game.LoadMap(url);
        unsigned outside=0,inside=0;for(auto* a:game.Level->Actors)if(a && a->IsA("NavigationPoint")) {
            if(Outside(a->Location()+vec3(0,0,35)))++outside;else ++inside;
        }
        if(std::string(map)=="DM-Thorstadt")check(outside>0&&inside>0,"mixed indoor/outdoor map");
        open+=outside;closed+=inside;std::cout<<"BREATH_EXPOSURE map="<<map<<" outdoors="<<outside<<" covered="<<inside<<std::endl;
    }
    auto* p=game.LoginNetworkPlayer("Breath");check(Eligible(p),"living pawn eligible");
    auto* zombie=game.packages->FindClass("RuneI.PlayerZombie")->GetDefaultObject<UPawn>();auto mesh=p->GetByte("SkelMesh");p->SetByte("SkelMesh",zombie->GetByte("SkelMesh"));check(Undead(p)&&!Eligible(p),"undead model on living class");p->SetByte("SkelMesh",mesh);
    p->Health()=0;check(!Eligible(p),"dead bodies excluded");p->Health()=100;
    if(commandline->HasArg("","--rune-undead-idle-probe")) {
        unsigned changed=0,preserved=0,filteredSamples=0,worldStable=0,livingMotion=0;
        for(const auto& c:openrune::Characters) {
            auto* actor=game.packages->FindClass(c.className)->GetDefaultObject<UPawn>();
            auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(actor));check(model!=nullptr,"idle model");
            for(size_t seq=0;seq<model->Sequences.size();++seq) {
                const bool filtered=Undead(actor)&&idleSequence(model->Sequences[seq].Info.Name);
                auto rest=model->DecodeFrame(seq,0);
                for(float phase:{.21f,.63f}) {
                    auto raw=model->SampleFrame(seq,phase,true),pose=raw;IdlePose(*model,actor,seq,pose);
                    for(size_t j=0;j<pose.size();++j) {
                        if(filtered) {
                            if(!gestureJoint(model->Joints[j].Name))check(sameJoint(pose[j],rest[j]),"undead breathing carrier remained");
                            if(!sameJoint(raw[j],pose[j]))++changed;
                        } else {
                            check(sameJoint(pose[j],raw[j]),"living/combat/non-idle pose changed");++preserved;
                            if(!Undead(actor)&&idleSequence(model->Sequences[seq].Info.Name)&&!sameJoint(raw[j],rest[j]))++livingMotion;
                        }
                    }
                    if(filtered) {
                        ++filteredSamples;
                        auto bones=model->JointMatrices(pose),restBones=model->JointMatrices(rest);
                        // World-space chest/shoulders were still moving under the
                        // old chest-only fix. Check every carrier, not just locals.
                        for(size_t j=0;j<bones.size();++j)if(!gestureCarrier(*model,j))for(vec4 point:{vec4(0,0,0,1),vec4(1,0,0,1),vec4(0,1,0,1),vec4(0,0,1,1)}) {
                            check(length((bones[j]*point-restBones[j]*point).xyz())<.00001f,"undead idle world motion");++worldStable;
                        }
                    }
                }
            }
        }
        check(changed>0 && preserved>0 && filteredSamples>0 && livingMotion>0,"idle correction coverage");
        unsigned gesturing=0;
        for(const auto& c:openrune::Characters) {
            auto* actor=game.packages->FindClass(c.className)->GetDefaultObject<UPawn>();if(!Undead(actor))continue;
            auto* m=UObject::Cast<USkelModel>(GetRuneDisplayMesh(actor));size_t seq=0;for(size_t i=0;i<m->Sequences.size();++i)if(m->Sequences[i].Info.Name=="neutral_idle")seq=i;
            auto restPose=m->DecodeFrame(seq,0);bool moved=false;
            for(double t=0;t<60;t+=.2){auto sample=restPose;IdlePose(*m,actor,seq,sample,t);
                for(size_t j=0;j<sample.size();++j){if(!sameJoint(sample[j],restPose[j]))moved=true;check(length(sample[j].Position-restPose[j].Position)==0&&length(sample[j].Scale-restPose[j].Scale)==0,"gesture stretched model");}}
            check(moved,"undead gesture absent");++gesturing;
        }
        std::cout<<"RUNE_UNDEAD_GESTURES_OK models="<<gesturing<<" silent=1"<<std::endl;
        auto* model=UObject::Cast<USkelModel>(GetRuneDisplayMesh(p));size_t idle=0;
        for(size_t i=0;i<model->Sequences.size();++i)if(model->Sequences[i].Info.Name=="neutral_idle")idle=i;
        p->SetByte("SkelMesh",zombie->GetByte("SkelMesh"));p->AnimSequence()=model->Sequences[idle].Info.Name;p->AnimFrame()=.37f;p->bAnimLoop()=true;
        // Exercise live pawn state at a time when a gesture is actually active,
        // not a resting interval that would pass even without the yield guards.
        auto* liveProxy=UObject::TryCast<UActor>(p->GetUObject("AnimProxy"));check(liveProxy!=nullptr,"live gesture proxy");
        liveProxy->AnimSequence()=p->AnimSequence();liveProxy->bAnimLoop()=true;
        auto held=model->DecodeFrame(idle,0);double activeTime=-1;
        for(double t=0;t<60;t+=.1){auto sample=held;IdlePose(*model,p,idle,sample,t);for(size_t j=0;j<sample.size();++j)if(!sameJoint(sample[j],held[j])){activeTime=t;break;}if(activeTime>=0)break;}
        check(activeTime>=0,"live pawn gestures absent");
        auto savedVelocity=p->Velocity();p->Velocity()=vec3(100,0,0);auto moving=held;IdlePose(*model,p,idle,moving,activeTime);
        for(size_t j=0;j<held.size();++j)check(sameJoint(held[j],moving[j]),"gesture during movement");p->Velocity()=savedVelocity;
        bool savedKick=p->RuneKickActive;p->RuneKickActive=true;auto kicking=held;IdlePose(*model,p,idle,kicking,activeTime);
        for(size_t j=0;j<held.size();++j)check(sameJoint(held[j],kicking[j]),"gesture during kick");p->RuneKickActive=savedKick;
        bool savedGuard=p->Dual.guard;p->Dual.guard=true;auto guarding=held;IdlePose(*model,p,idle,guarding,activeTime);
        for(size_t j=0;j<held.size();++j)check(sameJoint(held[j],guarding[j]),"gesture during guard");p->Dual.guard=savedGuard;
        auto body=model->SampleAnimation(p),rest=model->DecodeFrame(idle,0);IdlePose(*model,p,idle,rest);
        for(size_t j=0;j<body.size();++j)check(sameJoint(body[j],rest[j]),"swapped model sample");
        auto* proxy=UObject::TryCast<UActor>(p->GetUObject("AnimProxy"));check(proxy!=nullptr,"idle proxy");
        proxy->AnimSequence()=p->AnimSequence();proxy->AnimFrame()=.63f;proxy->bAnimLoop()=true;
        auto upper=model->SampleAnimation(proxy);for(size_t j=0;j<upper.size();++j)check(sameJoint(upper[j],rest[j]),"proxy sample");
        auto raw=model->SampleFrame(idle,.37f,true);p->Health()=0;auto dead=raw;IdlePose(*model,p,idle,dead);
        for(size_t j=0;j<dead.size();++j)check(sameJoint(dead[j],raw[j]),"corpse pose changed");p->Health()=100;
        // The target is corrected before a transition, not pinned over its blend.
        p->RuneTweenModel=model;p->RuneTweenTarget=p->AnimSequence();p->RuneTweenPose=raw;
        p->AnimFrame()=-.5f/model->Sequences[idle].Info.NumFrames;
        auto blend=model->SampleAnimation(p);
        for(size_t j=0;j<blend.size();++j)check(sameJoint(blend[j],BlendRunePose(raw[j],rest[j],.5f)),"idle tween continuity");
        p->SetByte("SkelMesh",mesh);
        std::cout<<"RUNE_UNDEAD_IDLE_OK changed_tracks="<<changed<<" preserved_tracks="<<preserved<<" held_idle_samples="<<filteredSamples<<" stable_world_points="<<worldStable<<" living_animated_tracks="<<livingMotion<<" body=1 proxy=1 swapped=1 corpse_unchanged=1 tween=1"<<std::endl;
    }

    std::cout<<"RUNE_BREATH_OK living_variants="<<living<<" excluded_variants="<<excluded<<" outdoors="<<open<<" covered="<<closed<<" default_physics="<<RunePhysics::RequestedMode.load()<<std::endl;
}
void Demo(Engine& game,int frame) {
    if(!commandline->HasArg("","--breath-demo"))return;
    if(!commandline->HasArg("","--save-directory"))throw std::runtime_error("Breath demo requires isolated saves");
    static vec3 camera,look;static bool ready=false;static std::vector<UPawn*> models;static std::vector<vec3> anchors;static Rotator facing;
    if(frame==60) {
        auto size=DisplayBackend::Get()->GetScreenSize();auto geometry=game.window->GetFrameGeometry();geometry.width=size.width;geometry.height=size.height;game.window->SetFrameGeometry(geometry);game.window->ShowFullscreen();models.clear();anchors.clear();
        auto* p=game.viewport->Actor();if(!p)throw std::runtime_error("Breath demo needs player");
        bool covered=commandline->GetArg("","--breath-demo","")=="indoors",found=false;vec3 center,direction;
        for(auto* a:game.Level->Actors)if(a && a->IsA("NavigationPoint") && !found) {
            auto origin=a->Location();vec3 hit,normal;if(!p->Trace(hit,normal,origin-vec3(0,0,180),origin+vec3(0,0,80),false,vec3(8,8,0))||normal.z<.7f)continue;
            center=hit+vec3(0,0,p->CollisionHeight()+1);
            if(Outside(center+vec3(0,0,32))==covered)continue;
            for(int i=0;i<16&&!found;++i) {
                float angle=i*6.2831853f/16;direction={std::cos(angle),std::sin(angle),0};auto side=vec3(-direction.y,direction.x,0);
                camera=center+direction*190.f+side*130.f+vec3(0,0,45);look=center+vec3(0,0,22);
                found=Outside(center+side*65.f+vec3(0,0,35))!=covered&&Outside(center-side*65.f+vec3(0,0,35))!=covered&&p->FastTrace(camera,look)&&p->CheckLocation(center+side*65.f,22,42,true).first&&p->CheckLocation(center-side*65.f,22,42,true).first;
            }
        }
        if(!found)throw std::runtime_error("No breath demo lane");auto side=vec3(-direction.y,direction.x,0);
        facing=Rotator::FromVector(direction);int index=0;for(const char* type:{"RuneI.Ragnar","RuneI.DarkViking","RuneI.PlayerZombie"}) {
            UPawn* v=index==0?p:UObject::TryCast<UPawn>(p->Spawn(game.packages->FindClass(type),{},{},center+side*(index==1?-65.f:65.f),{}));if(!v)throw std::runtime_error("Breath demo actor");
            v->SetCollision(false,false,false);v->SetLocation(center+side*(index==0?0:index==1?-65.f:65.f));v->Health()=1000;v->GotoState("","");v->DisableEvent("PlayerTick");v->DisableEvent("Tick");v->DisableEvent("SeePlayer");v->DisableEvent("SeeMonster");v->DisableEvent("HearNoise");v->Enemy()=nullptr;v->SetPhysics(PHYS_Walking);v->Velocity()=v->Acceleration()=vec3(0);v->SetRotation(Rotator::FromVector(direction));v->LoopAnim("weapon1_idle",1,0,0);models.push_back(v);anchors.push_back(v->Location());++index;
        }
        ready=true;LogMessage(std::string("BREATH_DEMO_READY covered=")+(covered?"1":"0"));
    }
    if(ready){for(size_t i=0;i<models.size();++i){auto* v=models[i];v->GotoState("","");v->DisableEvent("PlayerTick");v->DisableEvent("Tick");v->DisableEvent("Timer");v->DisableEvent("SeePlayer");v->DisableEvent("SeeMonster");v->DisableEvent("HearNoise");v->DisableEvent("AnimEnd");v->StateFrame->SetLatentState(LatentRunState::Stop);v->Velocity()=v->Acceleration()=vec3(0);v->Enemy()=nullptr;v->SetLocation(anchors[i]);v->SetRotation(facing);v->LoopAnim("weapon1_idle",1,0,0);}game.CameraLocation=camera;game.CameraRotation=Rotator::FromVector(look-camera);}
}
}
