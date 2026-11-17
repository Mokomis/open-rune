#include "Precomp.h"
#include "RuneIK.h"
#include "Animation/RuneIKMath.h"
#include "Engine.h"
#include "RunePhysics.h"
#include "RuneCorpse.h"
#include "RuneCharacters.h"
#include <iostream>
#include <stdexcept>
#include "Physics/RunePhysicsWorld.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "Utils/CommandLine.h"
#include <map>
#include <cstring>

namespace RuneIK { namespace {
struct Leg {int hip=-1,knee=-1,ankle=-1;vec3 soleUp{0,0,1};};
struct Rig {Leg legs[2];bool valid=false,ready=false;};
struct Cache {const USkelModel* model=nullptr;uint64_t key=0;bool ready=false;float time=-1,rootOffset=0,offset[2]{0,0};Array<RuneJointPose> pose;};
std::map<const USkelModel*,Rig> rigs;
std::map<UActor*,Cache> cache;
Metrics metrics;
std::map<UActor*,bool> replicaGrounded;
vec3 Point(const mat4&m){return {m[12],m[13],m[14]};}
RuneRigid::Rotation Rotation(mat4 m){for(int c=0;c<3;++c){vec3 v(m[c*4],m[c*4+1],m[c*4+2]);float n=length(v);if(n<1e-6f)return {};for(int k=0;k<3;++k)m[c*4+k]/=n;}m[12]=m[13]=m[14]=m[3]=m[7]=m[11]=0;m[15]=1;auto q=normalize(quaternion::rotation_matrix(m));return {q.x,q.y,q.z,q.w};}
mat4 Matrix(RuneRigid::Rotation r){return mat4::quaternion(r.x,r.y,r.z,r.w);}
uint64_t Hash(uint64_t h,const void* data,size_t n){auto*p=static_cast<const unsigned char*>(data);for(size_t i=0;i<n;++i){h^=p[i];h*=1099511628211ull;}return h;}
int Joint(const USkelModel&m,const char* name){for(size_t i=0;i<m.Joints.size();++i)if(m.Joints[i].Name==name)return int(i);return -1;}
bool Ancestor(const USkelModel&m,int a,int child){for(int n=0;child>=0&&n<64;++n){if(child==a)return true;if(child==0)break;child=m.Joints[child].Parent;}return false;}
Rig& GetRig(const USkelModel&m,const mat4&world){auto& rig=rigs[&m];if(rig.ready)return rig;rig.ready=true;
 const char* names[2][3]={{"lhip","lknee","lankle"},{"rhip","rknee","rankle"}};rig.valid=true;
 for(int k=0;k<2;++k){auto&l=rig.legs[k];l.hip=Joint(m,names[k][0]);l.knee=Joint(m,names[k][1]);l.ankle=Joint(m,names[k][2]);if(l.hip<1||l.knee<1||l.ankle<1||!Ancestor(m,l.hip,l.knee)||!Ancestor(m,l.knee,l.ankle))rig.valid=false;}
 if(rig.valid){auto ref=m.JointMatrices(RuneCorpse::ReferencePose(m));for(auto&l:rig.legs)l.soleUp=RuneRigid::rotate(RuneRigid::inverse(Rotation(world*ref[l.ankle])),{0,0,1});}return rig;}
void SetWorldRotation(const USkelModel&m,Array<RuneJointPose>&pose,const Array<mat4>&bones,const mat4&world,int joint,RuneRigid::Rotation desired){int parent=m.Joints[joint].Parent;auto parentRotation=Rotation(joint?world*bones[parent]:world);auto local=RuneRigid::multiply(RuneRigid::inverse(parentRotation),desired);auto matrix=Matrix(local);pose[joint].Rotation=RunePoseEuler(joint?matrix:mat4::transpose(matrix));}
void Aim(const USkelModel&m,Array<RuneJointPose>&pose,const mat4&world,int joint,int child,vec3 target){auto bones=m.JointMatrices(pose);auto start=Point(world*bones[joint]),end=Point(world*bones[child]);if(length(end-start)<1e-5f||length(target-start)<1e-5f)return;auto delta=RuneRigid::between(end-start,target-start);SetWorldRotation(m,pose,bones,world,joint,RuneRigid::multiply(delta,Rotation(world*bones[joint])));}
}
void SetGrounded(UActor*a,bool value){replicaGrounded[a]=value;}
void Forget(UActor*a){cache.erase(a);replicaGrounded.erase(a);}
void Reset(){replicaGrounded.clear();cache.clear();rigs.clear();metrics={};}
Metrics Stats(){return metrics;}
void ApplyAtWorld(const USkelModel&m,UActor*a,Array<RuneJointPose>&pose,const mat4&world){
 if(!engine||!a||!a->XLevel()||a->bDeleteMe()||!engine->LaunchInfo.IsRune()||!engine->LevelInfo||pose.size()!=m.Joints.size()||
    (!RuneAnimation::TargetDepth&&!RunePhysics::InPresentation())||commandline->GetArgInt("","--rune-ik",1)==0)return;
 auto*p=UObject::TryCast<UPawn>(a);bool walking=p && (p->NetworkReplica && replicaGrounded.contains(a)?replicaGrounded.at(a):p->Physics()==PHYS_Walking);if(!p||p->Health()<=0||(!walking && !(RuneAnimation::TargetDepth && RunePhysics::Controls(a) && p->Physics()==PHYS_None))||(!RuneAnimation::TargetDepth && RunePhysics::Controls(a))||p->DrawScale()<=0||p->DrawScale()>2||
   (p->HasProperty("bIsBoss")&&p->GetBool("bIsBoss"))||p->GetStateName()=="Scripting")return;
 auto& rig=GetRig(m,world);if(!rig.valid)return;
 float now=engine->LevelInfo->TimeSeconds();uint64_t hash=1469598103934665603ull;hash=Hash(hash,&world,sizeof(world));hash=Hash(hash,&now,sizeof(now));
 // Hash members rather than struct padding: identical poses from separate draws
 // and attachment queries must reuse the same ground solves.
 for(auto&v:pose){hash=Hash(hash,&v.Position,sizeof(v.Position));hash=Hash(hash,&v.Rotation,sizeof(v.Rotation));hash=Hash(hash,&v.Scale,sizeof(v.Scale));}
 auto& c=cache[a];if(c.ready&&c.model==&m&&c.key==hash){pose=c.pose;++metrics.cacheHits;return;}
 if(c.model!=&m){c=Cache{};c.model=&m;}float dt=c.time<0?1.f:std::clamp(now-c.time,0.f,.1f);float smooth=c.time<0?1.f:1.f-std::exp(-28.f*dt);c.time=now;
 auto original=pose;auto initial=m.JointMatrices(pose);float scale=p->DrawScale();vec3 forward(std::cos(p->Rotation().YawRadians()),std::sin(p->Rotation().YawRadians()),0);
 struct Ground {vec3 ankle,normal;float weight=0;bool found=false;};Ground ground[2];float rootWanted=0;
 for(int side=0;side<2;++side){auto&l=rig.legs[side];auto&g=ground[side];
  bool attached=true;for(int j:{l.hip,l.knee,l.ankle})for(int k=0;k<3;++k)if(std::abs(pose[j].Scale[k])<.01f)attached=false;
  if(!attached){c.offset[side]=0;continue;}
  g.ankle=Point(world*initial[l.ankle]);vec3 hit;
  g.found=p->Trace(hit,g.normal,g.ankle-vec3(0,0,32*scale),g.ankle+vec3(0,0,28*scale),false,vec3(0))&&g.normal.z>.55f;
  if(commandline->HasArg("","--rune-ik-probe"))std::cout<<"IK_GROUND side="<<side<<" ankle="<<g.ankle.x<<","<<g.ankle.y<<","<<g.ankle.z<<" hit="<<hit.x<<","<<hit.y<<","<<hit.z<<" normal="<<g.normal.z<<" found="<<g.found<<std::endl;
  float wanted=0;if(g.found){float gap=g.ankle.z-hit.z-2.5f*scale;float t=std::clamp((gap/scale-6.f)/10.f,0.f,1.f);g.weight=1-t*t*(3-2*t);wanted=std::clamp(-gap,-18.f*scale,18.f*scale)*g.weight;}
  c.offset[side]+=(wanted-c.offset[side])*smooth;if(g.found&&g.weight>.01f)rootWanted=std::min(rootWanted,c.offset[side]);
 }
 // Lower the pelvis enough for the lower foot to reach without stretching.
 // Carry interactions retain their authored hand height until their hand IK is applied.
 if(RuneCorpse::Busy(p)||RunePhysics::Carrying(p))rootWanted=0;
 rootWanted=std::max(rootWanted,-8.f*scale);c.rootOffset+=(rootWanted-c.rootOffset)*smooth;
 vec3 shift(0,0,c.rootOffset),localShift;
 for(int axis=0;axis<3;++axis){vec3 v(world[axis*4],world[axis*4+1],world[axis*4+2]);localShift[axis]=dot(shift,v)/std::max(dot(v,v),1e-10f);}
 pose[0].Position+=localShift;
 for(int side=0;side<2;++side){auto&l=rig.legs[side];auto&g=ground[side];if(!g.found||g.weight<.001f)continue;
  auto bones=m.JointMatrices(pose);auto hip=Point(world*bones[l.hip]),knee=Point(world*bones[l.knee]),ankle=Point(world*bones[l.ankle]);
  auto solved=TwoBone(hip,knee,ankle,g.ankle+vec3(0,0,c.offset[side]),knee+forward*(4*scale));if(!solved.valid)continue;
  Aim(m,pose,world,l.hip,l.knee,solved.joint);Aim(m,pose,world,l.knee,l.ankle,solved.end);
  bones=m.JointMatrices(pose);auto current=Rotation(world*bones[l.ankle]);auto up=RuneRigid::rotate(current,l.soleUp);auto delta=RuneRigid::between(up,g.normal);auto adjusted=RuneRigid::multiply(delta,current);
  auto q0=quaternion(current.x,current.y,current.z,current.w),q1=quaternion(adjusted.x,adjusted.y,adjusted.z,adjusted.w);auto blend=normalize(slerp(q0,q1,g.weight));SetWorldRotation(m,pose,bones,world,l.ankle,{blend.x,blend.y,blend.z,blend.w});
  auto final=m.JointMatrices(pose);float e=std::max(std::abs(length(Point(world*final[l.knee])-Point(world*final[l.hip]))-length(knee-hip)),std::abs(length(Point(world*final[l.ankle])-Point(world*final[l.knee]))-length(ankle-knee)));metrics.maxLengthError=std::max(metrics.maxLengthError,e);metrics.maxCorrection=std::max(metrics.maxCorrection,std::abs(c.offset[side]));++metrics.groundedFeet;
 }
 for(auto&v:pose)if(!Finite(v.Position)||!Finite(v.Scale)){pose=original;break;}
 c.pose=pose;c.key=hash;c.ready=true;++metrics.solves;
}
void Apply(const USkelModel&m,UActor*a,Array<RuneJointPose>&pose){if(a&&UObject::TryCast<UPawn>(a))ApplyAtWorld(m,a,pose,GetRuneMeshToWorld(a));}
void RunProbe(Engine& game,UPlayerPawn* player){
 unsigned cases=0,checks=0;auto check=[&](bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(std::string("character IK: ")+text);};
 bool disabled=commandline->GetArgInt("","--rune-ik",1)==0;float worstFoot=0,worstLength=0;
 bool campaign=commandline->HasArg("","--rune-physics-campaign");
 for(size_t index=0;index<openrune::Characters.size();++index)for(int skin=0;skin<openrune::Characters[index].skins;++skin){
  if(campaign && (index || skin))continue;
  auto*p=campaign?player:game.LoginNetworkPlayer("IKAvatar",-1,int(index),skin);check(p,"roster avatar");p->DisableEvent("PlayerTick");p->DisableEvent("Tick");p->GotoState("","");p->Health()=1000;p->SetPhysics(PHYS_Walking);
  auto*m=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(p));check(m,"skeletal character");auto original=RuneCorpse::ReferencePose(*m);auto world=GetRuneMeshToWorld(p);auto&rig=GetRig(*m,world);
  std::cout<<"RUNE_IK_CASE class="<<openrune::Characters[index].className<<" skin="<<skin<<" supported="<<rig.valid<<std::endl;check(rig.valid,"roster biped legs resolved");
  vec3 floor,normal;check(p->Trace(floor,normal,p->Location()-vec3(0,0,250),p->Location()+vec3(0,0,80),false,vec3(0))&&normal.z>.7f,"standing floor");
  auto before=m->JointMatrices(original);float ankleZ=1e30f;for(auto&leg:rig.legs)ankleZ=std::min(ankleZ,Point(world*before[leg.ankle]).z);
  world[14]+=floor.z+2.5f*p->DrawScale()+5.f*p->DrawScale()-ankleZ;Forget(p);auto posed=original;auto count=Stats().groundedFeet;
  std::cout<<"IK_INPUT physics="<<int(p->Physics())<<" health="<<p->Health()<<" scale="<<p->DrawScale()<<" control="<<RunePhysics::Controls(p)<<" state="<<p->GetStateName().ToString()<<" floor="<<floor.z<<" raw_ankle="<<ankleZ<<" origin="<<p->Location().x<<","<<p->Location().y<<","<<p->Location().z<<std::endl;
  {RuneAnimation::TargetPoseScope target;ApplyAtWorld(*m,p,posed,world);}
  auto final=m->JointMatrices(posed);auto base=m->JointMatrices(original);
  if(!disabled){check(Stats().groundedFeet>=count+2,"both feet grounded");for(auto&leg:rig.legs){
   auto foot=Point(world*final[leg.ankle]);float error=std::abs(foot.z-floor.z-2.5f*p->DrawScale());worstFoot=std::max(worstFoot,error);check(error<.25f,"IK sole reaches floor");
   for(auto pair:{std::pair{leg.hip,leg.knee},std::pair{leg.knee,leg.ankle}}){float a=length(Point(world*base[pair.second])-Point(world*base[pair.first])),b=length(Point(world*final[pair.second])-Point(world*final[pair.first]));worstLength=std::max(worstLength,std::abs(a-b));check(std::abs(a-b)<.05f,"model bone length preserved");}
  }}
  auto repeat=original;auto hits=Stats().cacheHits;{RuneAnimation::TargetPoseScope target;ApplyAtWorld(*m,p,repeat,world);}if(!disabled)check(Stats().cacheHits>hits,"repeated pass reuses ground solve");
  for(size_t j=0;j<posed.size();++j)check(length(posed[j].Position-repeat[j].Position)<1e-6f&&posed[j].Rotation==repeat[j].Rotation,"repeat pose identical");
  p->SetPhysics(PHYS_Falling);auto airborne=original;{RuneAnimation::TargetPoseScope target;ApplyAtWorld(*m,p,airborne,world);}for(size_t j=0;j<original.size();++j)check(airborne[j].Rotation==original[j].Rotation&&length(airborne[j].Position-original[j].Position)<1e-6f,"airborne animation remains authored");
  if(!campaign)p->Destroy();++cases;
 }
 std::cout<<"RUNE_IK_RUNTIME_OK cases="<<cases<<" checks="<<checks<<" disabled="<<disabled<<" worst_sole_error="<<worstFoot<<" worst_length_error="<<worstLength<<std::endl;
}

}
