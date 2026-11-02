#include "Precomp.h"
#include "RuneNetwork.h"
#include "Engine.h"
#include "RuneKick.h"
#include "RuneBash.h"
#include "Package/PackageManager.h"
#include "Package/Package.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "VM/ScriptCall.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
#include <deque>
#include <random>
#if !defined(_WIN32)
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#endif

using namespace openrune::net;
namespace
{
void check(bool condition,const char* message)
{ if(!condition)throw std::runtime_error(std::string("network process fixture: ")+message); }
uint64_t milliseconds()
{ return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count()); }
#if !defined(_WIN32)
struct LoopbackSocket
{
    int fd=-1;
    sockaddr_in peer{};
    bool known=false;
    unsigned latencyMs=0, lossPercent=0;
    std::mt19937 random;
    std::deque<std::pair<uint64_t,Bytes>> delayed;
    explicit LoopbackSocket(uint16_t port,unsigned latency,unsigned loss,bool server):
        latencyMs(latency),lossPercent(loss),random(server?72931:86427)
    {
        fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);check(fd>=0,"UDP socket");
        sockaddr_in local{};local.sin_family=AF_INET;local.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        check(!bind(fd,reinterpret_cast<sockaddr*>(&local),sizeof(local)),"loopback bind");
        check(fcntl(fd,F_SETFL,O_NONBLOCK)!=-1,"nonblocking UDP");
        if(port){peer=local;peer.sin_port=htons(port);known=true;}
    }
    ~LoopbackSocket(){if(fd>=0)close(fd);}
    uint16_t port() const
    {
        sockaddr_in local{};socklen_t length=sizeof(local);
        check(!getsockname(fd,reinterpret_cast<sockaddr*>(&local),&length),"local port");
        return ntohs(local.sin_port);
    }
    void receive(SecureSession& session)
    {
        for(unsigned n=0;n<64;++n)
        {
            std::array<uint8_t,1201> bytes{};sockaddr_in source{};socklen_t length=sizeof(source);
            auto count=recvfrom(fd,bytes.data(),bytes.size(),0,reinterpret_cast<sockaddr*>(&source),&length);
            if(count<0){if(errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR)break;throw std::runtime_error("UDP receive");}
            check(source.sin_family==AF_INET && source.sin_addr.s_addr==htonl(INADDR_LOOPBACK),"non-loopback source");
            if(!known){peer=source;known=true;}
            check(peer.sin_port==source.sin_port,"unexpected UDP peer");
            session.receiveDatagram(std::span(bytes).first(size_t(count)));
        }
    }
    void send(SecureSession& session)
    {
        if(!known)return;
        for(auto& bytes:session.takeDatagrams())
            if(random()%100>=lossPercent)delayed.emplace_back(milliseconds()+latencyMs,std::move(bytes));
        while(!delayed.empty() && delayed.front().first<=milliseconds())
        {
            auto bytes=std::move(delayed.front().second);delayed.pop_front();
            check(sendto(fd,bytes.data(),bytes.size(),0,reinterpret_cast<sockaddr*>(&peer),sizeof(peer))==ssize_t(bytes.size()),"UDP send");
        }
    }
};
#endif
World duelWorld(RuneNetworkAuthority& authority,UPlayerPawn* first,UPlayerPawn* second)
{
    World world;check(authority.capture(world),"world capture");
    check(world.size()>=2 && validWorld(world) && authority.identity(first).id && authority.identity(second).id,
        "duel actors and equipment snapshot");return world;
}
void placeDuel(UPlayerPawn* first,UPlayerPawn* second)
{
    auto start=first->Location();
    for(int angle=0;angle<16;++angle)
    {
        float rad=angle*3.14159265f/8;vec3 forward{std::cos(rad),std::sin(rad),0};
        auto nearValue=start+forward*75.0f,farValue=start+forward*200.0f;
        if(!first->CheckLocation(nearValue,22,42,true).first || !first->CheckLocation(farValue,22,42,true).first || !first->FastTrace(farValue,start))continue;
        if(!second->SetLocation(nearValue))continue;
        auto rotation=Rotator::FromVector(forward);
        first->SetRotation(rotation);first->ViewRotation()=rotation;
        second->SetRotation(Rotator(0,rotation.Yaw+32768,0));
        first->Velocity()=second->Velocity()=vec3(0);
        first->SetPhysics(PHYS_Walking);second->SetPhysics(PHYS_Walking);
        check(RuneKick::Target(first)==second,"duel target selection");return;
    }
    throw std::runtime_error("network process fixture: no clear duel lane");
}
void grantShield(Engine& game,UPlayerPawn* pawn)
{
    if(pawn->GetUObject("Shield"))return;
    auto* shield=pawn->Spawn(game.packages->GetPackage("RuneI")->GetClass("VikingShield"),{},{},pawn->Location()+vec3(0,0,100),Rotator(0,0,0));
    check(shield!=nullptr,"shield spawn");
    auto* copy=CallEvent(shield,"SpawnCopy",{ExpressionValue::ObjectValue(pawn)}).ToObject();
    check(copy!=nullptr,"shield copy");
    CallEvent(pawn,"AcquireInventory",{ExpressionValue::ObjectValue(copy)});copy->GotoState("Idle","Begin");
    check(pawn->GetUObject("Shield")!=nullptr,"shield equipped");
}
}
void RunRuneNetworkProcessProbe(Engine& game,const std::string& role,uint16_t port,unsigned oneWayLatencyMs,unsigned lossPercent)
{
#if defined(_WIN32)
    (void)game;(void)role;(void)port;(void)oneWayLatencyMs;(void)lossPercent;
    throw std::runtime_error("network process fixture currently requires POSIX loopback");
#else
    bool server=role.starts_with("server"), natural=role.ends_with("-natural");
    bool dual=role.starts_with("server-dual"), secondClient=role.starts_with("client-two");
    game.NetworkAuthority=server;
    auto url=game.GetDefaultURL("DM-Hildir.run");url.Options.push_back("game=RuneI.RuneMultiPlayer");
    game.LoadMap(url);
    UPlayerPawn *first=nullptr,*second=nullptr;
    std::unique_ptr<RuneNetworkAuthority> authority;
    if(server)
    {
        first=game.LoginNetworkPlayer("PeerOne");second=game.LoginNetworkPlayer("PeerTwo");
        check(first && second,"server pawns");
        game.viewport->Actor()=first;first->bShowMenu()=second->bShowMenu()=false;
        placeDuel(first,second);grantShield(game,first);
        authority=std::make_unique<RuneNetworkAuthority>(game);
        check(authority->bind(1,first,1) && authority->bind(2,second,1),"pawn ownership");
    }
    vec3 firstOrigin=server?first->Location():vec3(0),secondOrigin=server?second->Location():vec3(0);
    LoopbackSocket socket(port,oneWayLatencyMs,lossPercent,server);
    if(server)std::cout<<"PORT "<<socket.port()<<std::endl;
    std::unique_ptr<LoopbackSocket> secondSocket;
    if(dual)
    {secondSocket=std::make_unique<LoopbackSocket>(0,oneWayLatencyMs,lossPercent,true);
        std::cout<<"PORT2 "<<secondSocket->port()<<std::endl;}
    InvitationKey key{};for(size_t i=0;i<key.size();++i)key[i]=uint8_t(i+1); // Public loopback fixture key only.
    SecureSession session(server,key,secondClient?"engine-duel-fixture-2":"engine-duel-fixture",
        server?Bytes{1,2}:(secondClient?Bytes{1,5}:Bytes{1,3}),1);
    auto match=MakeRuneMatchContract(game,"DM-Hildir",2);
    ReplicationPeer peer(session,match);
    std::unique_ptr<SecureSession> secondSession;
    std::unique_ptr<ReplicationPeer> secondPeer;
    if(dual)
    {
        secondSession=std::make_unique<SecureSession>(true,key,"engine-duel-fixture-2",Bytes{1,4},1);
        secondPeer=std::make_unique<ReplicationPeer>(*secondSession,match);
    }
    RuneNetworkReplica replica;
    std::map<uint32_t,UActor*> proxies;
    if(server)
    {
        auto world=duelWorld(*authority,first,second);
        check(peer.snapshots.capture(world,1) && (!dual || secondPeer->snapshots.capture(world,1)),"initial duel snapshot");
    }
    FixedSimulation simulation;
    bool kickStarted=false,bashStarted=false,kickResolved=false,bashResolved=false;
    bool kickSent=false,bashSent=false;
    std::vector<CombatOutcome> received;
    std::map<uint64_t,Admission> actionResults;
    std::optional<std::array<int32_t,3>> initialClientPose;
    std::optional<Snapshot> previousClientSnapshot;
    uint16_t initialClientYaw=0;
    uint64_t lastSnapshotAt=0,snapshotSpacing=33,nextPresentation=0;
    size_t presentedFrames=0,interpolatedFrames=0;
    auto start=milliseconds();uint64_t lastInput=0;
    while(milliseconds()-start<14000)
    {
        auto t=milliseconds()-start;
        socket.receive(session);
        if(dual)secondSocket->receive(*secondSession);
        session.step(t+(server?10000:50000));
        if(dual)secondSession->step(t+10000);
        check(!session.failed(),"authenticated session");
        if(dual)check(!secondSession->failed(),"second authenticated session");
        if(!server && peer.world.candidate())
        {
            auto snapshot=*peer.world.candidate();
            bool ready=std::all_of(snapshot.world.begin(),snapshot.world.end(),[&](const Entity& e){return proxies.contains(e.ref.id);});
            if(ready)
            {
                if(peer.world.installed())previousClientSnapshot=*peer.world.installed();
                if(!replica.install(snapshot))
                {
                    for(const auto& e:snapshot.world)
                        std::cerr<<"INSTALL_ENTITY id="<<e.ref.id<<" kind="<<int(e.kind)<<" class="<<proxies.at(e.ref.id)->Class->Name.ToString()
                            <<" anim="<<e.bodyAnimation<<" torso="<<e.torsoAnimation<<" owner="<<e.owner.id<<" shield="<<e.shield.id<<'\n';
                    throw std::runtime_error("network process fixture: duel snapshot install");
                }
                check(peer.world.commit(snapshot.id),"duel snapshot commit");
                if(lastSnapshotAt)snapshotSpacing=std::clamp<uint64_t>(t-lastSnapshotAt,16,100);
                lastSnapshotAt=t;
                if(!initialClientPose && snapshot.world.size()>1)
                {auto& owned=snapshot.world[secondClient?1:0];
                    initialClientPose=owned.position;initialClientYaw=owned.angles[1];}
            }
        }
        if(!server && peer.world.installed() && t>=lastInput+20)
        {
            Input input;input.life=1;input.yaw=initialClientYaw;
            if(t>=8000 && t<9000)
            {if(secondClient)input.strafe=1000;else input.forward=-1000;}
            check(peer.inputs.capture(input,t+50000).has_value(),"duel input");lastInput=t;
            if(!secondClient)
            {
                if(t>=2500 && !kickSent){check(peer.inputs.act(ActionKind::Kick,t+50000).has_value(),"kick intent");kickSent=true;}
                if(t>=6500 && !bashSent){check(peer.inputs.act(ActionKind::Bash,t+50000).has_value(),"bash intent");bashSent=true;}
            }
        }
        peer.step();
        if(dual)secondPeer->step();
        if(server)
        {
            uint64_t published=0;
            for(const auto& event:authority->pendingActors())
            {
                if(peer.lastActorPublished()<event.id && !peer.publishActor(event))break;
                if(dual && secondPeer->lastActorPublished()<event.id && !secondPeer->publishActor(event))break;
                published=event.id;
            }
            if(published)check(authority->releaseActorsThrough(published),"release delivered actor events");
        }
        else
        {
            for(const auto& event:peer.drainActors())
            {
                if(event.operation==ActorOperation::Appearance){check(replica.appearance(game,event),"replica appearance failed");continue;}
                if(event.operation==ActorOperation::Despawn)
                {
                    check(replica.unbind(event.ref),"replica despawn");proxies.erase(event.ref.id);
                    peer.world.resync();continue;
                }
                auto* package=game.packages->GetPackage(event.package);
                check(package!=nullptr,"spawn package");
                auto* actorClass=package->GetClass(event.actorClass);
                check(actorClass!=nullptr,"spawn class");
                auto* actor=UObject::Cast<UActor>(game.packages->GetTransientPackage()->NewObject(
                    "DuelReplica"+std::to_string(event.ref.id)+"_"+std::to_string(event.ref.generation),
                    actorClass,ObjectFlags::Transient));
                check(actor!=nullptr,"spawn actor");
                actor->XLevel()=game.Level;actor->Level()=game.LevelInfo;
                actor->Index=int(game.Level->Actors.size());game.Level->Actors.push_back(actor);
                if(!event.proxyClass.empty())
                {
                    auto* proxyPackage=game.packages->GetPackage(event.proxyPackage);
                    check(proxyPackage!=nullptr,"animation proxy package");
                    auto* proxyClass=proxyPackage->GetClass(event.proxyClass);
                    check(proxyClass!=nullptr,"animation proxy class");
                    auto* proxy=UObject::Cast<UActor>(game.packages->GetTransientPackage()->NewObject(
                        "DuelProxy"+std::to_string(event.ref.id)+"_"+std::to_string(event.ref.generation),
                        proxyClass,ObjectFlags::Transient));
                    check(proxy!=nullptr,"animation proxy spawn");
                    proxy->XLevel()=game.Level;proxy->Level()=game.LevelInfo;proxy->Owner()=actor;
                    proxy->NetworkReplica=true;proxy->bHidden()=true;
                    actor->SetObject("AnimProxy",proxy);
                    proxy->Index=int(game.Level->Actors.size());game.Level->Actors.push_back(proxy);
                }
                check(replica.bind(event.ref,actor),"replica binding");proxies[event.ref.id]=actor;
            }
        }
        if(server)simulation.advance(t*1000,[&](uint64_t tick,float dt)
        {
            authority->beginTick(tick,t+10000);
            auto held=peer.commands.held(t+10000);
            if(!held.sequence)held.yaw=uint16_t(first->ViewRotation().Yaw);
            check(authority->input(1,held),"continuous owned movement");
            if(dual)
            {
                auto secondHeld=secondPeer->commands.held(t+10000);
                if(!secondHeld.sequence)secondHeld.yaw=uint16_t(second->ViewRotation().Yaw);
                check(authority->input(2,secondHeld),"second owned movement");
                for(auto action:secondPeer->commands.drainActions(t+10000))
                {
                    bool started=authority->action(2,action,t+10000);
                    check(secondPeer->decideAction(action.action.id,
                        started?Admission::Started:Admission::ExecutionRejected),"second action decision");
                }
            }
            for(auto a:peer.commands.drainActions(t+10000))
            {
                if(a.action.kind==ActionKind::Kick)
                {
                    check(!kickStarted && authority->action(1,a,t+10000),"real kick start");
                    check(peer.decideAction(a.action.id,Admission::Started),"kick action decision");
                    kickStarted=true;
                }
                else if(a.action.kind==ActionKind::Bash)
                {
                    check(kickResolved && !bashStarted,"bash ordering");
                    if(!natural){first->RuneKickActive=first->RuneKickPending=false;first->GotoState("PlayerWalking","Begin");}
                    placeDuel(first,second);
                    check(authority->action(1,a,t+10000),"real bash start");
                    check(peer.decideAction(a.action.id,Admission::Started),"bash action decision");bashStarted=true;
                }
            }
            authority->commitContacts();
            game.LevelInfo->TimeSeconds()+=dt;game.TotalTime+=dt;game.Level->Tick(dt,false);
            for(auto cancelled:authority->drainCancelledActions())
            {
                auto* destination=cancelled.peer==1?&peer:(dual && cancelled.peer==2?secondPeer.get():nullptr);
                if(destination)check(destination->decideAction(cancelled.id,Admission::Cancelled),"cancelled move decision");
            }
            if(!natural && kickStarted && !kickResolved)
            {
                check(RuneKick::Target(first)==second && RuneKick::Notify(first),"real kick impact");
                kickResolved=true;
            }
            if(!natural && bashStarted && !bashResolved)
            {
                check(RuneKick::Target(first)==second && RuneBash::AnimationFrames(first,0,.13f),"real bash impact");
                bashResolved=true;
            }
            uint64_t publishedThrough=0;
            for(const auto& result:authority->pendingCombat())
            {
                check(result.target==authority->identity(second) && result.contact==CombatContact::Hit,
                    "actual unblocked target hit");
                if(peer.lastCombatPublished()<result.id && !peer.publishCombat(result))break;
                if(dual && secondPeer->lastCombatPublished()<result.id && !secondPeer->publishCombat(result))break;
                if(natural && result.move==CombatMove::Kick)kickResolved=true;
                if(natural && result.move==CombatMove::Bash)bashResolved=true;
                publishedThrough=result.id;
                std::cout<<"SERVER_HIT move="<<int(result.move)<<" health="<<result.targetHealth<<" id="<<result.id<<'\n';
            }
            if(publishedThrough)check(authority->releaseCombatThrough(publishedThrough),"release delivered combat");
            if(tick%2==0)
            {
                auto world=duelWorld(*authority,first,second);
                check(peer.snapshots.capture(world,tick+1) &&
                    (!dual || secondPeer->snapshots.capture(world,tick+1)),"duel world update");
            }
        });
        if(!server)
        {
            for(auto result:peer.inputs.drainResults())actionResults[result.id]=result.status;
            for(auto result:peer.drainCombat())check(replica.queueCombat(result,t),"bounded combat dependency queue");
            auto state=replica.flushCombat(t);
            check(state!=RuneNetworkReplica::CombatQueue::Fault,"combat dependency fault");
            if(state==RuneNetworkReplica::CombatQueue::Resync)peer.world.resync();
            for(auto outcome:replica.drainResolvedCombat())
            {
                received.push_back(outcome);
                std::cout<<"CLIENT_HIT move="<<int(outcome.move)<<" health="<<outcome.targetHealth<<" id="<<outcome.id<<'\n';
            }
            if(peer.world.installed() && t>=nextPresentation)
            {
                double alpha=std::clamp(double(t-lastSnapshotAt)/double(snapshotSpacing),0.0,1.0);
                replica.present(alpha);++presentedFrames;nextPresentation=t+8; // 120 Hz presentation fixture.
                if(previousClientSnapshot && alpha>.05 && alpha<.95)
                {
                    auto* current=peer.world.installed();
                    const auto& owned=current->world[secondClient?1:0];
                    auto old=std::find_if(previousClientSnapshot->world.begin(),previousClientSnapshot->world.end(),
                        [&](const Entity& entity){return entity.ref==owned.ref;});
                    if(old!=previousClientSnapshot->world.end() && old->position!=owned.position)
                    {
                        auto expected=interpolatePose(*old,owned,alpha);
                        auto actual=proxies.at(owned.ref.id)->Location();
                        check(std::abs(actual.x-expected.position[0])<.02 &&
                            std::abs(actual.y-expected.position[1])<.02,
                            "owned remote pawn renders interpolated pose");
                        ++interpolatedFrames;
                    }
                }
            }
        }
        socket.send(session);
        if(dual)secondSocket->send(*secondSession);
        if(server && kickResolved && bashResolved && t>11000)break;
        if(!server && received.size()==2 && peer.world.installed() &&
            peer.world.installed()->tick>=received.back().tick+30 && t>11000)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if(server)
    {
        check(kickResolved && bashResolved && !authority->combatFaulted(),"two real server impacts");
        check(length((first->Location()-firstOrigin).xy())>20,"server applied continuous movement");
        if(dual)check(length((second->Location()-secondOrigin).xy())>20,"server applied second owned movement");
        std::cout<<"PASS ENGINE_DUEL_SERVER kick=1 bash=1 final_health="<<second->Health()<<'\n';
    }
    else
    {
        check(received.size()==2 && received[0].move==CombatMove::Kick &&
            received[1].move==CombatMove::Bash && received[0].targetHealth>received[1].targetHealth,
            "two client-confirmed impacts");
        if(!secondClient)check(actionResults[1]==Admission::Started && actionResults[2]==Admission::Started,
            "engine action decisions reached client");
        check(initialClientPose.has_value() && peer.world.installed() && !peer.world.installed()->world.empty() &&
            peer.world.installed()->world.size()>size_t(secondClient?1:0) &&
            std::abs(peer.world.installed()->world[secondClient?1:0].position[0]-(*initialClientPose)[0])+
            std::abs(peer.world.installed()->world[secondClient?1:0].position[1]-(*initialClientPose)[1])>160,
            "client received moving player snapshots");
        check(presentedFrames>500,"replicas presented between snapshots at 120 Hz");
        check(interpolatedFrames>10,"remote player presented intermediate movement frames");
        auto target=proxies.find(received.back().target.id);
        check(target!=proxies.end() && UObject::Cast<UPawn>(target->second)->Health()==received.back().targetHealth,
            "client health converged");
        auto attacker=proxies.find(received.front().attacker.id);
        auto* shield=attacker!=proxies.end()?UObject::TryCast<UActor>(attacker->second->GetUObject("Shield")):nullptr;
        check(shield && shield->Owner()==attacker->second && proxies.size()>=5,
            "replicated character equipment");
        std::cout<<"PASS ENGINE_DUEL_CLIENT kick=1 bash=1 final_health="<<received.back().targetHealth
            <<" actors="<<proxies.size()<<" shield=1 presented="<<presentedFrames
            <<" interpolated="<<interpolatedFrames<<'\n';
    }
#endif
}
