#pragma once
#include "Engine.h"
#include "RuneNetwork.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/NavigationPoint/UNavigationPoint.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/UViewport.h"
#include "VM/ScriptCall.h"
#include "VM/Frame.h"
#include "Utils/CommandLine.h"
#include <iostream>
#include <set>
#include <iomanip>
#include "RuneNavigationIntegrityProbe.h"

inline void RunRuneNavigationProbe(Engine& game) {
 auto check=[](bool b,const char* why){if(!b)throw std::runtime_error(std::string("navigation probe: ")+why);};
 auto url=game.GetDefaultURL(commandline->GetArg("","--navigation-map","DM-Hildir.run"));
 url.AddOrReplaceOption("game=RuneI.RuneSinglePlayer");game.LoadMap(url);game.LoginPlayer();
 auto* p=game.viewport->Actor();p->DisableEvent("PlayerTick");p->Health()=10000;
 auto tick=[&](int n){for(int i=0;i<n;++i){game.TotalTime+=1.f/120;game.LevelInfo->TimeSeconds()+=1.f/120;game.Level->Tick(1.f/120,false);}};
 tick(60);
 auto* v=UObject::TryCast<UPawn>(p->Spawn(game.packages->FindClass("RuneI.DarkViking"),{},{},p->Location()+vec3(0,0,100),{}));check(v,"spawn");
 v->GotoState("","");v->DisableEvent("Tick");v->Health()=10000;v->SetFloat("HuntDistance",0);
 auto ground=[&](UNavigationPoint* nav,UPawn* actor,vec3& out){
  vec3 hit,normal;auto at=nav->Location();
  if(!actor->Trace(hit,normal,at-vec3(0,0,1024),at+vec3(0,0,80),false,vec3(actor->CollisionRadius()*.7f,actor->CollisionRadius()*.7f,0))||normal.z<.7f)return false;
  out=hit+vec3(0,0,actor->CollisionHeight()+.5f);
  return actor->CheckLocation(out,actor->CollisionRadius(),actor->CollisionHeight(),true).first;
 };
 struct Spot{UNavigationPoint* nav;vec3 v,p;};std::vector<Spot> spots;
 std::vector<UNavigationPoint*> sampleNodes;
 if(commandline->HasArg("","--navigation-all-markers")) {
  for(auto* a:game.Level->Actors)if(auto* n=UObject::TryCast<UNavigationPoint>(a);n&&!n->bDeleteMe())sampleNodes.push_back(n);
 } else for(auto* n=game.LevelInfo->NavigationPointList();n;n=n->nextNavigationPoint())sampleNodes.push_back(n);
 for(auto* nav:sampleNodes){
  if(nav->bPlayerOnly()||nav->IsA("Teleporter")||nav->IsA("LiftCenter")||nav->IsA("LiftExit"))continue;
  vec3 a,b;if(ground(nav,v,a)&&ground(nav,p,b))spots.push_back({nav,a,b});
 }
 if(commandline->HasArg("","--navigation-stairs")) {
  using namespace openrune::net;
  auto* zone=p->Region().Zone;MovementAck ack;ack.mode=MovementWalking;ack.speed=uint16_t(p->GroundSpeed()*8);
  ack.acceleration=uint16_t(p->AccelRate());ack.friction=uint16_t(zone->ZoneGroundFriction()*256);ack.step=uint16_t(p->MaxStepHeight()*8);
  p->NetworkReplica=true;unsigned candidates=0;
  for(auto& spot:spots)for(int heading=0;heading<16;++heading){
   MovementPose pose;pose.mode=MovementWalking;pose.position={spot.p.x,spot.p.y,spot.p.z};Input input;input.life=1;input.forward=1000;input.yaw=uint16_t(heading*4096);
   float low=spot.p.z,high=spot.p.z;int steps=0;
   for(;steps<210;++steps){
    auto prior=pose;
    if(!StepRunePredictedMovement(game,p,pose,input,ack,1.f/60)||pose.mode!=MovementWalking)break;
    low=std::min(low,pose.position[2]);high=std::max(high,pose.position[2]);
    if(std::hypot(pose.position[0]-prior.position[0],pose.position[1]-prior.position[1])<.01f)break;
   }
   float rise=pose.position[2]-spot.p.z,travel=std::hypot(pose.position[0]-spot.p.x,pose.position[1]-spot.p.y);
   if(high-low>=90&&travel>=180&&std::abs(rise)>=80){
    ++candidates;std::cout<<"STAIR_SCAN route="<<spot.p.x<<","<<spot.p.y<<","<<spot.p.z<<","<<input.yaw<<" rise="<<rise<<" travel="<<travel<<" range="<<high-low<<" steps="<<steps<<" node="<<spot.nav->Name.ToString()<<"\n";
   }
  }
  p->NetworkReplica=false;std::cout<<"STAIR_SCAN_OK candidates="<<candidates<<"\n";return;
 }
 unsigned markers=0,listed=0;for(auto* a:game.Level->Actors)if(a && a->IsA("NavigationPoint"))++markers;
 for(auto* n=game.LevelInfo->NavigationPointList();n;n=n->nextNavigationPoint())++listed;
 std::cout<<"NAV_SPOTS count="<<spots.size()<<" listed="<<listed<<" markers="<<markers<<"\n";
 if(commandline->HasArg("","--navigation-integrity") || commandline->HasArg("","--navigation-safety")){RunRuneNavigationIntegrityProbe(game,v);return;}
 if(commandline->HasArg("","--navigation-audit")) {
  std::cout<<"NAV_PROFILE radius="<<v->CollisionRadius()<<" height="<<v->CollisionHeight()<<" step="<<v->MaxStepHeight()<<"\n";
  // Read-only inventory of the loaded map. No fallback search or NPC pursuit.
  std::set<UNavigationPoint*> linked;
  for(auto* node=game.LevelInfo->NavigationPointList();node;node=node->nextNavigationPoint()) {
   if(!linked.insert(node).second)break;
  }
  std::vector<std::pair<UNavigationPoint*,vec3>> grounded;
  auto indexes=[](auto values) {std::string text;for(int value:values){if(!text.empty())text+=",";text+=std::to_string(value);if(value<0)break;}return text;};
  for(auto* actor:game.Level->Actors) {
   if(!actor || actor->bDeleteMe())continue;
   auto* nav=UObject::TryCast<UNavigationPoint>(actor);
   if(nav) {
    vec3 at;bool support=ground(nav,v,at);if(support)grounded.push_back({nav,at});
    std::cout<<"NAV_NODE name="<<std::quoted(nav->Name.ToString())<<" class="<<std::quoted(nav->Class->Name.ToString())
     <<" linked="<<linked.contains(nav)<<" player_only="<<int(bool(nav->bPlayerOnly()))<<" special_cost="<<int(bool(nav->bSpecialCost()))
     <<" one_way="<<int(bool(nav->bOneWayPath()))<<" end_only="<<int(bool(nav->bEndPointOnly()))
     <<" xyz="<<nav->Location().x<<","<<nav->Location().y<<","<<nav->Location().z<<" grounded="<<support
     <<" paths="<<indexes(nav->Paths())<<" upstream="<<indexes(nav->upstreamPaths())<<" pruned="<<indexes(nav->PrunedPaths())<<"\n";
   }
   if(actor->IsA("Mover"))
    std::cout<<"NAV_MOVER name="<<std::quoted(actor->Name.ToString())<<" class="<<std::quoted(actor->Class->Name.ToString())
     <<" tag="<<std::quoted(actor->Tag().ToString())<<" event="<<std::quoted(actor->Event().ToString())
     <<" state="<<std::quoted(actor->GetStateName().ToString())<<" keys="<<int(actor->GetByte("NumKeys"))
     <<" move_time="<<actor->GetFloat("MoveTime")<<" xyz="<<actor->Location().x<<","<<actor->Location().y<<","<<actor->Location().z<<"\n";
  }
  auto* floorGoal=UObject::Cast<UActor>(game.packages->GetTransientPackage()->NewObject("NavigationAuditFloor",game.packages->FindClass("Engine.Actor"),ObjectFlags::Transient));
  floorGoal->Level()=game.LevelInfo;floorGoal->XLevel()=game.Level;floorGoal->CollisionRadius()=floorGoal->CollisionHeight()=0;
  for(auto* actor:game.Level->Actors)if(actor && !actor->bDeleteMe() && actor->IsA("PlayerStart")) {
   auto* spawn=UObject::TryCast<UNavigationPoint>(actor);vec3 at;
   bool support=spawn && ground(spawn,v,at);unsigned checked=0,reached=0,geometric=0;std::string reachable;
   if(support && v->SetLocation(at)) {
    v->SetPhysics(PHYS_Walking);auto candidates=grounded;
    std::sort(candidates.begin(),candidates.end(),[&](auto a,auto b){return length(a.second-at)<length(b.second-at);});
    for(auto [nav,position]:candidates) {
     if(nav==spawn || nav->bPlayerOnly() || length(position-at)>512)continue;
     if(checked>=8)break;++checked;
     floorGoal->Location()=position;if(v->ReachableWalking(floorGoal))++geometric;
     if(v->ActorReachable(nav,true)){++reached;if(!reachable.empty())reachable+=",";reachable+=nav->Name.ToString();}
    }
   }
   std::cout<<"NAV_SPAWN name="<<std::quoted(actor->Name.ToString())<<" team="<<int(actor->GetByte("TeamNumber"))
    <<" grounded="<<support<<" checked="<<checked<<" geometric="<<geometric<<" reached="<<reached<<" reachable="<<(reachable.empty()?"none":reachable)<<"\n";
  }
  unsigned upstream=0,mismatches=0;
  for(auto* node=game.LevelInfo->NavigationPointList();node;node=node->nextNavigationPoint()) {
   for(int i:node->upstreamPaths()) {
    if(i<0||size_t(i)>=game.Level->ReachSpecs.size())break;
    ++upstream;if(game.Level->ReachSpecs[i].endActor!=node)++mismatches;
   }
  }
  std::cout<<"NAV_REVERSE entries="<<upstream<<" mismatches="<<mismatches<<" specs="<<game.Level->ReachSpecs.size()<<"\n";
  int auditIndex=0;for(auto& edge:game.Level->ReachSpecs) {
   std::cout<<"NAV_EDGE index="<<auditIndex++<<" from="<<(edge.startActor?edge.startActor->Name.ToString():"none")<<" to="<<(edge.endActor?edge.endActor->Name.ToString():"none")<<" pruned="<<int(edge.bPruned)<<" radius="<<edge.collisionRadius<<" height="<<edge.collisionHeight<<" flags="<<edge.reachFlags<<" distance="<<edge.distance<<"\n";
  }
  for(auto& s:spots){
   v->SetLocation(s.v);v->SetPhysics(PHYS_Walking);unsigned links=0;
   for(int edge:s.nav->Paths())if(edge>=0 && size_t(edge)<game.Level->ReachSpecs.size())++links;
   std::cout<<"NAV_ANCHOR name="<<s.nav->Name.ToString()<<" raw="<<s.nav->Location().x<<","<<s.nav->Location().y<<","<<s.nav->Location().z<<" floor="<<s.v.z<<" near_reachable="<<v->ActorReachable(s.nav,true)<<" links="<<links<<"\n";
  }
  if(commandline->HasArg("","--navigation-graph-only")){std::cout<<"RUNE_NAV_GRAPH_OK\n";return;}
  unsigned blocked=0,missing=0;
  for(auto& a:spots)for(auto& b:spots){
   if(a.nav==b.nav||length(a.v-b.p)>2000||std::abs(a.v.z-b.p.z)>80)continue;
   if(!v->SetLocation(a.v)||!p->SetLocation(b.p))continue;
   v->SetPhysics(PHYS_Walking);
   if(v->ActorReachable(p)||v->FastTrace(p->Location(),v->Location()))continue;
   ++blocked;UActor* next=nullptr;
   for(int query=0;query<100 && !next;++query){game.TotalTime+=.025;v->ClearPaths();next=UObject::TryCast<UActor>(v->FindPathToward(p,false));}
   if(!next){++missing;std::cout<<"NAV_MISSING start="<<a.nav->Name.ToString()<<" end="<<b.nav->Name.ToString()<<"\n";}
  }
  std::cout<<"RUNE_NAV_AUDIT obstructed="<<blocked<<" missing="<<missing<<"\n";return;
 }
 if(commandline->HasArg("","--navigation-graph-only")){std::cout<<"RUNE_NAV_GRAPH_OK\n";return;}
 if(commandline->HasArg("","--navigation-lift-barrier")) {
  auto a=std::find_if(spots.begin(),spots.end(),[](auto& s){return s.nav->Name=="PlayerStart0";});
  auto b=std::find_if(spots.begin(),spots.end(),[](auto& s){return s.nav->Name=="PlayerStart3";});
  check(a!=spots.end()&&b!=spots.end()&&std::abs(a->v.z-b->p.z)>200,"lift-separated floor fixture");
  check(v->SetLocation(a->v),"lift start");v->SetPhysics(PHYS_Walking);v->Enemy()=nullptr;auto origin=v->Location();
  bool invented=false;for(int i=0;i<120;++i){game.TotalTime+=.025;invented|=RuneNavigation::Fallback(v,b->p)!=nullptr;}
  check(!invented && length(v->Location()-origin)<.01f,"walker must not invent lift/floor crossing");
  std::cout<<"RUNE_NAV_LIFT_BARRIER_OK no_fabricated_route=1 no_actor_relocation=1\n";return;
 }
 if(commandline->HasArg("","--navigation-stall")) {
  bool ready=false;
  for(auto& spot:spots) {
   if(ready)break;
   for(int i=0;i<16;++i) {
    vec3 direction(std::cos(i*3.14159265f/8),std::sin(i*3.14159265f/8),0);
    if(!v->SetLocation(spot.v)||!p->SetLocation(spot.v+direction*180.f))continue;
    v->SetPhysics(PHYS_Walking);v->Enemy()=p;v->SetRotation(Rotator::FromVector(direction));
    if(!v->CanSee(p)||v->TryMove(direction*50.f,true).Fraction<1)continue;
    v->Tactics={};v->GotoState("Fighting","Begin");v->bAnimFinished()=true;v->bAnimLoop()=true;
    bool issued=false,replanned=false;
    for(int frame=0;frame<40;++frame) {
      // Deliberately stall physical execution after a clear planned move.
      // This is a bounded decision/liveness assertion, not a physics replay.
      RuneTactics::Tick(v,.1f);issued|=v->Tactics.moveTime>0;
      if(issued && v->GetStateName()=="Charging"){replanned=true;break;}
    }
    if(!issued)continue;
    std::cout<<"RUNE_STALL_RESULT walking_intent="<<issued<<" replanned="<<replanned<<" state="<<v->GetStateName().ToString()<<"\n";
    check(replanned,"tactical clear-plan stall never returned to routing");ready=true;break;
   }
  }
  check(ready,"no clear stuck-progress fixture");return;
 }
 struct Trial{Spot a,b;};std::vector<Trial> trials;std::set<UNavigationPoint*> starts;
 for(auto& a:spots){
  if(trials.size()>=size_t(commandline->GetArgInt("","--navigation-cases",6)))break;
  for(auto& b:spots){
   auto distance=length(a.v-b.p);if(distance<500||distance>2300||std::abs(a.v.z-b.p.z)>180)continue;
   if(!v->SetLocation(a.v)||!p->SetLocation(b.p))continue;
   v->SetPhysics(PHYS_Walking);p->SetPhysics(PHYS_Walking);
   if(v->ActorReachable(p)||v->FastTrace(p->Location(),v->Location()))continue;
   bool local=commandline->HasArg("","--navigation-local");
   if(local && v->FindPathToward(p,false,true))continue;
   v->ClearPaths();UActor* next=nullptr;
   for(int query=0;query<(local?100:1) && !next;++query){game.TotalTime+=.025;next=UObject::TryCast<UActor>(v->FindPathToward(p,false));}
   if(!next)continue;
   std::cout<<"NAV_CANDIDATE start="<<a.nav->Name.ToString()<<" end="<<b.nav->Name.ToString()<<" next="<<next->Name.ToString()<<" distance="<<distance<<"\n";
   trials.push_back({a,b});break;
  }
 }
 if(commandline->HasArg("","--navigation-walls")) {
  unsigned tested=0,good=0,steps=0;
  for(auto& spot:spots) {
   if(tested>=12)break;
   for(int angle=0;angle<16;++angle) {
    v->GotoState("","");v->DisableEvent("Tick");v->Velocity()=v->Acceleration()=vec3(0);
    p->SetLocation(spot.p+vec3(0,0,600));
    if(!v->SetLocation(spot.v))continue;
    v->SetPhysics(PHYS_Walking);
    vec3 d(std::cos(angle*3.14159265f/8),std::sin(angle*3.14159265f/8),0);
    auto wall=v->TryMove(d*200.f,true);
    if(wall.Fraction<.1f||wall.Fraction>.9f||std::abs(wall.Normal.z)>.2f||wall.Actor!=v->Level())continue;
    auto at=spot.v+d*(wall.Fraction*200.f-2);
    if(!v->SetLocation(at))continue;
    auto side=vec3(-d.y,d.x,0);float distance=v->CollisionRadius()+16;
    if(v->TryMove(side*distance,true).Fraction<1 && v->TryMove(-side*distance,true).Fraction<1)continue;
    v->Focus()=v->Destination()=at+d*200.f;v->Acceleration()=d*v->AccelRate();v->bCanJump()=false;
    bool adjusted=v->PickWallAdjust();float step=length(v->Destination()-at);
    bool valid=v->Physics()==PHYS_Walking && (!adjusted || step>=v->CollisionRadius()+15);
    float actual=0;
    if(valid && adjusted) {
      auto destination=v->Destination();
      v->DisableEvent("SeePlayer");v->DisableEvent("EnemyNotVisible");v->Enemy()=nullptr;
      v->MoveTo(destination,.5f);
      for(int f=0;f<240;++f){tick(1);if(v->StateFrame->GetLatentState()!=LatentRunState::MoveTo)break;}
      actual=length(v->Location()-at);valid=length(v->Location()-destination)<8;
      ++steps;
    }
    std::cout<<"NAV_WALL index="<<tested++<<" adjusted="<<adjusted<<" physics="<<int(v->Physics())<<" destination_distance="<<step<<" actual_move="<<actual<<" valid="<<valid<<"\n";
    good+=valid;break;
   }
  }
  std::cout<<"RUNE_WALL_RESULT passed="<<good<<" cases="<<tested<<"\n";
  check(tested>=3 && steps>=3 && good==tested,"blocked nonjumper cannot complete a useful ground sidestep");return;
 }
 check(trials.size()>=3,"insufficient original-map obstructed routes");
 unsigned passed=0,index=0;
 for(auto& trial:trials){
  if(commandline->HasArg("","--navigation-save") && trial.a.nav->Name!="InventorySpot19")continue;
  v->GotoState("","");v->Tactics={};v->Velocity()=v->Acceleration()=vec3(0);p->Velocity()=p->Acceleration()=vec3(0);
  check(p->SetLocation(trial.b.p)&&v->SetLocation(trial.a.v),"place pursuit");
  v->Enemy()=p;v->SetVector("LastSeenPos",p->Location());v->SetVector("HomeBase",v->Location());v->SetFloat("HuntDistance",0);v->EnableEvent("Tick");
  v->SetRotation(Rotator::FromVector(p->Location()-v->Location()));v->GotoState("Hunting","Begin");
  if(commandline->HasArg("","--navigation-save")) {
   check(commandline->HasArg("","--save-directory"),"isolated route save");
   for(int f=0;f<1200 && (!v->MoveTarget() || v->MoveTarget()->Tag()!="OpenRuneLocalRoute");++f)tick(1);
   check(v->MoveTarget() && v->MoveTarget()->Tag()=="OpenRuneLocalRoute","save during real local route");
   auto name=v->Name;game.SaveGameToSlot(93,"Enemy routing isolated check");
   game.LoadFromSaveFile(UnrealURL("?load=93"));game.PossessSavedPlayer();p=game.viewport->Actor();
   v=nullptr;for(auto* a:game.Level->Actors)if(a && a->Name==name)v=UObject::TryCast<UPawn>(a);
   check(v && v->MoveTarget() && v->MoveTarget()->Tag()=="OpenRuneLocalRoute","restored invisible route target");
   bool reached=false;
   for(int f=0;f<4800;++f){tick(1);p->Health()=v->Health()=10000;if(length(p->Location()-v->Location())<115 && v->CanSee(p)){reached=true;break;}}
   check(reached,"saved pursuit failed to resume");UActor* mark=nullptr;
   for(auto* a:game.Level->Actors)if(a && !a->bDeleteMe() && a->Owner()==v && a->Tag()=="OpenRuneLocalRoute")mark=a;
   check(mark,"one retained local route target");check(v->Destroy() && mark->bDeleteMe(),"goal cleanup on source destruction");
   std::cout<<"RUNE_NAV_SAVE_OK resumed=1 marker_cleanup=1\n";return;
  }
  float closest=length(v->Location()-p->Location()),travel=0,stalled=0,worstStall=0;vec3 prior=v->Location();std::set<std::string> nodes;
  bool reached=false;
  for(int f=0;f<3600;++f){
   tick(1);v->Health()=p->Health()=10000;
   closest=std::min(closest,length(v->Location()-p->Location()));
   if(v->MoveTarget())nodes.insert(v->MoveTarget()->Name.ToString());
   if(closest<115 && v->CanSee(p)){reached=true;break;}
   if(f%60==0){float moved=length(v->Location()-prior);travel+=moved;stalled=moved<4?stalled+.5f:0;worstStall=std::max(worstStall,stalled);prior=v->Location();
    if(commandline->HasArg("","--navigation-trace"))std::cout<<"NAV_TRACE case="<<index<<" f="<<f<<" pos="<<v->Location().x<<","<<v->Location().y<<","<<v->Location().z<<" state="<<v->GetStateName().ToString()<<" node="<<(v->MoveTarget()?v->MoveTarget()->Name.ToString():"None")<<" timer="<<v->MoveTimer()<<" test="<<v->GetString("teststring")<<"\n";
   }
  }
  std::cout<<"NAV_PURSUIT case="<<index++<<" start="<<trial.a.nav->Name.ToString()<<" end="<<trial.b.nav->Name.ToString()<<" passed="<<reached<<" closest="<<closest<<" travel="<<travel<<" stall="<<worstStall<<" nodes="<<nodes.size()<<"\n";
  passed+=reached;
 }
 std::cout<<"RUNE_NAVIGATION_RESULT passed="<<passed<<" cases="<<trials.size()<<"\n";
 check(passed==trials.size(),"original pursuit failed to traverse authored route");
}
