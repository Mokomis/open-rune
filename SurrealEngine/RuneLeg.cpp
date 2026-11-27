#include "Precomp.h"
#include "RuneLeg.h"
#include "RuneKick.h"
#include "RuneNetwork.h"
#include "RuneCharacters.h"
#include "Utils/Random.h"
#include "RunePhysics.h"
#include "RuneGore.h"
#include "RuneBattleLaws.h"
#include "RuneBloodPresentation.h"
#include "Engine.h"
#include "GameWindow.h"
#include <fstream>
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Core/UFunction.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Inventory/UWeapon.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Render/VisibleRuneEffects.h"
#include "VM/ScriptCall.h"
#include "VM/Frame.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include <map>
#include <set>
#include "Math/coords.h"
#include <tuple>
#include <iostream>

namespace RuneLeg { namespace {
constexpr int QuarterTurn=16384, SupportSteps=16;
constexpr float LegCollisionHeight=15.f;
struct LegFootprint {
 bool ready=false;
 vec3 pivot{};
 float lift[SupportSteps+1]{};
};
struct Anatomy {int hip[2]{-1,-1},knee[2]{-1,-1},ankle[2]{-1,-1};uint16_t groups[2]{};bool valid=false;std::vector<uint8_t> faces[4];LegFootprint foot[2];};
std::map<std::tuple<const USkelModel*,const USkelModel*,int>,Anatomy> rigs;
ULevelInfo* rigLevel=nullptr;
thread_local Contact current,replayed;
int part(int side){return side?8:6;}
vec3 Point(const mat4& m){return {m[12],m[13],m[14]};}
bool descendant(const USkelModel& m,int joint,int parent){for(int n=0;joint>=0 && joint<int(m.Joints.size()) && n<64;++n){if(joint==parent)return true;joint=m.Joints[joint].Parent;}return false;}
Anatomy& anatomy(UActor* a){
 if(rigLevel!=engine->LevelInfo){rigLevel=engine->LevelInfo;rigs.clear();}
 auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(a));auto* geo=GetRuneGeometry(a);int variant=a->GetByte("SkelMesh");
 if(geo && (variant<0 || variant>=int(geo->Meshes.size())))variant=0;
 auto [it,inserted]=rigs.try_emplace({model,geo,variant});auto& r=it->second;if(!inserted || !model || !geo || geo->Meshes.empty())return r;
 for(size_t j=0;j<model->Joints.size();++j)for(int s=0;s<2;++s){
  if(model->Joints[j].Name==(s?"rhip":"lhip"))r.hip[s]=int(j);
  if(model->Joints[j].Name==(s?"rknee":"lknee"))r.knee[s]=int(j);
  if(model->Joints[j].Name==(s?"rankle":"lankle"))r.ankle[s]=int(j);
 }
 if(r.hip[0]<0 || r.hip[1]<0 || r.knee[0]<0 || r.knee[1]<0 || r.ankle[0]<0 || r.ankle[1]<0)return r;
 double total[16]{},leg[2][16]{};const auto& mesh=geo->Meshes[variant];
 for(const auto& tri:mesh.Triangles)if(tri.Group<16 && !(mesh.GroupFlags[tri.Group]&PF_Invisible))for(int index:tri.Index){
  const auto& v=mesh.Vertices[index];++total[tri.Group];
  for(int s=0;s<2;++s){if(descendant(*model,v.Joint[0],r.hip[s]))leg[s][tri.Group]+=v.Weight;if(descendant(*model,v.Joint[1],r.hip[s]))leg[s][tri.Group]+=1-v.Weight;}
 }
 for(int g=0;g<16;++g)if(total[g])for(int s=0;s<2;++s)
  if(leg[s][g]/total[g]>.65 && leg[1-s][g]/total[g]<.1)r.groups[s]|=uint16_t(1u<<g);
 unsigned counts[2]{};
 for(auto& faces:r.faces)faces.reserve(mesh.Triangles.size());
 for(const auto& tri:mesh.Triangles){float weight[2]{};for(auto index:tri.Index){auto& v=mesh.Vertices[index];for(int side=0;side<2;++side){if(descendant(*model,v.Joint[0],r.hip[side]))weight[side]+=v.Weight;if(descendant(*model,v.Joint[1],r.hip[side]))weight[side]+=1-v.Weight;}}
  int side=weight[0]>1.8f && weight[1]<.9f?0:weight[1]>1.8f && weight[0]<.9f?1:-1;
  if(side>=0)++counts[side];r.faces[0].push_back(side!=0);r.faces[1].push_back(side!=1);r.faces[2].push_back(side==0);r.faces[3].push_back(side==1);
 }
 r.valid=model->Joints.size()<=48 && counts[0] && counts[1];return r;
}
bool protectedPawn(UPawn* p){
 return !p || p->NetworkReplica || p->Health()<=0 || p->bDeleteMe() || p->bHidden() ||
  !(p->IsA("RunePlayer") || p->IsA("ScriptPawn")) || p->GetStateName()=="Scripting" ||
  (p->HasProperty("bIsBoss") && p->GetBool("bIsBoss")) ||
  (UObject::TryCast<UPlayerPawn>(p) && UObject::Cast<UPlayerPawn>(p)->ViewTarget());
}
bool hidden(UActor* a,const Anatomy& r,int side){
 auto* flags=static_cast<int*>(a->GetProperty("SkelGroupFlags"));for(int g=0;g<16;++g)if((r.groups[side]&(1u<<g)) && !(flags[g]&PF_Invisible))return false;return r.groups[side]!=0;
}
// Slots48/49 are unused by the supported38-joint roster. Their values are
// already saved and reliably replicated; never repurpose an actual bone flag.
int marker(UActor* a){
 if(!a || !a->HasProperty("JointFlags"))return 0;auto* m=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(a));if(!m || m->Joints.size()>48)return 0;
 auto* flags=static_cast<uint8_t*>(a->GetProperty("JointFlags"));return flags[48]==0xa7 && flags[49]>=1 && flags[49]<=4?flags[49]:0;
}
void mark(UActor* a,int value){auto* flags=static_cast<uint8_t*>(a->GetProperty("JointFlags"));flags[48]=0xa7;flags[49]=uint8_t(value);}
int loose(UActor* a){int value=marker(a);return a && a->IsA("Gib") && value>=3?value-3:-1;}
UActor* detach(UPawn* p,int side){
 const auto r=anatomy(p);auto* model=UObject::Cast<USkelModel>(GetRuneDisplayMesh(p));auto bones=model->ActorJointMatrices(p);auto at=GetRuneMeshToWorld(p)*bones[r.knee[side]];
 auto* limb=p->Spawn(engine->packages->FindClass("RuneI.GibRedMedium"),{},{},Point(at),p->Rotation());if(!limb)return nullptr;
 limb->SetBase(nullptr,true);limb->SetObject("Skeletal",p->GetUObject("Skeletal"));limb->SetObject("SubstituteMesh",p->GetUObject("SubstituteMesh"));limb->SetByte("SkelMesh",p->GetByte("SkelMesh"));limb->DrawScale()=p->DrawScale();
 auto* source=static_cast<int*>(p->GetProperty("SkelGroupFlags"));auto* flags=static_cast<int*>(limb->GetProperty("SkelGroupFlags"));
 auto** from=static_cast<UObject**>(p->GetProperty("SkelGroupSkins"));auto** skins=static_cast<UObject**>(limb->GetProperty("SkelGroupSkins"));
 for(int g=0;g<16;++g){flags[g]=source[g];skins[g]=from[g];}
 limb->SetCollision(false,false,false);limb->SetCollisionSize(8,LegCollisionHeight);limb->bCollideWorld()=true;
 limb->GotoState("FallingGib","Begin");limb->AnimSequence()="neutral_idle";limb->AnimFrame()=0;limb->AnimRate()=0;limb->bAnimLoop()=false;
 mark(limb,3+side);
 limb->bFixedRotationDir()=false;limb->RotationRate()={0,0,0};
 limb->SetRotation(Rotator(0,p->Rotation().Yaw,0));
 limb->Velocity()=p->Velocity()*.3f+vec3(0,0,100);limb->SetPhysics(PHYS_Falling);limb->LifeSpan()=25;
 return limb;
}
void sever(Contact c,bool finisher=false){
 auto* p=c.target;auto r=anatomy(p);if(!r.valid)return;
 // Create the detached representation first; failed allocation leaves the leg intact.
 auto* limb=detach(p,c.side);if(!limb)return;
 static_cast<int*>(p->GetProperty("BodyPartHealth"))[part(c.side)]=Severed;mark(p,c.side+1);
 auto* flags=static_cast<int*>(p->GetProperty("SkelGroupFlags"));for(int g=0;g<16;++g)if(r.groups[c.side]&(1u<<g))flags[g]|=PF_Invisible;
 auto* joints=static_cast<uint8_t*>(p->GetProperty("JointFlags"));auto* model=UObject::Cast<USkelModel>(GetRuneDisplayMesh(p));
 for(size_t j=0;j<model->Joints.size() && j<50;++j)if(descendant(*model,int(j),r.hip[c.side]))joints[j]&=~uint8_t(1);
 if(finisher)return; // Death already owns the victim's animation and physics.
 p->RuneKickActive=p->RuneKickPending=false;
 auto* proxy=UObject::TryCast<UActor>(p->GetUObject("AnimProxy"));if(proxy)proxy->GotoState("Idle","Begin");
 if(p->GetStateName()=="Uninterrupted")p->GotoState(UObject::TryCast<UPlayerPawn>(p)?"PlayerWalking":"Fighting","Begin");
 if(p->Physics()==PHYS_None)p->SetPhysics(PHYS_Walking);
 if(p->AnimSequence()=="neutral_kick")p->LoopAnim("neutral_idle",1,.1f,0);
 LogMessage("RUNE_KICK_COUNTER actor="+p->Class->Name.ToString()+" side="+std::to_string(c.side)+" health="+std::to_string(p->Health())+" limb="+limb->Name.ToString());
}
}
void SeverForFinisher(UPawn* victim,int side){
 if(side>=0 && side<=1 && !protectedPawn(victim) && !marker(victim))sever({nullptr,nullptr,victim,side},true);
}
int Missing(UActor* a){
 auto* p=UObject::TryCast<UPawn>(a);if(!p || p->Health()<=0 || !p->HasProperty("BodyPartHealth"))return -1;
 int value=marker(p);return value==1 || value==2?value-1:-1;
}
void ReplicaAppearance(UActor* a){
 auto* p=UObject::TryCast<UPawn>(a);if(!p || !p->NetworkReplica)return;auto r=anatomy(p);if(!r.valid)return;
 auto* parts=static_cast<int*>(p->GetProperty("BodyPartHealth"));int value=marker(p);
 for(int side=0;side<2;++side){if(value==side+1)parts[part(side)]=Severed;else if(parts[part(side)]==Severed)parts[part(side)]=0;}
}
bool Detached(UActor* a){return loose(a)>=0;}
const std::vector<uint8_t>* TriangleMask(UActor* a){int value=marker(a);if(!value)return nullptr;auto& r=anatomy(a);return r.valid?&r.faces[value-1]:nullptr;}
float SpeedScale(UPawn* p){return Missing(p)>=0?.6f:1.f;}
Contact Exposure(UActor* weapon,UPawn* target,uint32_t low,uint32_t high,vec3 hit){
 Contact c;auto* attacker=weapon?UObject::TryCast<UPawn>(weapon->Owner()):nullptr;
 if(!RuneBattleLaws::Allowed(RuneBattleLaws::Kick) || protectedPawn(target) || Missing(target)>=0 || !attacker || attacker==target || !weapon->IsA("Sword") || weapon->GetStateName()!="Swinging" || !target->RuneKickActive || (target->Physics()!=PHYS_Walking && target->Physics()!=PHYS_Falling && target->Physics()!=PHYS_None))return c;
 auto* pose=static_cast<UActor*>(target);auto* proxy=UObject::TryCast<UActor>(target->GetUObject("AnimProxy"));if(pose->AnimSequence()!="neutral_kick" && proxy && proxy->AnimSequence()=="neutral_kick")pose=proxy;
 if(pose->AnimSequence()!="neutral_kick" || pose->AnimFrame()<.12f || pose->AnimFrame()>.9f)return c;
 auto r=anatomy(target);if(!r.valid)return c;auto* model=UObject::Cast<USkelModel>(GetRuneDisplayMesh(target));auto bones=model->ActorJointMatrices(target);auto world=GetRuneMeshToWorld(target);
 vec3 f(std::cos(target->Rotation().YawRadians()),std::sin(target->Rotation().YawRadians()),0);float best=0;int chosen=-1;
 size_t idle=0;for(size_t i=0;i<model->Sequences.size();++i)if(model->Sequences[i].Info.Name=="neutral_idle"){idle=i;break;}
 auto rest=model->JointMatrices(model->DecodeFrame(idle,0));float scale=target->DrawScale();
 for(int side=0;side<2;++side){
  auto hip=Point(world*bones[r.hip[side]]),ankle=Point(world*bones[r.ankle[side]]),knee=Point(world*bones[r.knee[side]]);
  float reach=dot(ankle-hip,f),raised=(ankle-hip).z-(Point(world*rest[r.ankle[side]])-Point(world*rest[r.hip[side]])).z;
  float kneeReach=dot(knee-hip,f);
  // A lifted, projecting knee exposes its shin during extension as well as
  // at full reach. A folded/loading foot and the planted support leg do not.
  if(reach>12.f*scale || (raised>10.f*scale && kneeReach>5.f*scale)){
   float score=std::max(reach,raised+std::max(kneeReach,0.f));if(score>best){best=score;chosen=side;}
  }
 }
 if(chosen<0)return c;
 auto bit=[&](int j){return j<32 ? (low&(1u<<j))!=0:(j<64 && (high&(1u<<(j-32)))!=0);};
 if(!bit(r.knee[chosen]) && !bit(r.ankle[chosen]) && !bit(r.ankle[chosen]+1))return c;
 // The native swept joint boxes already prove contact with this leg.
 // HitLoc is the sweep's nearest contact, shared by all its joint masks; a
 // second point-distance test wrongly rejects valid multi-joint sword cuts.
 if(!std::isfinite(hit.x+hit.y+hit.z))return c;
 return {weapon,attacker,target,chosen};
}
ReplayScope::ReplayScope(Contact c):previous(replayed){replayed=c;}
ReplayScope::~ReplayScope(){replayed=previous;}
WeaponScope::WeaponScope(UFunction* fn,UObject* obj,const ArrayView<ExpressionValue>& args):previous(current){
 if(fn->Name!="DoWeaponSwipe")return;current={};auto* w=UObject::TryCast<UActor>(obj);if(!w || args.size()!=6)return;
 auto* p=UObject::TryCast<UPawn>(args[0].ToObject());if(!p)return;
 current=replayed.weapon==w && replayed.target==p?replayed:Exposure(w,p,uint32_t(args[1].ToInt()),uint32_t(args[2].ToInt()),args[3].ToVector());
}
WeaponScope::~WeaponScope(){current=previous;}
bool Observing(){return current.side>=0;}
void AfterDamage(UPawn* p,UPawn* attacker,int damage,int before,int joint){
 if(current.target!=p || current.attacker!=attacker || current.side<0 || protectedPawn(p) || Missing(p)>=0 || damage<8 || !current.weapon || before-p->Health()<=0 || !RuneBattleLaws::Allowed(RuneBattleLaws::Kick))return;
 if(RuneDualWield::Blocks(p,attacker))return;
 // The real weapon script has already admitted this damage. No random roll,
 // duplicate damage or torso-hit promotion; health/shield/team/rage rules stand.
 auto r=anatomy(p);if(!r.valid || !descendant(*UObject::Cast<USkelModel>(GetRuneDisplayMesh(p)),joint,r.hip[current.side]))return;
 sever(current);current={};
}
void AfterScriptCall(UFunction* f,UObject* object,const ArrayView<ExpressionValue>& args){
 if(f->Name!="InitFor" || args.size()<1)return;auto* body=UObject::TryCast<UActor>(object);auto* source=UObject::TryCast<UPawn>(args[0].ToObject());
 if(!body || !body->IsA("Carcass") || !source || body->XLevel()!=source->XLevel())return;
 int value=marker(source);if(value!=1 && value!=2)return;mark(body,value);auto r=anatomy(body);if(!r.valid)return;
 auto* m=UObject::Cast<USkelModel>(GetRuneDisplayMesh(body));auto* flags=static_cast<uint8_t*>(body->GetProperty("JointFlags"));
 for(size_t j=0;j<m->Joints.size() && j<48;++j)if(descendant(*m,int(j),r.hip[value-1]))flags[j]&=~uint8_t(1);
}
void ApplyPose(const USkelModel& m,UActor* a,Array<RuneJointPose>& pose){
 int lost=Missing(a);auto* p=UObject::TryCast<UPawn>(a);if(lost<0 || !p || RunePhysics::Controls(p) || p->GetStateName()=="Scripting" || (p->Physics()!=PHYS_Walking && !(p->NetworkReplica && p->Physics()==PHYS_None)))return;
 auto r=anatomy(a);if(!r.valid || pose.empty())return;
 float speed=length(p->Velocity().xy()),amount=std::clamp(speed/50.f,0.f,1.f);if(amount<=0)return;
 // Presentation hops keep the original swept capsule and stair traversal;
 // head/weapon bones receive the same interpolated whole-body lift.
 float wave=std::max(0.f,std::sin(float(engine->LevelInfo->TimeSeconds())*15.f));float lift=wave*wave*10.f*amount;
 auto world=GetRuneMeshToWorld(a);vec3 shift(0,0,lift*p->DrawScale()),local;
 for(int axis=0;axis<3;++axis){vec3 v(world[axis*4],world[axis*4+1],world[axis*4+2]);local[axis]=dot(shift,v)/std::max(dot(v,v),1e-8f);}
 pose[0].Position+=local;int good=1-lost;
 // Replace the surviving leg's walking cycle with a short springing stance.
 size_t idle=0;for(size_t i=0;i<m.Sequences.size();++i)if(m.Sequences[i].Info.Name=="neutral_idle"){idle=i;break;}
 auto rest=m.DecodeFrame(idle,0);
 for(int j:{r.hip[good],r.knee[good],r.ankle[good]})pose[j]=BlendRunePose(pose[j],rest[j],amount);
 pose[r.knee[good]].Rotation.Roll+=int((1-wave)*1100.f*amount);
}
bool LandDetached(UActor* leg,UActor* support){
 if(leg->NetworkReplica || !Detached(leg))return false;
 leg->GotoState("","");
 leg->SetPhysics(PHYS_Rotating);
 leg->SetBase(support,true);
 leg->Velocity()=leg->Acceleration()=vec3(0);
 leg->RotationRate()=Rotator(0,0,0);
 // The actor timer and angular velocity already round-trip through campaign saves.
 leg->TimerCounter()=0;leg->TimerRate()=.22f;leg->bTimerLoop()=false;
 return true;
}
bool TipDetached(UActor* leg,float dt){
 int side=loose(leg);if(side<0)return false;
 if(leg->NetworkReplica || leg->TimerRate()>0)return true;
 // Gravity accelerates the tip; authority sends normal actor angles to clients.
 float speed=std::min(75000.f,float(leg->RotationRate().Roll)+180000.f*dt);
 leg->RotationRate().Roll=int(speed);
 int old=std::abs(int(int16_t(leg->Rotation().Roll)));
 int angle=std::min(QuarterTurn,old+int(std::lround(speed*dt)));
 leg->Rotation().Roll=side?angle:-angle;
 if(angle==QuarterTurn){leg->RotationRate().Roll=0;leg->SetPhysics(PHYS_None);}
 return true;
}
bool LooseTransform(UActor* a,mat4& out){
 int side=loose(a);if(side<0)return false;
 auto& rig=anatomy(a);if(!rig.valid)return false;
 auto* model=UObject::Cast<USkelModel>(GetRuneDisplayMesh(a));
 auto basis=Coords::Rotation(model->RotationOffset).ToMatrix();
 auto& foot=rig.foot[side];
 if(!foot.ready){
  auto* geo=GetRuneGeometry(a);int variant=a->GetByte("SkelMesh");
  if(variant>=int(geo->Meshes.size()))variant=0;
  const auto& mesh=geo->Meshes[variant];size_t seq=0;
  for(size_t i=0;i<model->Sequences.size();++i)if(model->Sequences[i].Info.Name=="neutral_idle"){seq=i;break;}
  auto bones=model->JointMatrices(model->DecodeFrame(seq,0));
  std::vector<vec3> points;
  vec3 low(1e20f),high(-1e20f);std::vector<bool> seen(mesh.Vertices.size());
  for(size_t t=0;t<mesh.Triangles.size();++t)if(rig.faces[2+side][t])for(auto i:mesh.Triangles[t].Index){
   if(seen[i])continue;seen[i]=true;
   const auto& v=mesh.Vertices[i];
   auto q=(bones[v.Joint[0]]*vec4(v.Point[0],1))*v.Weight;
   if(v.Weight!=1)q+=(bones[v.Joint[1]]*vec4(v.Point[1],1))*(1-v.Weight);
   auto point=(basis*q).xyz();points.push_back(point);
   for(int k=0;k<3;++k){low[k]=std::min(low[k],point[k]);high[k]=std::max(high[k],point[k]);}
  }
  if(points.empty())return false;
  foot.pivot=(low+high)*.5f;foot.pivot.z=low.z;
  for(int i=0;i<=SupportSteps;++i){
   auto tilt=Coords::Rotation(Rotator(0,0,(side?1:-1)*i*QuarterTurn/SupportSteps)).ToMatrix();
   float bottom=1e20f;
   for(auto p:points)bottom=std::min(bottom,(tilt*vec4(p-foot.pivot,0)).z);
   foot.lift[i]=-bottom;
  }
  foot.ready=true;
 }
 // Rotate about the foot. The mesh support height keeps the boot and cut end
 // on the floor as they take turns bearing the weight.
 float fraction=std::min(1.f,std::abs(float(int16_t(a->Rotation().Roll)))/QuarterTurn)*SupportSteps;
 int i=std::min(int(fraction),SupportSteps-1);
 float lift=foot.lift[i]+(foot.lift[i+1]-foot.lift[i])*(fraction-i);
 vec3 at=a->Location()+vec3(0,0,lift*a->DrawScale()-LegCollisionHeight);
 out=mat4::translate(at)*Coords::Rotation(a->Rotation()).ToMatrix()*mat4::scale(a->DrawScale())*mat4::translate(-foot.pivot)*basis;
 return true;
}
namespace {
struct Wound {UPawn* pawn=nullptr;double dropAt=0,markAt=0;};
struct Drop {vec3 at,velocity,color;double born;};
std::map<int,Wound> wounds;std::vector<Drop> drops;ULevelInfo* presentationLevel=nullptr;double last=0;
}
void Draw(VisibleFrame* frame){
 if(frame->ShadowView || frame->PortalDepth || frame->MirrorFlag || !engine->LaunchInfo.IsRune())return;
 double now=engine->TotalTime;if(presentationLevel!=engine->LevelInfo || now<last){presentationLevel=engine->LevelInfo;wounds.clear();drops.clear();}last=now;
 std::erase_if(drops,[&](const Drop& d){return now-d.born>1.2 || now<d.born;});
 std::set<int> seen;auto texture=RuneEffects::Texture(0,false);int count=0;
 for(auto* a:engine->Level->Actors){auto* p=UObject::TryCast<UPawn>(a);int side=Missing(p);if(side<0 || p->bHidden() || length(p->Location()-frame->ViewLocation.xyz())>1200 || ++count>24)continue;
  seen.insert(p->Index);auto& w=wounds[p->Index];if(w.pawn!=p)w={p,now,now};auto r=anatomy(p);if(!r.valid)continue;auto* m=UObject::Cast<USkelModel>(GetRuneDisplayMesh(p));auto bones=m->ActorJointMatrices(p);auto world=GetRuneMeshToWorld(p);auto hip=Point(world*bones[r.hip[side]]),knee=Point(world*bones[r.knee[side]]);auto axis=knee-hip;if(length(axis)<.01f)continue;axis=normalize(axis);
  vec3 u=normalize(std::abs(axis.z)<.9f?cross(axis,vec3(0,0,1)):cross(axis,vec3(0,1,0))),v=cross(axis,u);vec3 color=RuneBloodPresentation::BloodColor();
  auto name=p->Class->Name.ToString();if(name.find("Sark")!=std::string::npos)color={.055f,.24f,.025f};
  if(frame->EffectQuadBudget>0)RuneEffects::Quad(frame,texture,hip+axis*2.f,u*(6.f*p->DrawScale()),v*(7.f*p->DrawScale()),color*.65f,1,PF_Highlighted);
  if(now>=w.dropAt){w.dropAt=now+.3;if(drops.size()<48)drops.push_back({hip+axis*3.f,axis*18.f+p->Velocity()*.1f+vec3(0,0,-25),color,now});}
  if(now>=w.markAt){w.markAt=now+(length(p->Velocity().xy())>15?.6:1.5);RuneGore::BleedMark(hip,color);}
 }
 std::erase_if(wounds,[&](const auto& w){return !seen.contains(w.first);});
 for(const auto& d:drops){if(frame->EffectQuadBudget<=0)break;float age=float(now-d.born);auto at=d.at+d.velocity*age+vec3(0,0,-180)*age*age;float opacity=std::min(1.f,age*15.f)*std::clamp((1.2f-age)*3.f,0.f,1.f);RuneEffects::Quad(frame,texture,at,frame->ViewRotation.YAxis*.7f,frame->ViewRotation.ZAxis*1.6f,d.color,opacity,PF_Highlighted);}
}
void Demo(Engine& game,int frame){
 if(!commandline->HasArg("","--kick-counter-demo"))return;
 if(!commandline->HasArg("","--save-directory"))throw std::runtime_error("Kick counter demo requires isolated saves");
 static UPawn* victim=nullptr;static UActor* sword=nullptr;static vec3 lane,dir,wall,camera,look;static bool ready=false;static vec3 start;
 auto* p=game.viewport->Actor();
 if(frame==90){
  if(!p || !RunePhysics::FindImpactLane(p,lane,dir,wall))throw std::runtime_error("No kick counter demo lane");
  lane-=dir*110.f;auto side=vec3(-dir.y,dir.x,0);
  p->SetLocation(lane);p->SetRotation(Rotator::FromVector(dir));p->ViewRotation()=p->Rotation();p->DesiredRotation()=p->Rotation();p->Health()=1000;p->SetPhysics(PHYS_Walking);p->GotoState("PlayerWalking","Begin");p->DisableEvent("PlayerTick");
  victim=UObject::TryCast<UPawn>(p->Spawn(game.packages->FindClass("RuneI.DarkViking"),{},{},lane+dir*80.f,Rotator::FromVector(-dir)));
  if(!victim)throw std::runtime_error("Demo victim");victim->SetCollision(true,false,false);p->SetCollision(true,false,false);
  victim->Health()=1000;victim->SetPhysics(PHYS_Walking);victim->Enemy()=p;victim->GotoState("Fighting","Begin");victim->DesiredRotation()=victim->Rotation();
  for(const char* e:{"Tick","Timer","SeePlayer","HearNoise","EnemyNotVisible","Bump"})victim->DisableEvent(e);
  victim->StateFrame->SetLatentState(LatentRunState::Stop);victim->Velocity()=victim->Acceleration()=vec3(0);victim->RuneKickThink=victim->RuneBashThink=10000;
  RuneTactics::Enabled=false;RuneDualWield::Enabled=false;auto* item=p->Spawn(game.packages->FindClass("RuneI.VikingShortSword"),{},{},lane+vec3(0,0,100),{});if(!item)throw std::runtime_error("Demo sword");
  item->SetFloat("RespawnTime",0);sword=UObject::TryCast<UActor>(CallEvent(item,"SpawnCopy",{ExpressionValue::ObjectValue(p)}).ToObject());CallEvent(p,"AcquireInventory",{ExpressionValue::ObjectValue(sword)});sword->GotoState("Active","Begin");CallEvent(p,"SwitchWeapon",{ExpressionValue::ByteValue(2)});
  // Fixed authored camera observes both the cut and subsequent one-legged gait.
  camera=lane+side*180.f+dir*42.f+vec3(0,0,15);look=lane+dir*50.f+vec3(0,0,5);
  if(!p->FastTrace(camera,look)){camera=lane-side*180.f+dir*42.f+vec3(0,0,15);if(!p->FastTrace(camera,look)){camera=lane-dir*130.f+side*85.f+vec3(0,0,35);}}
  ready=true;LogMessage("KICK_COUNTER_DEMO_READY");
 }
 if(!ready)return;
 if(victim->StateFrame && !victim->RuneKickActive)victim->StateFrame->SetLatentState(LatentRunState::Stop);
 if(frame==180){if(!RuneKick::Start(victim))throw std::runtime_error("Demo kick failed");}
 if(frame==198)CallEvent(p->GetUObject("AnimProxy"),"Attack");
 if(frame==300){
  // Deterministic presentation setup if the NPC's defensive script altered
  // this replay: use the real weapon script and a genuinely exposed knee.
  if(Missing(victim)<0){
   victim->SetPhysics(PHYS_Walking);victim->RuneKickActive=true;victim->AnimSequence()="neutral_kick";victim->AnimFrame()=.6f;victim->Velocity()=victim->Acceleration()=vec3(0);
   auto r=anatomy(victim);auto* m=UObject::Cast<USkelModel>(GetRuneDisplayMesh(victim));auto hit=Point(GetRuneMeshToWorld(victim)*m->ActorJointMatrices(victim)[r.knee[1]]);sword->GotoState("Swinging","");
   CallEvent(sword,"DoWeaponSwipe",{ExpressionValue::ObjectValue(victim),ExpressionValue::IntValue(1u<<r.knee[1]),ExpressionValue::IntValue(0),ExpressionValue::VectorValue(hit),ExpressionValue::VectorValue(vec3(0,0,1)),ExpressionValue::VectorValue(vec3(0))});LogMessage("KICK_COUNTER_DEMO_PRESENTATION_SETUP");
  }
  if(Missing(victim)<0)throw std::runtime_error("Demo sever did not occur");
  start=victim->Location();victim->SetPhysics(PHYS_Walking);victim->GotoState("Fighting","");victim->StateFrame->SetLatentState(LatentRunState::Stop);for(const char* e:{"Tick","Timer","SeePlayer","HearNoise","EnemyNotVisible","Bump"})victim->DisableEvent(e);victim->DesiredSpeed()=.7f;victim->LoopAnim("neutral_idle",1,.1f,0);
 }
 if(frame>=310 && frame<620){
  // Ordinary swept movement back and forth in the already checked lane.
  float direction=((frame-310)/120)%2?1.f:-1.f;
  victim->Acceleration()=dir*(direction*400.f);victim->SetPhysics(PHYS_Walking);
 }
 if(frame==620){victim->Acceleration()=victim->Velocity()=vec3(0);LogMessage("KICK_COUNTER_DEMO_HOP distance="+std::to_string(length(victim->Location()-start)));}
 game.CameraLocation=camera;game.CameraRotation=Rotator::FromVector(look-camera);
 if(commandline->HasArg("","--kick-counter-captures"))for(int capture:{90,215,270,301,350,375,400,450,500,550,620})if(frame==capture){
  auto* device=game.window->GetRenderDevice();int w=device->GetRenderWidth(),h=device->GetRenderHeight();Array<TextureColor> pixels;pixels.resize(size_t(w)*h);bool gamma=device->GammaCorrectScreenshots;device->GammaCorrectScreenshots=true;device->ReadPixels(pixels.data());device->GammaCorrectScreenshots=gamma;
  std::string path=commandline->GetArg("","--kick-counter-captures","")+"-"+std::to_string(frame)+".ppm";std::ofstream file(path,std::ios::binary);file<<"P6\n"<<w<<" "<<h<<"\n255\n";for(auto c:pixels){file.put(char(c.R));file.put(char(c.G));file.put(char(c.B));}
 }
}

#include "RuneLegProbe.inc"
}
