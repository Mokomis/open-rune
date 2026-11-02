#include "Precomp.h"
#include "RuneBot.h"
#include "RuneNetwork.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Package/Package.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Inventory/UWeapon.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UGameInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "VM/ScriptCall.h"
#include "Utils/CommandLine.h"
#include <iostream>
#include <stdexcept>
using namespace openrune::net;

// Accelerated objective diagnostic. The fixture grants a head and starts the
// round, but the ordinary bot must navigate and score through real input,
// physics, ThrowWeapon and the map's scoring zone. No awarded score/teleport.
void RunRuneBotGoalProbe(Engine& game)
{
    auto check=[](bool value,const char* why){if(!value)throw std::runtime_error(std::string("bot goal fixture: ")+why);};
    const bool navigation=commandline->HasArg("","--rune-bot-navigation-probe");
    game.NetworkAuthority=true;
    auto url=game.GetDefaultURL(navigation?"DM-Hildir.run":"HB-Agora.run");
    url.AddOrReplaceOption(navigation?"game=RuneI.TeamGame":"game=HeadBall.HeadBallGame");game.LoadMap(url);
    auto* player=game.LoginNetworkPlayer("GoalRunner");auto* other=game.LoginNetworkPlayer("GoalObserver");
    check(player && other,"players");game.viewport->Actor()=player;
    player->GetUObject("PlayerReplicationInfo")->SetByte("Team",0);
    other->GetUObject("PlayerReplicationInfo")->SetByte("Team",1);
    for(auto* p:{player,other})CallEvent(game.GameInfo,"StartPlayer",{ExpressionValue::ObjectValue(p)});
    if(!navigation)game.GameInfo->SetBool("bGameStarted",true);
    RuneNetworkAuthority authority(game);check(authority.bind(1,player,1)&&authority.bind(2,other,1),"bind");
    uint64_t ticks=0,now=1000,actionId=0,snapshotId=0;
    auto tick=[&](){now=1000+(++ticks*1000)/60;authority.beginTick(ticks,now);authority.commitContacts();
        game.LevelInfo->TimeSeconds()+=1.f/60;game.TotalTime+=1.f/60;game.Level->Tick(1.f/60,false);};
    for(int i=0;i<120;++i)tick();
    int start=commandline->GetArgInt("","--rune-bot-goal-spawn",-1);
    if(start>=0) {
        std::vector<UActor*> starts;
        for(auto* a:game.Level->Actors)if(a && a->IsA("PlayerStart"))starts.push_back(a);
        check(size_t(start)<starts.size(),"spawn index");check(player->SetLocation(starts[start]->Location()),"fixture start placement");
        player->Velocity()=vec3(0);player->SetPhysics(PHYS_Walking);for(int i=0;i<60;++i)tick();
    }
    if(commandline->HasArg("","--rune-bot-goal-upper")) {
        check(!navigation && player->SetLocation(vec3(-434.f,-251.5f,-229.5f)),"upper goal fixture placement");
        player->Velocity()=vec3(0);player->SetPhysics(PHYS_Walking);for(int i=0;i<60;++i)tick();
    }
    if(navigation) {
        auto start=commandline->HasArg("","--rune-bot-navigation-corner")?vec3(-694.125f,736.75f,-272.125f):vec3(-694.125f,720.875f,-341.f);
        if(commandline->HasArg("","--rune-bot-navigation-floor"))start=vec3(-694.125f,703.f,-341.f);
        check(player->SetLocation(start),"navigation fixture placement");
        if(commandline->HasArg("","--rune-bot-navigation-corner")){
            // Replay the exact standing correction captured from the bot log;
            // SetLocation otherwise nudges the fixture away from that ledge.
            game.Level->Collision.RemoveFromCollision(player);player->Location()=start;game.Level->Collision.AddToCollision(player);
        }
        player->Velocity()=vec3(0);player->SetPhysics(PHYS_Walking);
    } else {
    auto* item=player->Spawn(game.packages->GetPackage("RuneI")->GetClass("RagnarHead"),{},{},player->Location()+vec3(0,0,100),{});
    check(item,"head spawn");item->SetFloat("RespawnTime",0);
    auto* copy=CallEvent(item,"SpawnCopy",{ExpressionValue::ObjectValue(player)}).ToObject();check(copy,"head inventory copy");
    CallEvent(player,"AcquireInventory",{ExpressionValue::ObjectValue(copy)});
    for(int i=0;i<180;++i)tick();
    check(player->Weapon() && player->Weapon()->IsA("Head"),"head equipped");
    }
    const auto navigationStart=player->Location();
    RuneNetworkReplica replica;World world;
    auto snapshot=[&](){
        check(authority.capture(world),"capture");uint64_t released=0;
        for(const auto& event:authority.pendingActors()) {
            if(event.operation==ActorOperation::Spawn)check(replica.spawn(game,event)!=nullptr,"replica spawn");
            else if(event.operation==ActorOperation::Appearance)check(replica.appearance(game,event),"replica appearance");
            else check(replica.unbind(event.ref),"replica despawn");
            released=event.id;
        }
        if(released)check(authority.releaseActorsThrough(released),"release actors");
        check(replica.install(Snapshot{++snapshotId,0,ticks,world}),"replica install");replica.present(1);
        // Replica pawns must not obstruct the authoritative fixture actors.
        for(auto* a:game.Level->Actors)if(a && a->NetworkReplica && a->IsA("Pawn"))game.Level->Collision.RemoveFromCollision(a);
    };
    MatchStatus status;status.mode=navigation?GameMode::TeamDeathmatch:GameMode::HeadBall;status.phase=MatchPhase::Playing;status.headBallStarted=true;
    status.scores={{1,0,0,0,255,authority.identity(player)},{2,0,0,1,255,authority.identity(other)}};
    RuneBot::Brain brain(73,1);brain.debug=true;
    auto** teams=static_cast<UObject**>(game.GameInfo->GetProperty("Teams"));
    bool scored=false;
    for(int i=0;i<10800 && !scored;++i) {
        if(i%2==0)snapshot();
        auto decision=brain.think(game,replica,world,status,authority.identity(player),now);
        auto input=decision.input;input.sequence=ticks;input.createdMs=now;
        check(authority.input(1,input),"normal movement input");
        if(decision.action) {
            AdmittedAction request{{++actionId,input.sequence,now,input.life,*decision.action},input,now};
            bool accepted=authority.action(1,request,now);
            if(*decision.action==ActionKind::Throw)std::cout<<"BOT_GOAL_THROW accepted="<<accepted<<std::endl;
        }
        tick();scored=navigation?length(player->Location()-navigationStart)>250:teams[0]->GetFloat("Score")>0;
        check(player->Health()>0,"runner died before scoring");
    }
    std::cout<<"BOT_GOAL_RESULT scored="<<scored<<" seconds="<<ticks/60.f<<" position="<<player->Location().x<<","<<player->Location().y<<","<<player->Location().z<<" weapon="<<(player->Weapon()?player->Weapon()->Class->Name.ToString():"None")<<std::endl;
    check(scored,navigation?"did not escape within 180 simulated seconds":"did not score within 180 simulated seconds");
    if(navigation)std::cout<<"BOT_NAV_OK ordinary movement escaped the raised-geometry fixture\n";
    else std::cout<<"BOT_GOAL_OK normal movement, real thrown head, authored scoring zone\n";
}

void RunRuneBotTeamProbe(Engine& game)
{
    auto check=[](bool v,const char* why){if(!v)throw std::runtime_error(std::string("bot team fixture: ")+why);};
    game.NetworkAuthority=true;auto url=game.GetDefaultURL("DM-Hildir.run");url.AddOrReplaceOption("game=RuneI.TeamGame");game.LoadMap(url);
    auto* p=game.LoginNetworkPlayer("Support");auto* ally=game.LoginNetworkPlayer("Ally");auto* enemy=game.LoginNetworkPlayer("Enemy");game.viewport->Actor()=p;
    for(auto* a:{p,ally,enemy}){a->GetUObject("PlayerReplicationInfo")->SetByte("Team",a==enemy?1:0);CallEvent(game.GameInfo,"StartPlayer",{ExpressionValue::ObjectValue(a)});a->DisableEvent("PlayerTick");a->SetCollision(true,false,false);}
    auto tick=[&](){game.LevelInfo->TimeSeconds()+=1.f/60;game.TotalTime+=1.f/60;game.Level->Tick(1.f/60,false);};for(int i=0;i<60;i++)tick();
    vec3 origin,direction;bool lane=false;
    for(auto* a:game.Level->Actors)if(a && a->IsA("PlayerStart") && !lane){
        if(!p->SetLocation(a->Location()))continue;p->SetPhysics(PHYS_Walking);p->Velocity()=vec3(0);for(int i=0;i<60;i++)tick();
        for(int j=0;j<16&&!lane;j++){
            float angle=j*3.14159265f/8;direction={std::cos(angle),std::sin(angle),0};auto end=p->Location()+direction*400.f;vec3 hit,normal;
            lane=p->FastTrace(end,p->Location()) && p->CheckLocation(end,22,42,true).first && p->Trace(hit,normal,end-vec3(0,0,140),end,false,vec3(10,10,0)) && normal.z>.7f;
        }if(lane)origin=p->Location();
    }check(lane,"open approach");
    RuneNetworkAuthority authority(game);check(authority.bind(1,p,1)&&authority.bind(2,ally,1)&&authority.bind(3,enemy,1),"bindings");
    RuneNetworkReplica replica;World world;uint64_t id=0;
    auto snapshot=[&](float foeDistance,float allyDistance){
        check(p->SetLocation(origin)&&ally->SetLocation(origin+direction*allyDistance)&&enemy->SetLocation(origin+direction*foeDistance),"placement");
        for(auto* a:{p,ally,enemy}){a->SetRotation(Rotator::FromVector(a==enemy?-direction:direction));a->Velocity()=a->Acceleration()=vec3(0);}
        check(authority.capture(world),"capture");uint64_t released=0;
        for(const auto& event:authority.pendingActors()){
            if(event.operation==ActorOperation::Spawn)check(replica.spawn(game,event)!=nullptr,"spawn replica");
            else if(event.operation==ActorOperation::Appearance)check(replica.appearance(game,event),"appearance");
            else check(replica.unbind(event.ref),"despawn");released=event.id;
        }if(released)check(authority.releaseActorsThrough(released),"release");
        check(replica.install(Snapshot{++id,0,id,world}),"snapshot");replica.present(1);
        for(auto* a:game.Level->Actors)if(a && a->NetworkReplica && a->IsA("Pawn"))game.Level->Collision.RemoveFromCollision(a);
    };
    MatchStatus status;status.mode=GameMode::TeamDeathmatch;status.phase=MatchPhase::Playing;status.scores={{1,0,0,0,255,authority.identity(p)},{2,0,0,0,255,authority.identity(ally)},{3,0,0,1,255,authority.identity(enemy)}};
    snapshot(250,70);RuneBot::Brain brain(73);RuneBot::Decision laneDecision;
    for(uint64_t t=1000;t<=2700;t+=100)laneDecision=brain.think(game,replica,world,status,authority.identity(p),t);
    std::cout<<"BOT_TEAM_LANE forward="<<laneDecision.input.forward<<" strafe="<<laneDecision.input.strafe<<" action="<<(laneDecision.action?int(*laneDecision.action):0)<<std::endl;
    // Identical observations and seed must yield identical decisions after a
    // red/blue team swap. There is no special policy for the human's side.
    auto swapped=status;for(auto& score:swapped.scores)score.team=1-score.team;
    RuneBot::Brain a(19),b(19);
    for(uint64_t t=1000;t<3000;t+=100){auto x=a.think(game,replica,world,status,authority.identity(p),t),y=b.think(game,replica,world,swapped,authority.identity(p),t);check(x.input.forward==y.input.forward&&x.input.strafe==y.input.strafe&&x.input.yaw==y.input.yaw&&x.action==y.action,"team symmetry");}
    snapshot(60,350);RuneBot::Brain recovery(73);bool acted=false;uint64_t now=1000;
    for(;now<4000;now+=100)if(recovery.think(game,replica,world,status,authority.identity(p),now).action){acted=true;break;}
    check(acted,"ordinary combat action");snapshot(200,350);
    auto moving=recovery.think(game,replica,world,status,authority.identity(p),now+100);
    std::cout<<"BOT_TEAM_RECOVERY forward="<<moving.input.forward<<" strafe="<<moving.input.strafe<<" action="<<(moving.action?int(*moving.action):0)<<std::endl;
    if(commandline->HasArg("","--rune-fix-check")){check(laneDecision.input.forward>100,"support advances toward a flank");check(std::abs(moving.input.forward)+std::abs(moving.input.strafe)>100 && !moving.action,"recovery moves without a second attack");}
    snapshot(200,350);auto observed=world;
    for(auto& e:observed)if(e.ref==authority.identity(enemy))e.flags|=32;
    auto* visibleAlly=UObject::Cast<UPlayerPawn>(replica.actor(authority.identity(ally)));
    check(visibleAlly->Weapon()!=nullptr,"support fixture weapon");
    auto attack=visibleAlly->Weapon()->GetName("A_AttackStandA");check(!attack.IsNone(),"support fixture attack animation");
    visibleAlly->AnimSequence()=attack;RuneBot::Brain support(73);
    for(uint64_t t=1000;t<1800;t+=100)support.think(game,replica,observed,status,authority.identity(p),t);
    check(support.supportMoves>0,"observed teammate fight attracts support without a visible enemy");
    std::cout<<"BOT_TEAM_SUPPORT decisions="<<support.supportMoves<<std::endl;
    std::cout<<"BOT_TEAM_OK team symmetry and recovery/lane observations\n";
}


// Moving agents, real pawn collisions, real VM movement: a frozen one-decision
// lane test cannot detect reciprocal circling or a doorway traffic deadlock.
void RunRuneBotCrowdProbe(Engine& game)
{
    auto check=[](bool v,const char* why){if(!v)throw std::runtime_error(std::string("bot crowd fixture: ")+why);};
    game.NetworkAuthority=true;auto url=game.GetDefaultURL("DM-Hildir.run");url.AddOrReplaceOption("game=RuneI.TeamGame");game.LoadMap(url);
    std::vector<UPlayerPawn*> pawns;
    for(int i=0;i<5;++i){auto* p=game.LoginNetworkPlayer("Crowd"+std::to_string(i));check(p,"login");pawns.push_back(p);CallEvent(game.GameInfo,"StartPlayer",{ExpressionValue::ObjectValue(p)});p->SetCollision(true,false,false);}
    game.viewport->Actor()=pawns[0];
    uint64_t ticks=0,now=1000,snapshotId=0;RuneNetworkAuthority authority(game);
    for(int i=0;i<5;++i)check(authority.bind(i+1,pawns[i],1),"bind");
    auto tick=[&](){now=1000+(++ticks*1000)/60;authority.beginTick(ticks,now);authority.commitContacts();game.LevelInfo->TimeSeconds()+=1.f/60;game.TotalTime+=1.f/60;game.Level->Tick(1.f/60,false);};
    for(int i=0;i<120;++i)tick();
    vec3 origin,forward;bool lane=false;std::vector<UActor*> markers;
    for(auto* a:game.Level->Actors)if(a && a->IsA("PlayerStart"))markers.push_back(a);
    for(auto* a:markers)if(!lane){
        auto* p=pawns[0];if(!p->SetLocation(a->Location()))continue;p->SetPhysics(PHYS_Walking);p->Velocity()=vec3(0);for(int i=0;i<60;++i)tick();
        auto center=p->Location();
        for(int j=0;j<16 && !lane;++j){float angle=j*3.14159265f/8;vec3 f(std::cos(angle),std::sin(angle),0),side(-f.y,f.x,0);bool clear=true;
            for(float along:{-280.f,0.f,280.f})for(float across:{-64.f,0.f,64.f}){
                auto at=center+f*along+side*across;vec3 hit,normal;
                if(!p->CheckLocation(at,18,42,true).first || !p->FastTrace(at,center) || !p->Trace(hit,normal,at-vec3(0,0,60),at,false,vec3(4,4,0)) || normal.z<.7f || std::abs(hit.z-(center.z-p->CollisionHeight()))>4)clear=false;
            }if(clear){lane=true;origin=center;forward=f;}
        }
    }check(lane,"flat crossing lane");
    RuneNetworkReplica replica;World world;
    auto snapshot=[&](){check(authority.capture(world),"capture");uint64_t released=0;
        for(const auto& event:authority.pendingActors()){
            if(event.operation==ActorOperation::Spawn)check(replica.spawn(game,event)!=nullptr,"replica spawn");
            else if(event.operation==ActorOperation::Appearance)check(replica.appearance(game,event),"appearance");
            else check(replica.unbind(event.ref),"despawn");released=event.id;
        }if(released)check(authority.releaseActorsThrough(released),"release actors");
        check(replica.install(Snapshot{++snapshotId,0,ticks,world}),"install");replica.present(1);
        for(auto* a:game.Level->Actors)if(a && a->NetworkReplica && a->IsA("Pawn"))game.Level->Collision.RemoveFromCollision(a);
    };
    const bool grouped=commandline->HasArg("","--rune-bot-group-probe");
    const auto side=vec3(-forward.y,forward.x,0);int cases=0,passed=0;float worst=0,maxTravel=0;std::array<uint64_t,3> actions{};
    for(int seed=1;seed<=8;++seed)for(bool swapped:{false,true}){
        int selected=commandline->GetArgInt("","--crowd-case",grouped?0:-1);if(selected>=0 && selected!=(seed-1)*2+int(swapped))continue;
        for(auto* p:pawns){p->SetCollision(true,false,false);p->Health()=10000;p->GotoState("PlayerWalking","Begin");p->Velocity()=p->Acceleration()=vec3(0);p->aForward()=p->aStrafe()=0;p->bFire()=p->bAltFire()=0;p->SetPhysics(PHYS_Walking);p->Bloodlust={};p->RuneKickActive=false;p->RuneBashPhase=0;if(auto* proxy=p->GetUObject("AnimProxy"))proxy->GotoState("Idle","Begin");}
        std::array<vec3,5> positions={origin-forward*80.f-side*16.f,origin+forward*80.f+side*16.f,origin-forward*140.f+side*24.f,origin+forward*280.f,origin-forward*280.f};
        if(grouped)positions[1]=origin-forward*80.f+side*24.f;
        for(int i=0;i<5;++i){auto* p=pawns[i];check(p->SetLocation(positions[i]),"placement");auto f=((!grouped&&i==1)||i==4)?-forward:forward;p->SetRotation(Rotator::FromVector(f));p->ViewRotation()=p->Rotation();p->GetUObject("PlayerReplicationInfo")->SetByte("Team",(i<3?0:1)^int(swapped));}
        for(auto* p:pawns)p->SetCollision(true,true,true);
        pawns[3]->DisableEvent("PlayerTick");pawns[4]->DisableEvent("PlayerTick");
        MatchStatus status;status.mode=GameMode::TeamDeathmatch;status.phase=MatchPhase::Playing;
        for(int i=0;i<5;++i)status.scores.push_back({uint32_t(i+1),0,0,uint8_t((i<3?0:1)^int(swapped)),255,authority.identity(pawns[i])});
        std::array<RuneBot::Brain,3> brains={RuneBot::Brain(seed),RuneBot::Brain(seed+17),RuneBot::Brain(seed+31)};
        if(seed==1&&!swapped)brains[2].debug=true;
        std::array<bool,3> crossed{};std::array<float,3> travel{},crossTime{};auto previous=positions;bool complete=false;int frame=0;
        for(;frame<(grouped?1500:900) && !complete;++frame){if(frame%6==0){snapshot();for(int i=0;i<3;++i){auto observed=world;for(auto& e:observed)if(e.ref==authority.identity(pawns[!grouped&&i==1?3:4]))e.flags|=32;
                auto d=brains[i].think(game,replica,observed,status,authority.identity(pawns[i]),now);auto input=d.input;input.sequence=ticks+1;input.createdMs=now;check(authority.input(i+1,input),"input");if(d.action){AdmittedAction a{{++actions[i],input.sequence,now,input.life,*d.action},input,now};authority.action(i+1,a,now);}}}
            tick();for(int i=0;i<3;++i){if(!crossed[i])travel[i]+=length(pawns[i]->Location()-previous[i]);previous[i]=pawns[i]->Location();float along=dot(pawns[i]->Location()-origin,forward);bool arrived=grouped?brains[i].attacks+brains[i].specials>0:(i==1?along<-65:along>65);if(!crossed[i]&&arrived)crossTime[i]=(frame+1)/60.f;crossed[i]=crossed[i]||arrived;}
            complete=crossed[0]&&crossed[1]&&crossed[2];
        }
        ++cases;if(complete)++passed;worst=std::max(worst,frame/60.f);for(float distance:travel)maxTravel=std::max(maxTravel,distance);
        std::cout<<"BOT_CROWD_CASE grouped="<<grouped<<" seed="<<seed<<" swapped="<<swapped<<" completed="<<complete<<" seconds="<<frame/60.f<<" crossed="<<crossed[0]<<crossed[1]<<crossed[2]<<" travel="<<travel[0]<<","<<travel[1]<<","<<travel[2]<<" times="<<crossTime[0]<<","<<crossTime[1]<<","<<crossTime[2]<<std::endl;
    }
    std::cout<<"BOT_CROWD_RESULT passed="<<passed<<" cases="<<cases<<" worst_seconds="<<worst<<" max_travel="<<maxTravel<<std::endl;
    if(commandline->HasArg("","--rune-fix-check"))check(passed==cases,"not all agents cleared the opposing allied traffic");
}
