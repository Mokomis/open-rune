#include "Precomp.h"
#include "RuneBot.h"
#ifdef OPENRUNE_ENABLE_NETWORK
#include "Engine.h"
#include "Packages/Core/UClass.h"
#include "Packages/Core/Properties/UProperty.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Brush/MovingBrush.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include <iostream>
#include <set>
namespace RuneBot {
namespace {
float planar(vec3 d){d.z=0;return length(d);}
UActor* trace(UPlayerPawn* p,UActor* ignore,vec3 from,vec3 to,float radius,float height,vec3& hit,vec3& normal) {
 for(const auto& h:p->XLevel()->Collision.Trace(from,to,height,radius,true,true,false)) {
  auto* a=h.Actor;if(!a||a==p||a==ignore||a->IsA("Pawn")||a->IsOwnedBy(p))continue;
  if(a!=p->Level()&&!a->bBlockPlayers())continue;
  hit=from+(to-from)*h.Fraction;normal=h.Normal;return a;
 }
 hit=to;normal=vec3(0);return nullptr;
}
bool harmless(UPlayerPawn* p,vec3 at) {
 auto* z=p->Level()->GetLocZone(at,{}).Zone;return z&&!z->bPainZone()&&!z->bWaterZone()&&!z->IsA("HeadBallZone");
}
bool dispatches(Engine& game,NameString event,NameString goal,int depth,std::set<NameString>& seen,bool& loop,float& delay) {
 if(event.IsNone() || goal.IsNone())return false;
 if(event==goal)return true;if(depth>=3 || !seen.insert(event).second)return false;
 for(auto* actor:game.Level->Actors)if(actor&&actor->Tag()==event&&actor->Class->Name=="Dispatcher") {
  auto* property=actor->Class->GetProperty("OutEvents");if(!property)continue;
  auto* events=static_cast<NameString*>(actor->GetProperty("OutEvents"));auto* waits=static_cast<float*>(actor->GetProperty("OutDelays"));float total=0;
  for(int i=0;i<std::min(8,property->ArrayDimension);++i)total+=std::max(0.f,waits[i]);
  for(int i=0;i<std::min(8,property->ArrayDimension);++i)if(dispatches(game,events[i],goal,depth+1,seen,loop,delay)){loop|=actor->GetBool("bIsLooping");delay+=total;return true;}
 }
 return false;
}
vec3 top(UBrush* m,float height){auto b=m->Brush()->BoundingBox.transform(GetMovingBrushTransform(m));auto c=b.center();c.z=b.max.z+height+1;return c;}
}
void Brain::discoverLifts(Engine& game,UPlayerPawn* p,uint64_t now) {
 if(liftLevel==game.Level&&liftRadius==p->CollisionRadius()&&liftHeight==p->CollisionHeight() && (liftActive>=0 || now<liftScanAt))return;
 liftScanAt=now+5000;
 lifts.clear();liftActive=-1;liftLevel=game.Level;liftRadius=p->CollisionRadius();liftHeight=p->CollisionHeight();
 for(auto* a:game.Level->Actors)if(auto* m=GetMovingBrush(a);m&&!m->bDeleteMe()&&m->Brush()&&m->GetByte("NumKeys")>=2&&m->GetByte("NumKeys")<=MovingBrushKeyCount(m)) {
  bool vertical=true;float travel=0;
  for(int key=1;key<int(m->GetByte("NumKeys"));++key){auto d=MovingBrushKeyPos(m,key)-MovingBrushKeyPos(m,0);auto r=MovingBrushKeyRot(m,key)-MovingBrushKeyRot(m,0);vertical&=planar(d)<1&&!r.Pitch&&!r.Yaw&&!r.Roll;travel=std::max(travel,std::abs(d.z));}
  if(!vertical || travel<64 || m->GetBool("bDamageTriggered") || (m->HasProperty("BumpType")?m->GetByte("BumpType"):m->GetByte("PolyObjBumpType"))==2 || m->GetBool("bTriggerOnceOnly"))continue;
  auto box=m->Brush()->BoundingBox.transform(GetMovingBrushTransform(m));auto extent=box.extents();
  if(extent.x<liftRadius+4 || extent.y<liftRadius+4)continue;
  auto center=top(m,liftHeight);vec3 hit,normal;
  // The AABB alone is not proof of a rideable floor (chains and doors also move).
  auto* surface=trace(p,nullptr,center,center-vec3(0,0,liftHeight+box.extents().z*2+64),4,0,hit,normal);
  if(surface!=m || normal.z<.7f){if(debug)std::cout<<"BOT_LIFT_SKIP actor="<<m->Name.ToString()<<" reason=top_surface hit="<<(surface?surface->Name.ToString():"none")<<std::endl;continue;}
  center=hit+vec3(0,0,liftHeight+1);
  LiftPlan plan;plan.actor=m;plan.offset=center-m->Location();plan.use=m->GetBool("bUseTriggered");
  const auto state=m->GetStateName();plan.automatic=state=="StandOpenTimed"||state=="BumpOpenTimed";
  for(auto* trigger:game.Level->Actors)if(trigger&&trigger->IsA("Trigger")&&trigger->GetBool("bInitiallyActive")) {
   int type=trigger->GetByte("TriggerType");if(type!=0&&type!=1&&type!=3&&type!=6)continue;
   bool loop=false;float delay=0;std::set<NameString> seen;
   if(dispatches(game,trigger->Event(),m->Tag(),0,seen,loop,delay)){plan.trigger=trigger;plan.automatic|=loop;plan.cycleSeconds=delay+m->GetFloat("MoveTime")*m->GetByte("NumKeys")*2;break;}
  }
  // Only reviewed proximity/Use and bounded Dispatcher chains can activate a lift.
  // Unknown switches, damage triggers and cinematic machinery remain excluded.
  if(!plan.automatic&&!plan.use&&!plan.trigger)continue;
  for(int key=0;key<int(m->GetByte("NumKeys"));++key) {
   // Tiny startup keys are animation details on one floor, not separate stops.
   bool duplicate=false;for(int prior=0;prior<key;++prior)if(length(MovingBrushKeyPos(m,prior)-MovingBrushKeyPos(m,key))<=std::max(1.f,p->MaxStepHeight()))duplicate=true;if(duplicate)continue;
   LiftStop stop;stop.center=center+m->GetVector("BasePos")+MovingBrushKeyPos(m,key)-m->Location();
   for(int heading=0;heading<16;++heading) {
    float angle=heading*6.2831853f/16;vec3 direction(std::cos(angle),std::sin(angle),0);
    float edge=std::min(std::abs(direction.x)>.001f?extent.x/std::abs(direction.x):1e9f,std::abs(direction.y)>.001f?extent.y/std::abs(direction.y):1e9f);
    float distance=edge+liftRadius+12;
    auto at=stop.center+direction*distance;float step=std::clamp(p->MaxStepHeight(),0.f,48.f);
    auto start=at+vec3(0,0,step+2);auto end=at-vec3(0,0,liftHeight+64);
    if(!trace(p,m,start,end,6,0,hit,normal)||normal.z<.7f)continue;
    auto standing=hit+vec3(0,0,liftHeight+1);
    if(standing.z-stop.center.z>step || stop.center.z-standing.z>64 || !harmless(p,standing))continue;
    // Clear body/head and a straight horizontal landing corridor; normal
    // collision remains authoritative when the platform actually arrives.
    if(trace(p,m,standing,standing+vec3(0,0,.1f),liftRadius,liftHeight-1,hit,normal))continue;
    bool corridor=!trace(p,m,stop.center,standing,liftRadius,liftHeight-1,hit,normal);
    if(!corridor) {
     auto rise=vec3(0,0,step);
     corridor=!trace(p,m,stop.center,stop.center+rise,liftRadius,liftHeight-1,hit,normal)&&
        !trace(p,m,stop.center+rise,standing+rise,liftRadius,liftHeight-1,hit,normal)&&
        !trace(p,m,standing+rise,standing,liftRadius,liftHeight-1,hit,normal);
    }
    if(!corridor)continue;
    stop.landings.push_back(standing);
   }
   plan.stops.push_back(stop);
  }
  if(plan.stops.size()<2 || std::count_if(plan.stops.begin(),plan.stops.end(),[](auto& x){return !x.landings.empty();})<2){if(debug)std::cout<<"BOT_LIFT_SKIP actor="<<m->Name.ToString()<<" reason=landings counts="<<plan.stops[0].landings.size()<<","<<plan.stops[1].landings.size()<<std::endl;continue;}
  if(debug)std::cout<<"BOT_LIFT_PLAN actor="<<m->Name.ToString()<<" bottom="<<plan.stops[0].center.z<<" top="<<plan.stops[1].center.z<<" entries="<<plan.stops[0].landings.size()<<","<<plan.stops[1].landings.size()<<std::endl;
  lifts.push_back(std::move(plan));
 }
}
std::optional<Decision> Brain::liftStep(Engine& game,UPlayerPawn* p,const openrune::net::Entity& self,vec3 goal,uint64_t now) {
 vec3 here(float(self.position[0])/8,float(self.position[1])/8,float(self.position[2])/8);
 if(liftActive<0 && (now<liftRetryAt || std::abs(goal.z-here.z)<80 || walkableLine(p,here,goal)))return {};
 discoverLifts(game,p,now);
 if(liftActive<0) {
  float best=1e30f;
  for(int i=0;i<int(lifts.size());++i)for(int from=0;from<int(lifts[i].stops.size());++from)for(int to=0;to<int(lifts[i].stops.size());++to) {
   if(from==to)continue;auto& plan=lifts[i];auto& entry=plan.stops[from];auto& exit=plan.stops[to];
   if(std::abs(entry.center.z-here.z)>64 || std::abs(exit.center.z-goal.z)>128 || planar(goal-exit.center)>2000)continue;
   for(auto a:entry.landings)for(auto b:exit.landings) {
    if(entry.center.z-a.z>p->MaxStepHeight() || a.z-entry.center.z>64)continue;
    float score=length(here-a)+length(goal-b);
    if(score<best && length(here-a)<1400 && !rejectedAt(a,now)) {best=score;liftActive=i;liftFrom=from;liftTo=to;liftEntry=a;liftExit=b;}
   }
  }
  if(liftActive<0)return {};
  liftPhase=0;liftGoal=goal;liftDeadline=now+uint64_t(std::clamp((lifts[liftActive].cycleSeconds+10)*1000.f,30000.f,90000.f));liftUseAt=0;
  liftLastZ=(lifts[liftActive].actor->Location()+lifts[liftActive].offset).z;liftMovedAt=liftWakeAt=liftApproachAt=now;liftApproachDistance=length(here-liftEntry);
  if(debug)std::cout<<"BOT_LIFT_SELECT actor="<<lifts[liftActive].actor->Name.ToString()<<" from="<<liftFrom<<" to="<<liftTo<<std::endl;
 }
 auto& plan=lifts[liftActive];auto* m=plan.actor;
 if(m->bDeleteMe() || now>=liftDeadline || self.health<=0) {
  rejectGoal(liftEntry,now);liftActive=-1;liftRetryAt=now+3000;routeAt=patrolAt=0;return {};
 }
 Decision out;out.input.life=self.life;out.input.yaw=self.angles[1];
 auto steer=[&](vec3 destination,float speed=650.f) {
  auto d=destination-here;d.z=0;float distance=length(d);if(distance<3)return;
  d/=distance;int wanted=Rotator::FromVector(d).Yaw;int turn=int16_t(uint16_t(wanted-self.angles[1]));out.input.yaw=uint16_t(self.angles[1]+std::clamp(turn,-4369,4369));
  float angle=float(out.input.yaw)*6.2831853f/65536;vec3 f(std::cos(angle),std::sin(angle),0);speed=std::min(speed,std::max(100.f,distance*12));
  out.input.forward=int16_t(dot(d,f)*speed);out.input.strafe=int16_t(dot(d,vec3(-f.y,f.x,0))*speed);
 };
 auto use=[&](){if(now>=liftUseAt){out.action=openrune::net::ActionKind::Use;liftUseAt=now+1500;}};
 auto current=m->Location()+plan.offset;vec3 hit,normal;
 if(std::abs(current.z-liftLastZ)>.5f){liftLastZ=current.z;liftMovedAt=now;}
 // Match the pawn's supporting footprint, not a thin center ray. On diagonal
 // platform edges the center can be over ground while the capsule still rides.
 bool supported=trace(p,nullptr,here,here-vec3(0,0,liftHeight+12),liftRadius,0,hit,normal)==m && (self.flags&2);
 bool atEntry=std::abs(current.z-plan.stops[liftFrom].center.z)<8;
 bool atExit=std::abs(current.z-plan.stops[liftTo].center.z)<8;
 if(liftPhase<2) {
  if(supported)liftPhase=2;
  else if(liftPhase==0 && (planar(here-liftEntry)>16 || std::abs(here.z-liftEntry.z)>48)) {
   float distance=length(here-liftEntry);
   if(distance+12<liftApproachDistance){liftApproachDistance=distance;liftApproachAt=now;}
   if(now-liftApproachAt>5000){rejectGoal(liftEntry,now);liftActive=-1;liftRetryAt=now+2000;routeAt=patrolAt=0;return {};}
   // navigate() intentionally considers an ordinary waypoint reached at 36
   // units; boarding needs a closer staging point. Do not deadlock between
   // that threshold and this phase's 16-unit threshold.
   auto next=planar(here-liftEntry)<64&&walkableLine(p,here,liftEntry)?liftEntry:navigate(game,p,liftEntry,now);auto d=next-here;d.z=0;
   if(length(d)>1 && safe(p,here,here+normalize(d)*16.f))steer(next);
   else use();
   progressAt=now+1500;return out;
  }
  else if(!atEntry) {
   liftPhase=1;
   // Stand/bump lifts cannot be called from an empty upper landing. Observe
   // one ordinary return cycle, then choose another route instead of waiting
   // for a hypothetical passenger indefinitely.
   uint64_t cycle=uint64_t(std::max(5000.f,(m->GetFloat("MoveTime")*m->GetByte("NumKeys")+m->GetFloat("StayOpenTime")+2.f)*1000));
   if(!plan.trigger&&!plan.use&&now-liftMovedAt>cycle) {
    rejectGoal(liftGoal,now);rejectGoal(liftEntry,now);liftActive=-1;liftRetryAt=now+3000;patrolAt=routeAt=0;return {};
   }
   if(planar(here-liftEntry)>12 && safe(p,here,liftEntry))steer(liftEntry,250);
   // Wait on verified solid ground, never walk into an empty shaft.
   ++liftWaits;progressAt=now+1500;
   if(plan.use)use();
   if(plan.trigger && !plan.trigger->bDeleteMe() && length(plan.trigger->Location()-here)<160 && (plan.trigger->GetByte("TriggerType")!=6 || p->FastTrace(plan.trigger->Location(),here))) {
    if(plan.trigger->GetByte("TriggerType")==6) {steer(plan.trigger->Location(),0);use();}
    else if(planar(plan.trigger->Location()-here)>8) {
     auto target=plan.trigger->Location();auto d=target-here;d.z=0;
     if(length(d)>1&&safe(p,here,here+normalize(d)*16.f))steer(target,300);
    }
   }
   return out;
  } else {
   liftPhase=1;auto next=current;auto d=next-here;d.z=0;
   if(length(d)>1 && safe(p,here,here+normalize(d)*std::min(16.f,length(d))))steer(next,850);
   else if(!(self.flags&2) && !trace(p,m,here,vec3(next.x,next.y,here.z),liftRadius,liftHeight-1,hit,normal))steer(next,650);
   else if((self.flags&2)&&now>=liftWakeAt&&harmless(p,next)&&
      !trace(p,m,here,here+vec3(0,0,96),liftRadius,liftHeight-1,hit,normal)&&
      !trace(p,m,here+vec3(0,0,96),next+vec3(0,0,96),liftRadius,liftHeight-1,hit,normal)) {
    out.action=openrune::net::ActionKind::Jump;liftWakeAt=now+2500;steer(next,650);
   } else if(plan.use)use();
   progressAt=now+1500;return out;
  }
 }
 if(liftPhase==2) {
  // Snapshot geometry plus grounded state is sufficient; client proxies have
  // no authoritative ActorBase pointer. Do not read or set server basing.
  if(!supported && std::abs(here.z-current.z)>96) {liftActive=-1;liftRetryAt=now+2000;return {};}
  auto platform=m->Brush()->BoundingBox.transform(GetMovingBrushTransform(m));float edgeMargin=liftRadius+10;
  bool inside=here.x>=platform.min.x+edgeMargin&&here.x<=platform.max.x-edgeMargin&&here.y>=platform.min.y+edgeMargin&&here.y<=platform.max.y-edgeMargin;
  // A footprint check alone misses overhangs above the edge of a lift. Move
  // inward when the rider's entire remaining vertical corridor is obstructed.
  auto rideEnd=here+vec3(0,0,plan.stops[liftTo].center.z-current.z);
  bool clearRide=!trace(p,m,here,rideEnd,liftRadius,liftHeight-1,hit,normal);
  if((!supported||!inside||!clearRide)&&planar(here-current)>8)steer(current,650);
  // After a return-on-obstruction lift closes, standing on it cannot send a
  // fresh Attach. Walk off onto the verified landing and board again. Jumping
  // is insufficient: Rune can retain Base during an ordinary jump.
  uint64_t wait=uint64_t(std::max(1500.f,(m->GetFloat("StayOpenTime")+m->GetFloat("DelayTime")+1.f)*1000));
  if(supported&&atEntry&&inside&&clearRide&&now-liftMovedAt>wait&&now>=liftWakeAt&&
     walkableLine(p,here,liftEntry)) {
   liftPhase=4;liftWakeAt=now+2500;
  }
  if(plan.use&&atEntry)use();
  if(atExit) {
   float best=1e30f;bool found=false;
   for(auto landing:plan.stops[liftTo].landings)if(walkableLine(p,here,landing)) {
    float score=length(landing-liftGoal);if(score<best){best=score;liftExit=landing;found=true;}
   }
   if(!found&&(self.flags&2)&&now>=liftWakeAt)for(auto landing:plan.stops[liftTo].landings) {
    auto rise=vec3(0,0,96);
    if(!trace(p,m,here,here+rise,liftRadius,liftHeight-1,hit,normal)&&
       !trace(p,m,here+rise,landing+rise,liftRadius,liftHeight-1,hit,normal)) {
     float score=length(landing-liftGoal);if(score<best){best=score;liftExit=landing;found=true;}
    }
   }
   if(found) {
    // Do not stop on the lip. Rune's diagonal brush support can outlive a
    // center-ray/AABB clearance check. Prefer another capsule width of solid
    // ground when available, while retaining tight landings that lack room.
    auto outward=liftExit-current;outward.z=0;
    if(length(outward)>1) {
     auto beyond=liftExit+normalize(outward)*(liftRadius+12);
     if(walkableLine(p,here,beyond))liftExit=beyond;
    }
    liftPhase=3;
   }
   else if(planar(here-current)>8)steer(current,450);
  }
  progressAt=now+1500;return out;
 }
 if(liftPhase==4) {
  auto box=m->Brush()->BoundingBox.transform(GetMovingBrushTransform(m));float margin=liftRadius+4;
  bool clear=here.x<box.min.x-margin||here.x>box.max.x+margin||here.y<box.min.y-margin||here.y>box.max.y+margin;
  if(clear&&!supported&&(self.flags&2)){liftPhase=0;liftApproachAt=now;liftApproachDistance=length(here-liftEntry);}
  else if(walkableLine(p,here,liftEntry))steer(liftEntry,450);
  progressAt=now+1500;return out;
 }
 if(liftPhase==3) {
  // A short-hold platform can depart before a walking rider clears its lip.
  // Use the same ordinary jump available to a player, with verified overhead
  // clearance and an already validated landing. Retry if it carried us back.
  if(supported&&!atExit){liftPhase=2;return out;}
  auto box=m->Brush()->BoundingBox.transform(GetMovingBrushTransform(m));float margin=liftRadius+4;
  bool clear=here.x<box.min.x-margin||here.x>box.max.x+margin||here.y<box.min.y-margin||here.y>box.max.y+margin;
  if(clear && !supported && (self.flags&2) && std::abs(here.z-liftExit.z)<12 && planar(here-liftExit)<24) {
   ++liftTrips;liftActive=-1;routeAt=0;progress=here;progressAt=now+1500;
   if(debug)std::cout<<"BOT_LIFT_EXIT trips="<<liftTrips<<std::endl;
   return out;
  }
  bool walk=walkableLine(p,here,liftExit);
  if((self.flags&2)&&atExit&&now>=liftWakeAt&&(!walk||m->GetFloat("StayOpenTime")<=1.5f)) {
   auto rise=vec3(0,0,96);
   if(!trace(p,m,here,here+rise,liftRadius,liftHeight-1,hit,normal)&&
      !trace(p,m,here+rise,liftExit+rise,liftRadius,liftHeight-1,hit,normal)) {
    out.action=openrune::net::ActionKind::Jump;liftWakeAt=now+2500;steer(liftExit,1000);return out;
   }
  }
  if(!(self.flags&2) || walk)steer(liftExit,1000);
  progressAt=now+1500;return out;
 }
 return out;
}
}
#endif
