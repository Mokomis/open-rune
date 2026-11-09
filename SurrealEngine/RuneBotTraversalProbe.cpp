#include "Precomp.h"
#include "RuneBot.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Brush/MovingBrush.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UGameInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/UViewport.h"
#include "VM/ScriptCall.h"
#include "VM/Frame.h"
#include "Utils/CommandLine.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>
#ifdef OPENRUNE_ENABLE_NETWORK
namespace RuneBot {
struct TraversalFixture {
 using Bytes=openrune::net::Bytes;
 static void put(Bytes& b,uint32_t n){for(int i=0;i<4;++i)b.push_back(uint8_t(n>>(i*8)));}
 static uint32_t get(const Bytes& b,size_t& p){if(p+4>b.size())throw std::runtime_error("short traversal record");uint32_t n=0;for(int i=0;i<4;++i)n|=uint32_t(b[p++])<<(i*8);return n;}
 static void block(Bytes& b,const Bytes& x){put(b,uint32_t(x.size()));b.insert(b.end(),x.begin(),x.end());}
 static Bytes block(const Bytes& b,size_t& p){auto n=get(b,p);if(n>16000000||n>b.size()-p)throw std::runtime_error("bad traversal record");Bytes r(b.begin()+p,b.begin()+p+n);p+=n;return r;}
 static void write(const std::filesystem::path& path,const Bytes& b){auto temp=path.string()+".tmp";{std::ofstream f(temp,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());if(!f)throw std::runtime_error("traversal write");}std::filesystem::rename(temp,path);}
 static Bytes read(const std::filesystem::path& path){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(!std::filesystem::exists(path)){if(std::chrono::steady_clock::now()>end)throw std::runtime_error("other traversal process timed out");std::this_thread::sleep_for(std::chrono::milliseconds(1));}std::ifstream f(path,std::ios::binary);Bytes b((std::istreambuf_iterator<char>(f)),{});std::filesystem::remove(path);return b;}
 static void Roam(Engine& game,UPlayerPawn* p) {
  using namespace openrune::net;
  auto check=[](bool v,const char* why){if(!v)throw std::runtime_error(std::string("bot roam: ")+why);};
  auto role=commandline->GetArg("","--traversal-process","");auto folder=std::filesystem::path(commandline->GetArg("","--traversal-exchange",""));
  check(folder.is_absolute()&&std::filesystem::is_directory(folder),"isolated exchange");
  constexpr unsigned rounds=1800; // 60 simulated seconds at 30 snapshots/s.
  if(role=="server") {
   RuneNetworkAuthority authority(game);check(authority.bind(1,p,1),"bind");uint64_t tick=1;auto ref=authority.identity(p);
   auto progress=p->Location(),previous=progress;float travel=0,still=0,worst=0;unsigned deaths=0;bool alive=true;
   for(unsigned round=1;round<=rounds;++round) {
    World world;check(authority.capture(world),"capture");Bytes frame;put(frame,ref.id);put(frame,ref.generation);
    auto events=authority.pendingActors();put(frame,unsigned(events.size()));uint64_t released=0;
    for(const auto& event:events){Bytes bytes;check(encodeActorEvent(1,event,bytes),"actor encode");block(frame,bytes);released=event.id;}
    if(released)check(authority.releaseActorsThrough(released),"actor release");Bytes snap;check(encodeSnapshot(Snapshot{round,0,tick,world},nullptr,snap),"snapshot encode");block(frame,snap);write(folder/("state-"+std::to_string(round)),frame);
    auto bytes=read(folder/("input-"+std::to_string(round)));size_t offset=0;auto encoded=block(bytes,offset);InputBatch batch;check(decodeInputs(1,encoded,batch)&&offset==bytes.size()&&batch.inputs.size()==1,"input decode");
    auto now=1000+(++tick*1000)/60;authority.beginTick(tick,now);authority.input(1,batch.inputs.front());for(const auto& action:batch.actions)authority.action(1,AdmittedAction{action,batch.inputs.front(),now},now);authority.commitContacts();
    for(int step=0;step<2;++step){if(step){++tick;authority.beginTick(tick,1000+tick*1000/60);authority.input(1,batch.inputs.front());}game.TotalTime+=1.f/60;game.LevelInfo->TimeSeconds()+=1.f/60;game.Level->Tick(1.f/60,false);}
    auto at=p->Location();float distance=length(at-previous);if(distance<1000)travel+=distance;previous=at;
    if(p->Health()<=0){if(alive)++deaths;alive=false;still=0;progress=at;}
    else {alive=true;if(length(at-progress)>20){still=0;progress=at;}else still+=1.f/30;worst=std::max(worst,still);}
   }
   std::cout<<"BOT_ROAM_RESULT travel="<<travel<<" worst_stationary_seconds="<<worst<<" deaths="<<deaths<<std::endl;
   check(travel>300&&worst<15,"insufficient travel or prolonged idle/stuck interval");
   std::cout<<"BOT_ROAM_SERVER_OK actual_inputs normal_patrol no_forced_route\n";
  } else {
   check(role=="client","role");game.NetworkClient=true;
   for(auto* actor:game.Level->Actors)if(actor&&(actor->IsA("Pawn")||actor->IsA("Inventory")||actor->IsA("Projectile"))){game.Level->Collision.RemoveFromCollision(actor);actor->bHidden()=true;actor->NetworkReplica=true;}
   RuneNetworkReplica replica;Brain brain(73);uint64_t actionId=0;brain.debug=true;
   for(unsigned round=1;round<=rounds;++round) {
    auto bytes=read(folder/("state-"+std::to_string(round)));size_t offset=0;EntityRef ref;ref.id=get(bytes,offset);ref.generation=get(bytes,offset);
    auto count=get(bytes,offset);check(count<10000,"event cap");
    for(unsigned i=0;i<count;++i){auto encoded=block(bytes,offset);ActorEvent event;check(decodeActorEvent(1,encoded,event),"actor decode");if(event.operation==ActorOperation::Spawn)check(replica.spawn(game,event)!=nullptr,"spawn");else if(event.operation==ActorOperation::Appearance)check(replica.appearance(game,event),"appearance");else check(replica.unbind(event.ref),"despawn");}
    auto encoded=block(bytes,offset);Snapshot snapshot;check(decodeSnapshot(encoded,nullptr,snapshot)&&offset==bytes.size(),"snapshot decode");check(replica.install(snapshot),"install");replica.present(1);
    MatchStatus status;status.mode=GameMode::TeamDeathmatch;status.phase=MatchPhase::Playing;status.scores={{1,0,0,0,255,ref}};
    auto now=1000+snapshot.tick*1000/60;auto decision=brain.think(game,replica,snapshot.world,status,ref,now);auto input=decision.input;input.sequence=round;input.createdMs=now;
    InputBatch batch;batch.inputs.push_back(input);if(decision.action)batch.actions.push_back(Action{++actionId,input.sequence,now,input.life,*decision.action});Bytes inputBytes;check(encodeInputs(1,batch,inputBytes),"input encode");Bytes frame;block(frame,inputBytes);write(folder/("input-"+std::to_string(round)),frame);
   }
   std::cout<<"BOT_ROAM_CLIENT_OK decisions="<<brain.decisions<<" routes="<<brain.routes<<" max_think_us="<<brain.maxThinkUs<<std::endl;
  }
 }
 static void Process(Engine& game,UPlayerPawn* p) {
  using namespace openrune::net;
  auto check=[](bool v,const char* why){if(!v)throw std::runtime_error(std::string("lift processes: ")+why);};
  auto role=commandline->GetArg("","--traversal-process","");auto folder=std::filesystem::path(commandline->GetArg("","--traversal-exchange",""));
  check(folder.is_absolute()&&std::filesystem::is_directory(folder),"existing isolated exchange directory");
  auto name=commandline->GetArg("","--traversal-lift","Mover8");int from=commandline->GetArgInt("","--traversal-from",0),to=commandline->GetArgInt("","--traversal-to",1-from);
  Brain brain(73);brain.debug=true;
  if(role=="server") {
   brain.discoverLifts(game,p);auto plan=std::find_if(brain.lifts.begin(),brain.lifts.end(),[&](auto& x){return x.actor->Name==name;});check(plan!=brain.lifts.end(),"server lift plan");
   check(from>=0&&to>=0&&from!=to&&from<int(plan->stops.size())&&to<int(plan->stops.size())&&!plan->stops[from].landings.empty()&&!plan->stops[to].landings.empty(),"valid requested stop pair");
   auto start=plan->stops[from].landings[0],goal=plan->stops[to].landings[0];auto* mover=plan->actor;if(commandline->HasArg("","--traversal-debug"))std::cout<<"LIFT_PROPERTIES\n"<<mover->PrintProperties()<<"\nCAPSULE radius="<<p->CollisionRadius()<<" height="<<p->CollisionHeight()<<" step="<<p->MaxStepHeight()<<" jump="<<p->JumpZ()<<std::endl;float expectedTravel=std::abs(plan->stops[to].center.z-plan->stops[from].center.z);
   check(p->SetLocation(start),"server initial landing");p->SetBase(nullptr,true);p->Velocity()=p->Acceleration()=vec3(0);p->SetPhysics(PHYS_Walking);
   if(commandline->HasArg("","--traversal-debug"))for(auto function:{"Attach","Bump","DoOpen","DoClose"})Frame::AddTrace(function,[mover,p,function](UFunction*,UObject* actor,const ArrayView<ExpressionValue>&){if(actor==mover)std::cout<<"LIFT_EVENT fn="<<function<<" player="<<p->bIsPlayer()<<" latent="<<(mover->StateFrame?int(mover->StateFrame->GetLatentState()):-1)<<" attach="<<mover->IsEventEnabled(EventName::Attach)<<std::endl;});
   // Keep this fixed-objective fixture from detouring for an optional shield or rune.
   if(!p->GetUObject("Shield")) {auto* item=p->Spawn(game.packages->FindClass("RuneI.VikingShield"),{},{},p->Location()+vec3(0,0,80),{});check(item,"shield");auto* copy=CallEvent(item,"SpawnCopy",{ExpressionValue::ObjectValue(p)}).ToObject();CallEvent(p,"AcquireInventory",{ExpressionValue::ObjectValue(copy)});}
   p->SetInt("MaxPower",200);p->SetInt("RunePower",200);
   if(commandline->HasArg("","--traversal-away")) {
    auto* helper=game.LoginNetworkPlayer("PreviousRider");check(helper,"rider");auto box=mover->Brush()->BoundingBox.transform(GetMovingBrushTransform(mover));auto at=box.center();at.z=box.max.z+helper->CollisionHeight()+2;check(helper->SetLocation(at),"previous rider placement");helper->SetPhysics(PHYS_Falling);helper->Velocity()=vec3(0);
    for(int i=0;i<40;++i){game.TotalTime+=1.f/60;game.LevelInfo->TimeSeconds()+=1.f/60;game.Level->Tick(1.f/60,false);}
    // End the fixture's previous rider without assuming arbitrary nearby space
    // is walkable. Destroy performs the normal detach/cleanup callbacks.
    helper->SetBase(nullptr,true);check(helper->Destroy(),"previous rider leaves");
   }
   RuneNetworkAuthority authority(game);check(authority.bind(1,p,1),"bind");uint64_t tick=1;auto ref=authority.identity(p);bool based=false;float rise=0;auto origin=p->Location();
   for(unsigned round=1;round<=1800;++round) {
    World world;check(authority.capture(world),"server capture");Bytes frame;put(frame,ref.id);put(frame,ref.generation);for(int axis=0;axis<3;++axis)put(frame,uint32_t(int32_t(std::lround(goal[axis]*8))));
    auto events=authority.pendingActors();put(frame,unsigned(events.size()));uint64_t released=0;
    for(const auto& event:events){Bytes bytes;check(encodeActorEvent(1,event,bytes),"encode actor");block(frame,bytes);released=event.id;}
    if(released)check(authority.releaseActorsThrough(released),"release actor");Bytes snap;check(encodeSnapshot(Snapshot{round,0,tick,world},nullptr,snap),"encode snapshot");block(frame,snap);write(folder/("state-"+std::to_string(round)),frame);
    auto bytes=read(folder/("input-"+std::to_string(round)));size_t offset=0;bool complete=get(bytes,offset)!=0;auto encoded=block(bytes,offset);check(offset==bytes.size(),"input trailing bytes");InputBatch batch;check(decodeInputs(1,encoded,batch)&&batch.inputs.size()==1,"decode inputs");
    auto now=1000+(++tick*1000)/60;authority.beginTick(tick,now);check(authority.input(1,batch.inputs.front()),"admit ordinary input");for(const auto& action:batch.actions){AdmittedAction admitted{action,batch.inputs.front(),now};authority.action(1,admitted,now);}authority.commitContacts();
    for(int step=0;step<2;++step){if(step){++tick;authority.beginTick(tick,1000+tick*1000/60);check(authority.input(1,batch.inputs.front()),"held movement each physics tick");}game.TotalTime+=1.f/60;game.LevelInfo->TimeSeconds()+=1.f/60;game.Level->Tick(1.f/60,false);based|=p->ActorBase()==mover;rise=std::max(rise,std::abs(p->Location().z-origin.z));}
    check(p->Health()>0,"server rider survived");
    if(round%30==0)std::cout<<"LIFT_SERVER_FRAME round="<<round<<" xyz="<<p->Location().x<<","<<p->Location().y<<","<<p->Location().z<<" lift_z="<<mover->Location().z<<" base="<<(p->ActorBase()?p->ActorBase()->Name.ToString():"none")<<" state="<<mover->GetStateName().ToString()<<" key="<<int(mover->GetByte("KeyNum"))<<std::endl;
    if(complete){std::cout<<"LIFT_SERVER_COMPLETE based="<<based<<" rise="<<rise<<" distance="<<length(p->Location()-goal)<<" actor_base="<<(p->ActorBase()?p->ActorBase()->Name.ToString():"none")<<" xyz="<<p->Location().x<<","<<p->Location().y<<","<<p->Location().z<<" goal="<<goal.x<<","<<goal.y<<","<<goal.z<<std::endl;check(based&&rise>expectedTravel*.8f&&length(p->Location()-goal)<60&&p->ActorBase()!=mover,"server confirms real boarding/ride/exit");std::cout<<"BOT_LIFT_PROCESS_SERVER_OK rounds="<<round<<" based="<<based<<" rise="<<rise<<" distance="<<length(p->Location()-goal)<<std::endl;return;}
   }
   check(false,"server lift deadline");
  } else {
   check(role=="client","role");game.NetworkClient=true;
   for(auto* actor:game.Level->Actors)if(actor&&(actor->IsA("Pawn")||actor->IsA("Inventory")||actor->IsA("Projectile"))){game.Level->Collision.RemoveFromCollision(actor);actor->bHidden()=true;actor->NetworkReplica=true;}
   RuneNetworkReplica replica;uint64_t actionId=0;bool initialized=false;
   for(unsigned round=1;round<=1800;++round) {
    auto bytes=read(folder/("state-"+std::to_string(round)));size_t offset=0;EntityRef ref;ref.id=get(bytes,offset);ref.generation=get(bytes,offset);vec3 goal;for(int axis=0;axis<3;++axis)goal[axis]=float(int32_t(get(bytes,offset)))/8;
    auto count=get(bytes,offset);check(count<10000,"actor event cap");
    for(unsigned i=0;i<count;++i){auto encoded=block(bytes,offset);ActorEvent e;check(decodeActorEvent(1,encoded,e),"decode actor");if(e.operation==ActorOperation::Spawn)check(replica.spawn(game,e)!=nullptr,"client spawn");else if(e.operation==ActorOperation::Appearance)check(replica.appearance(game,e),"client appearance");else check(replica.unbind(e.ref),"client despawn");}
    auto encoded=block(bytes,offset);Snapshot snapshot;check(decodeSnapshot(encoded,nullptr,snapshot)&&offset==bytes.size(),"decode snapshot");check(replica.install(snapshot),"client installs complete world");replica.present(1);
    auto* view=UObject::TryCast<UPlayerPawn>(replica.actor(ref));check(view,"client view");
    if(!initialized){for(const auto& e:snapshot.world)if(e.ref==ref)brain.life=e.life;brain.waypoint=goal;brain.patrolAt=UINT64_MAX;brain.equipmentUntil=0;initialized=true;}
    MatchStatus status;status.mode=GameMode::TeamDeathmatch;status.phase=MatchPhase::Playing;status.scores={{1,0,0,0,255,ref}};
    brain.waypoint=goal;brain.patrolAt=UINT64_MAX;
    auto now=1000+snapshot.tick*1000/60;auto decision=brain.think(game,replica,snapshot.world,status,ref,now);if(round%30==0)std::cout<<"LIFT_CLIENT_FRAME round="<<round<<" phase="<<brain.liftPhase<<" active="<<brain.liftActive<<" view_z="<<view->Location().z<<std::endl;
    auto input=decision.input;input.sequence=round;input.createdMs=now;
    bool complete=brain.liftTrips>0&&brain.liftActive<0&&length(view->Location()-goal)<60;
    InputBatch batch;batch.inputs.push_back(input);if(decision.action)batch.actions.push_back(Action{++actionId,input.sequence,now,input.life,*decision.action});Bytes inputBytes;check(encodeInputs(1,batch,inputBytes),"encode inputs");Bytes frame;put(frame,complete);block(frame,inputBytes);write(folder/("input-"+std::to_string(round)),frame);
    if(complete){std::cout<<"BOT_LIFT_PROCESS_CLIENT_OK trips="<<brain.liftTrips<<" waits="<<brain.liftWaits<<" actor_base="<<(view->ActorBase()?view->ActorBase()->Name.ToString():"none")<<" max_think_us="<<brain.maxThinkUs<<std::endl;check(!view->ActorBase(),"client navigation must not rely on authoritative base");return;}
   }
   check(false,"client lift deadline");
  }
 }

 static void Run(Engine& game,UPlayerPawn* p) {
  auto check=[](bool v,const char* why){if(!v)throw std::runtime_error(std::string("lift bot: ")+why);};
  if(commandline->HasArg("","--traversal-roam")){Roam(game,p);return;}
  Brain brain(73);brain.debug=true;brain.discoverLifts(game,p);
  for(auto& plan:brain.lifts)for(size_t i=0;i<plan.stops.size();++i)for(auto landing:plan.stops[i].landings)
   std::cout<<"TRAV_LANDING actor="<<plan.actor->Name.ToString()<<" stop="<<i<<" center="<<plan.stops[i].center.x<<","<<plan.stops[i].center.y<<","<<plan.stops[i].center.z<<" landing="<<landing.x<<","<<landing.y<<","<<landing.z<<std::endl;
  if(commandline->HasArg("","--traversal-process")){Process(game,p);return;}
  auto name=commandline->GetArg("","--traversal-lift","");if(name.empty()){std::cout<<"BOT_LIFT_PLANS_OK count="<<brain.lifts.size()<<"\n";return;}
  auto plan=std::find_if(brain.lifts.begin(),brain.lifts.end(),[&](auto& x){return x.actor->Name==name;});check(plan!=brain.lifts.end(),"requested lift has no validated plan");
  const int from=commandline->GetArgInt("","--traversal-from",0),to=1-from;check(from==0||from==1,"stop index");
  auto start=plan->stops[from].landings[0],goal=plan->stops[to].landings[0];
  check(p->SetLocation(start),"initial safe landing");p->Velocity()=p->Acceleration()=vec3(0);p->SetPhysics(PHYS_Walking);
  // Optional start-away condition is staged by a second pawn standing on the
  // platform through normal landing/Attach physics. No mover callback is invoked.
  auto* m=plan->actor;
  if(commandline->HasArg("","--traversal-away")) {
   auto* helper=game.LoginNetworkPlayer("PreviousRider");check(helper,"previous rider");
   auto box=m->Brush()->BoundingBox.transform(GetMovingBrushTransform(m));auto at=box.center();at.z=box.max.z+helper->CollisionHeight()+2;
   check(helper->SetLocation(at),"stage previous rider");helper->SetPhysics(PHYS_Falling);helper->Velocity()=vec3(0);
   for(int i=0;i<40;++i){game.TotalTime+=1.f/60;game.LevelInfo->TimeSeconds()+=1.f/60;game.Level->Tick(1.f/60,false);}
   check(helper->SetLocation(start+vec3(200,0,0)),"previous rider leaves fixture");helper->SetCollision(false,false,false);
  }
  using namespace openrune::net;
  RuneNetworkAuthority authority(game);check(authority.bind(1,p,1),"authority bind");RuneNetworkReplica replica;World world;
  uint64_t ticks=1,now=1000,action=0,snapshotId=0;auto ref=authority.identity(p);
  auto snapshot=[&](){
   check(authority.capture(world),"capture");
   std::erase_if(world,[&](const auto& e){return e.ref!=ref && !((e.kind==EntityKind::Weapon||e.kind==EntityKind::Shield||e.kind==EntityKind::Limb)&&e.owner==ref);});
   uint64_t released=0;
   for(const auto& e:authority.pendingActors()) {
    // Keep real map machinery authoritative in this single-world fixture.
    // The bot still sees a normal pawn replica and serialized public world.
    bool represent=e.kind==EntityKind::Pawn||e.kind==EntityKind::Weapon||e.kind==EntityKind::Shield||e.kind==EntityKind::Limb;
    if(represent){if(e.operation==ActorOperation::Spawn)check(replica.spawn(game,e)!=nullptr,"spawn replica");else if(e.operation==ActorOperation::Appearance)check(replica.appearance(game,e),"appearance");else check(replica.unbind(e.ref),"despawn");}
    released=e.id;
   }
   if(released)check(authority.releaseActorsThrough(released),"release");
   check(replica.install(Snapshot{++snapshotId,0,ticks,world}),"snapshot install");replica.present(1);
   for(auto* a:game.Level->Actors)if(a&&a->NetworkReplica&&a->IsA("Pawn"))game.Level->Collision.RemoveFromCollision(a);
  };
  snapshot();auto* view=UObject::Cast<UPlayerPawn>(replica.actor(ref));brain.life=p->GetInt("Health")>0?world.front().life:1;
  for(const auto& e:world)if(e.ref==ref)brain.life=e.life;
  brain.waypoint=goal;brain.patrolAt=UINT64_MAX;brain.equipmentUntil=0;
  MatchStatus status;status.mode=GameMode::TeamDeathmatch;status.phase=MatchPhase::Playing;status.scores={{1,0,0,0,255,ref}};
  bool based=false;float rise=0;auto origin=p->Location();
  for(int i=0;i<3600&&!brain.liftTrips;++i) {
   now=1000+(++ticks*1000)/60;authority.beginTick(ticks,now);authority.commitContacts();
   if(i%2==0)snapshot();auto decision=brain.think(game,replica,world,status,ref,now);
   auto input=decision.input;input.sequence=ticks;input.createdMs=now;check(authority.input(1,input),"normal input");
   if(decision.action){AdmittedAction request{{++action,input.sequence,now,input.life,*decision.action},input,now};authority.action(1,request,now);}
   game.TotalTime+=1.f/60;game.LevelInfo->TimeSeconds()+=1.f/60;game.Level->Tick(1.f/60,false);
   based|=p->ActorBase()==m;rise=std::max(rise,std::abs(p->Location().z-origin.z));
   if(i%60==0)std::cout<<"BOT_LIFT_FRAME f="<<i<<" phase="<<brain.liftPhase<<" active="<<brain.liftActive<<" xyz="<<p->Location().x<<","<<p->Location().y<<","<<p->Location().z<<" mover_z="<<m->Location().z<<" base="<<(p->ActorBase()?p->ActorBase()->Name.ToString():"none")<<" state="<<p->GetStateName().ToString()<<std::endl;
   check(p->Health()>0,"player died on lift route");
  }
  std::cout<<"BOT_LIFT_RESULT actor="<<m->Name.ToString()<<" from="<<from<<" trips="<<brain.liftTrips<<" waits="<<brain.liftWaits<<" based="<<based<<" rise="<<rise<<" seconds="<<ticks/60.f<<" distance="<<length(p->Location()-goal)<<" max_think_us="<<brain.maxThinkUs<<std::endl;
  check(brain.liftTrips>0&&based&&rise>100&&length(p->Location()-goal)<60,"normal bot did not board/ride/exit");
  std::cout<<"BOT_LIFT_OK normal_input original_touch_attach observed_replica no_mover_callbacks\n";
 }
};
}
void RunRuneBotTraversalProbe(Engine& game) {
 auto check=[](bool v,const char* why){if(!v)throw std::runtime_error(std::string("bot traversal: ")+why);};
 game.NetworkAuthority=true;
 auto url=game.GetDefaultURL(commandline->GetArg("","--traversal-map","DM-LostCrypt.run"));url.AddOrReplaceOption("game=RuneI.TeamGame");game.LoadMap(url);
 auto* p=game.LoginNetworkPlayer("LiftRunner");check(p,"player");game.viewport->Actor()=p;
 CallEvent(game.GameInfo,"StartPlayer",{ExpressionValue::ObjectValue(p)});
 for(int i=0;i<60;++i){game.TotalTime+=1.f/60;game.LevelInfo->TimeSeconds()+=1.f/60;game.Level->Tick(1.f/60,false);}
 if((commandline->HasArg("","--traversal-plans")||commandline->HasArg("","--traversal-lift"))&&!commandline->HasArg("","--traversal-inventory")){RuneBot::TraversalFixture::Run(game,p);return;}
 if(commandline->HasArg("","--traversal-events"))for(auto* actor:game.Level->Actors)if(actor&&!actor->bDeleteMe()) {
  auto text=actor->PrintProperties();auto lower=text;for(auto& c:lower)c=char(std::tolower(static_cast<unsigned char>(c)));
  if(lower.find("lift")!=std::string::npos)std::cout<<"TRAV_PROPERTIES actor="<<actor->Name.ToString()<<" class="<<actor->Class->Name.ToString()<<"\n"<<text<<"\n";
 }
 for(auto* actor:game.Level->Actors)if(auto* m=GetMovingBrush(actor);m && m->Brush()) {
  auto box=m->Brush()->BoundingBox.transform(GetMovingBrushTransform(m));
  std::cout<<"TRAV_MOVER name="<<m->Name.ToString()<<" tag="<<m->Tag().ToString()<<" state="<<m->GetStateName().ToString()<<" keys="<<int(m->GetByte("NumKeys"))<<" key="<<int(m->GetByte("KeyNum"))<<" base="<<m->GetVector("BasePos").x<<","<<m->GetVector("BasePos").y<<","<<m->GetVector("BasePos").z<<" location="<<m->Location().x<<","<<m->Location().y<<","<<m->Location().z<<" top="<<box.center().x<<","<<box.center().y<<","<<box.max.z<<" extent="<<box.extents().x<<","<<box.extents().y<<","<<box.extents().z<<" use="<<m->GetBool("bUseTriggered")<<" bump="<<(m->HasProperty("BumpType")?int(m->GetByte("BumpType")):int(m->GetByte("PolyObjBumpType")))<<" once="<<m->GetBool("bTriggerOnceOnly")<<" stay="<<m->GetFloat("StayOpenTime")<<"\n";
  for(int k=0;k<std::min(8,int(m->GetByte("NumKeys")));++k){auto d=MovingBrushKeyPos(m,k);std::cout<<"TRAV_KEY name="<<m->Name.ToString()<<" key="<<k<<" delta="<<d.x<<","<<d.y<<","<<d.z<<" rot="<<MovingBrushKeyRot(m,k).Pitch<<","<<MovingBrushKeyRot(m,k).Yaw<<","<<MovingBrushKeyRot(m,k).Roll<<"\n";}
  for(auto* t:game.Level->Actors)if(t && t->Event()==m->Tag() && t!=m)std::cout<<"TRAV_ACTIVATOR lift="<<m->Name.ToString()<<" name="<<t->Name.ToString()<<" class="<<t->Class->Name.ToString()<<" type="<<(t->HasProperty("TriggerType")?int(t->GetByte("TriggerType")):-1)<<" active="<<(t->HasProperty("bInitiallyActive")?t->GetBool("bInitiallyActive"):false)<<" xyz="<<t->Location().x<<","<<t->Location().y<<","<<t->Location().z<<"\n";
 }
 std::cout<<"BOT_TRAVERSAL_INVENTORY_OK\n";
 if(commandline->HasArg("","--traversal-plans"))RuneBot::TraversalFixture::Run(game,p);
}
#endif
