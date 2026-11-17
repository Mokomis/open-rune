#include "Precomp.h"
#include "RuneSpider.h"
#include "RuneBattleLaws.h"
#include "RuneLeg.h"
#include "RuneKick.h"
#include "RuneIK.h"
#include "Physics/RunePhysicsWorld.h"
#include "RunePhysics.h"
#include "RuneCorpse.h"
#include "RuneCharacters.h"
#include "RuneNetwork.h"
#include "RuneBot.h"
#include "Engine.h"
#include "GameWindow.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Core/UFunction.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Inventory/UWeapon.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "Collision/TopLevel/CollisionHit.h"
#include "VM/ScriptCall.h"
#include "VM/Frame.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include <iostream>
#include <fstream>

namespace RuneSpider {namespace {
constexpr float Speed=105,SideSpeed=85;
vec3 Facing(UPawn* p){auto r=UObject::TryCast<UPlayerPawn>(p);float yaw=r?r->ViewRotation().YawRadians():p->Rotation().YawRadians();return {std::cos(yaw),std::sin(yaw),0};}
vec3 Point(const mat4& m){return {m[12],m[13],m[14]};}
CollisionHit Trace(UPawn* p,vec3 from,vec3 to,vec3 extent=vec3(0),bool actors=false){TraceFlags f;f.world=f.movers=true;f.pawns=actors;return p->XLevel()->Collision.TraceFirstHit(from,to,p,extent,f);}
vec3 Extent(UPawn* p){return {p->CollisionRadius(),p->CollisionRadius(),p->CollisionHeight()};}
bool SafeZone(UPawn* p,vec3 at){auto* z=engine->LevelInfo->GetLocZone(at,{}).Zone;return z && !z->bWaterZone() && !z->bPainZone() && !z->IsA("HeadBallZone");}
bool Surface(UPawn* p,const CollisionHit& h){
 if(h.Fraction>=1 || h.Actor!=engine->LevelInfo || std::abs(h.Normal.z)>.2f || !h.Node)return false;
 int surf=h.Node->Surf;if(surf<0 || size_t(surf)>=p->XLevel()->Model->Surfaces.size())return false;
 return !(p->XLevel()->Model->Surfaces[surf].PolyFlags&(PF_Invisible|PF_Portal|PF_FakeBackdrop|PF_NotSolid));
}
bool Wall(UPawn* p,vec3 at,vec3 direction,vec3& normal){
 if(length(direction.xy())<.1f)return false;direction=normalize(vec3(direction.x,direction.y,0));
 auto from=at+vec3(0,0,p->CollisionHeight()*.35f),to=from+direction*(p->CollisionRadius()+18.f);auto h=Trace(p,from,to);
 if(!Surface(p,h)||dot(h.Normal,direction)>-.65f)return false;
 auto high=Trace(p,from+vec3(0,0,12),to+vec3(0,0,12));if(!Surface(p,high)||dot(h.Normal,high.Normal)<.9f)return false;
 normal=normalize(vec3(h.Normal.x,h.Normal.y,0));return true;
}
bool Clear(UPawn* p,vec3 a,vec3 b){return Trace(p,a,b,Extent(p),true).Fraction>=1;}
bool ExitPath(UPawn* p,vec3 at,vec3 normal,float maximum,vec3 (&path)[3]){
 float radius=p->CollisionRadius(),height=p->CollisionHeight();
 for(float rise=16;rise<=maximum;rise+=16){
  vec3 up=at+vec3(0,0,rise),across=up-normal*(2*radius+20.f);
  if(!Clear(p,at,up))break;if(!Clear(p,up,across))continue;
  auto bottom=across-vec3(0,0,2*height+24.f);auto h=Trace(p,across,bottom,{radius*.8f,radius*.8f,0});
  if(h.Fraction>=1 || h.Actor!=engine->LevelInfo || h.Normal.z<.7f)continue;
  auto floor=across+(bottom-across)*h.Fraction;vec3 landing=floor+vec3(0,0,height+2.f);
  if(landing.z<=at.z+8 || landing.z>up.z+1 || !SafeZone(p,landing) || !Clear(p,across,landing))continue;
  path[0]=up;path[1]=across;path[2]=landing;return true;
 }return false;
}
bool ContinuousExit(UPawn* p,vec3 at,vec3 normal,vec3 (&path)[3]){
 if(!ExitPath(p,at,normal,600,path))return false;
 // A clear top alone is insufficient: an arch/window can interrupt the wall
 // far below it. Only bots plan a route if hand contact continues to mantle reach.
 for(float rise=0;rise<path[0].z-at.z;rise+=8){
  vec3 local[3],n;auto position=at+vec3(0,0,rise);
  if(ExitPath(p,position,normal,2*p->CollisionHeight()+32,local))return true;
  if(!Wall(p,position,-normal,n)||dot(n,normal)<.95f)return false;
 }
 return true;
}
void Animate(UPawn* p){
 auto& s=p->Spider;
 // Alternate authored hand/foot steps while traversing the wall. The two-hand
 // ledge pull-up is a separate finish used only after finding a real top.
 const char* sequence=s.mantle?(s.step==0?"intropullupA":"pullupTest"):"ClimbUpRope";
 auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(p));float rate=1;
 if(s.mantle && model){float seconds=s.step==0?length(s.path[0]-p->Location())/180.f:(length(s.path[1]-s.path[0])+length(s.path[2]-s.path[1]))/180.f;
  for(auto& clip:model->Sequences)if(clip.Info.Name==sequence)rate=std::clamp(float(clip.Info.NumFrames)/std::max(1.f,clip.Info.Rate)/std::max(.2f,seconds),.25f,4.f);
 }
 auto play=[&](UActor* a){if(!a)return;
  if(s.mantle){if(a->AnimSequence()!=sequence || a->bAnimLoop())a->PlayAnim(sequence,rate,.08f);}
  else {if(a->AnimSequence()!=sequence || !a->bAnimLoop())a->LoopAnim(sequence,1,0,0);
   a->AnimFrame()=s.phase-std::floor(s.phase);a->AnimRate()=0;a->bAnimLoop()=true;}
 };
 play(p);play(UObject::TryCast<UActor>(p->GetUObject("AnimProxy")));
}
void Restore(UPawn* p){auto& s=p->Spider;for(int i=0;i<3;++i)if(auto* a=s.hidden[i];a&&!a->bDeleteMe()&&a->Owner()==p)a->bHidden()=s.wasHidden[i];}
}
bool Busy(UActor* a){auto* p=UObject::TryCast<UPawn>(a);return p && p->Spider.active && p->Health()>0;}
bool Eligible(UPawn* p){
 if(!p || !engine->LaunchInfo.IsRune() || !RuneBattleLaws::Allowed(RuneBattleLaws::Spider) || p->bDeleteMe() || p->bHidden() || p->Health()<=0 || RuneLeg::Missing(p)>=0 || !(p->IsA("RunePlayer")||p->IsA("ScriptPawn")) || (p->HasProperty("bIsBoss")&&p->GetBool("bIsBoss")) || p->GetStateName()=="Scripting" || p->GetStateName()=="EdgeHanging" || p->GetStateName()=="Uninterrupted" || RunePhysics::Controls(p) || RuneBloodlust::Busy(p) || RuneSlam::Busy(p) || RuneCorpse::Busy(p) || RunePhysics::Carrying(p) || p->LadderMode || !SafeZone(p,p->Location()))return false;
 auto* m=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(p));if(!m)return false;
 for(auto name:{"ClimbUpRope","intropullupA","pullupTest"}){bool found=false;for(auto& seq:m->Sequences)if(seq.Info.Name==name){found=true;break;}if(!found)return false;}return true;
}
void Input(UPawn* p,float f,float s){if(!p || !p->Spider.active)return;float n=std::max(1.f,std::hypot(f,s));p->Spider.forward=std::clamp(f/n,-1.f,1.f);p->Spider.strafe=std::clamp(s/n,-1.f,1.f);}
bool Start(UPawn* p){
 if(!Eligible(p)||p->NetworkReplica || p->Spider.active || p->Spider.cooldown>0 || p->Physics()!=PHYS_Falling || p->RuneKickActive || p->RuneBashPhase)return false;
 if(p->IsA("RunePlayer"))if(auto* proxy=p->GetUObject("AnimProxy");proxy && proxy->GetStateName()!="Idle")return false;
 vec3 normal;if(!Wall(p,p->Location(),Facing(p),normal))return false;
 auto& s=p->Spider;bool npc=s.npc;vec3 exit=s.exit;float think=s.think;
 if(p->IsA("RunePlayer"))CallEvent(p,"InstantStow");
 s={};s.npc=npc;s.exit=exit;s.think=0;s.normal=normal;s.last=p->Location();
 for(int i=0;i<3;++i){auto* a=i==0?static_cast<UActor*>(p->Weapon()):i==1?UObject::TryCast<UActor>(p->GetUObject("Shield")):static_cast<UActor*>(p->Dual.offhand);s.hidden[i]=a;if(a){s.wasHidden[i]=a->bHidden();a->bHidden()=true;}}
 RuneIK::Forget(p);CallEvent(p,"ActivateShield",{ExpressionValue::BoolValue(false)});RuneDualWield::Guard(p,false);
 p->SetBase(nullptr,true);p->Velocity()=p->Acceleration()=vec3(0);p->bFire()=p->bAltFire()=p->bDuck()=0;p->SetPhysics(PHYS_None);s.active=true;
 if(auto* proxy=UObject::TryCast<UActor>(p->GetUObject("AnimProxy")))proxy->GotoState("Idle","");
 if(!p->IsA("RunePlayer") && p->StateFrame)p->StateFrame->SetLatentState(LatentRunState::Stop);
 p->SetRotation(Rotator::FromVector(-normal));Animate(p);LogMessage("RUNE_SPIDER_GRAB actor="+p->Name.ToString());return true;
}
void Stop(UPawn* p,bool jump){
 if(!p || !p->Spider.active)return;RuneIK::Forget(p);auto n=p->Spider.normal;Restore(p);p->Spider.active=p->Spider.mantle=false;p->Spider.forward=p->Spider.strafe=0;p->Spider.cooldown=.45f;p->Bloodlust.tap=-1;
 if(p->NetworkReplica || p->bDeleteMe() || p->Health()<=0 || RunePhysics::Controls(p))return;
 if(p->GetStateName()!="Scripting"){
  p->SetPhysics(PHYS_Falling);p->SetBase(nullptr,true);p->Velocity()=jump?n*240.f+vec3(0,0,std::max(220.f,p->JumpZ()*.85f)):vec3(0,0,-20);
  // A hit's original pain/stun state owns its animation and recovery.
  if(p->GetStateName()=="PlayerWalking" || p->GetStateName()=="Fighting" || p->GetStateName()=="Attacking" || p->GetStateName()=="Waiting"){
   p->LoopAnim("neutral_idle",1,.12f,0);if(!p->IsA("RunePlayer"))p->GotoState("Fighting","Begin");
   if(auto* proxy=UObject::TryCast<UActor>(p->GetUObject("AnimProxy"))){proxy->GotoState("Idle","Begin");if(p->IsA("RunePlayer"))CallEvent(proxy,"RetrieveLastHeldWeapon");}
  }
 }
 LogMessage(std::string("RUNE_SPIDER_RELEASE jump=")+(jump?"1":"0")+" actor="+p->Name.ToString());
}
void Damaged(UPawn* p){if(p && !p->NetworkReplica && p->Spider.active){auto momentum=p->Velocity();Stop(p);if(p->Health()>0&&!RunePhysics::Controls(p))p->Velocity()=momentum;}}
bool Plan(UPawn* p,vec3 goal,vec3& normal,vec3& exit){
 if(!Eligible(p) || goal.z<p->Location().z+40 || length((goal-p->Location()).xy())>200)return false;
 if(!Wall(p,p->Location(),normalize(vec3(goal.x-p->Location().x,goal.y-p->Location().y,0)),normal))return false;
 vec3 path[3];if(!ContinuousExit(p,p->Location(),normal,path))return false;
 if(length(path[2]-goal)>length(p->Location()-goal)+24 || path[2].z>goal.z+100)return false;exit=path[2];return true;
}
bool Tick(UPawn* p,float elapsed){
 if(!engine->LaunchInfo.IsRune() || p->NetworkReplica)return false;
 auto& s=p->Spider;float dt=std::clamp(elapsed,0.f,.1f);s.cooldown=std::max(0.f,s.cooldown-dt);
 if(!s.active){
  if(!p->IsA("RunePlayer") && Eligible(p) && p->Enemy() && p->Enemy()->Health()>0){s.think-=dt;if(s.think<=0){s.think=1.2f;vec3 n,exit;if(Plan(p,p->Enemy()->Location(),n,exit)){
   s.npc=true;s.exit=exit;if(p->Physics()==PHYS_Walking){p->SetPhysics(PHYS_Falling);p->Velocity().z=p->JumpZ();}
   p->SetRotation(Rotator::FromVector(-n));Start(p);
  }}}
  return s.active;
 }
 if(!Eligible(p) || (p->Physics()!=PHYS_None) || length(p->Location()-s.last)>48){Stop(p);return false;}
 s.age+=dt;s.think=std::max(0.f,s.think-dt);if(s.npc){s.forward=1;s.strafe=0;if(s.age>9 || s.stalled>1){Stop(p);return false;}}
 p->Velocity()=p->Acceleration()=vec3(0);p->bFire()=p->bAltFire()=0;return true;
}
void Move(UPawn* p,float elapsed){
 auto& s=p->Spider;if(!s.active || p->NetworkReplica)return;float dt=std::clamp(elapsed,0.f,.05f);vec3 before=p->Location();
 if(!s.mantle){
  if(p->bDuck()){Stop(p);return;}
  // A hand-height ray can clear the wall before the cylinder reaches its top.
  // Bound ordinary top searches to 10 Hz; recheck immediately on lost contact.
  bool probe=s.forward>.1f && s.think<=0;if(probe)s.think=.1f;
  bool exit=probe && ExitPath(p,p->Location(),s.normal,2*p->CollisionHeight()+32,s.path);
  if(!exit){vec3 n;bool contact=Wall(p,p->Location(),-s.normal,n)&&dot(n,s.normal)>=.85f;
   if(!contact && s.forward>.1f)exit=ExitPath(p,p->Location(),s.normal,2*p->CollisionHeight()+32,s.path);
   if(!contact && !exit){LogMessage("RUNE_SPIDER_LOST_WALL z="+std::to_string(p->Location().z));Stop(p);return;}if(contact)s.normal=n;
  }
  if(exit){s.mantle=true;s.step=0;s.forward=s.strafe=0;LogMessage("RUNE_SPIDER_MANTLE actor="+p->Name.ToString());}

 }
 vec3 delta;
 if(s.mantle){auto d=s.path[s.step]-p->Location();float dist=length(d);if(dist<.75f){if(++s.step==3){Stop(p);return;}d=s.path[s.step]-p->Location();dist=length(d);}delta=dist>.001f?d*(std::min(dist,180.f*dt)/dist):vec3(0);}
 else{vec3 right=cross(vec3(0,0,1),-s.normal);delta=(vec3(0,0,s.forward*Speed)+right*(s.strafe*SideSpeed))*dt;}
 auto hit=p->TryMove(delta);if(p->bDeleteMe())return;float moved=length(p->Location()-before);
 s.stalled=(length(delta)>.05f&&moved<length(delta)*.15f)?s.stalled+dt:0;s.phase+=moved*(s.forward<-.05f?-1.f:1.f)/(96.f*std::max(.25f,p->DrawScale()));s.last=p->Location();
 p->Velocity()=dt>0?(p->Location()-before)/dt:vec3(0);p->Acceleration()=vec3(0);p->SetRotation(Rotator::FromVector(-s.normal));
 if(s.mantle && (hit.Fraction<.99f || s.stalled>.3f)){Stop(p);return;}
 if(!s.mantle && s.forward<0){auto floor=Trace(p,p->Location(),p->Location()-vec3(0,0,p->CollisionHeight()+4),{p->CollisionRadius()*.8f,p->CollisionRadius()*.8f,0});if(floor.Fraction<1 && floor.Normal.z>.7f){Stop(p);return;}}
 Animate(p);
}
void BeforeSave(){if(engine && engine->Level){auto actors=engine->Level->Actors;for(auto* a:actors)if(a && !a->NetworkReplica)if(auto* p=UObject::TryCast<UPawn>(a))Stop(p);}}
bool ScriptCall(UFunction* f,UObject* object,const ArrayView<ExpressionValue>& args,ExpressionValue& result){
 if(!engine->LaunchInfo.IsRune())return false;auto* p=UObject::TryCast<UPawn>(object);if(!p)if(auto* a=UObject::TryCast<UActor>(object);a&&a->IsA("AnimationProxy"))p=UObject::TryCast<UPawn>(a->Owner());if(!p || p->NetworkReplica)return false;
 auto n=f->Name;
 if(n=="Died" || n=="Destroyed" || n=="LimbSevered"){Stop(p);return false;}
 if(n=="Use" && object==p){if(p->Spider.active){Stop(p);return true;}return Start(p);}
 if(!p->Spider.active)return false;
 if(n=="Jump" && object==p){Stop(p,true);return true;}
 if(n=="PlayerMove" && object==p){auto* player=UObject::TryCast<UPlayerPawn>(p);if(player){Input(p,player->aForward()/6000.f,player->aStrafe()/6000.f);if(!player->NetworkDriven && args.size())CallEvent(p,"UpdateRotation",{args[0],ExpressionValue::FloatValue(1)});}return true;}
 for(auto name:{"Fire","AltFire","Attack","Defend","JumpOffPawn","SwitchWeapon","Powerup","Kick","Throw","ThrowWeapon","DropWeapon","GrabEdge","PlayMoving","PlayWaiting","AnimEnd","Timer","UseNotify","WeaponActivate","SwipeEffectStart"})if(n==name){result=ExpressionValue::BoolValue(false);return true;}
 return false;
}
void ApplyPose(const USkelModel& m,UActor* a,Array<RuneJointPose>& pose){
 if(!a || !UObject::TryCast<UPawn>(a) || !UObject::Cast<UPawn>(a)->Spider.active || pose.empty())return;size_t selected=m.Sequences.size(),idle=0;
 for(size_t i=0;i<m.Sequences.size();++i){if(m.Sequences[i].Info.Name==a->AnimSequence())selected=i;if(m.Sequences[i].Info.Name=="neutral_idle")idle=i;}
 if(selected==m.Sequences.size())return;auto name=m.Sequences[selected].Info.Name;if(name!="ClimbUpRope" && name!="intropullupA" && name!="pullupTest")return;
 // Run the original full-body ledge pose in place; authoritative swept
 // movement supplies its root travel. No knee, ankle, elbow or hand IK edits.
 auto first=m.DecodeFrame(selected,0),reference=m.DecodeFrame(idle,0);
 if(name!="ClimbUpRope" && a->bAnimLoop() && a->AnimFrame()>.8f){
  float t=std::clamp((a->AnimFrame()-.8f)/.2f,0.f,1.f);t=t*t*(3-2*t);
  for(size_t j=0;j<pose.size();++j)pose[j]=BlendRunePose(pose[j],first[j],t);
 }
 int pelvis=-1;for(size_t j=0;j<m.Joints.size();++j)if(m.Joints[j].Name=="pelvis"){pelvis=int(j);break;}
 if(pelvis>=0){auto current=m.JointMatrices(pose),rest=m.JointMatrices(reference);pose[0].Position+=Point(rest[pelvis])-Point(current[pelvis]);}
}
#include "RuneSpiderProbe.inc"
}
