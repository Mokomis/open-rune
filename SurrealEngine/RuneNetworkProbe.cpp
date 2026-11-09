#include "Precomp.h"
#include "RuneNetwork.h"
#include "Engine.h"
#include "RuneFeedback.h"
#include "RuneKick.h"
#include "Packages/Engine/Actors/Brush/MovingBrush.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Resources/Level/UPolys.h"
#include "Package/PackageManager.h"
#include "Package/Package.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "VM/ScriptCall.h"
#include "Utils/Logger.h"
#include "Utils/CommandLine.h"
#include <iostream>
#include <stdexcept>
#include <set>
using namespace openrune::net;
namespace
{
void check(bool v,const char* what) { if(!v)throw std::runtime_error(std::string("network engine fixture: ")+what); }
}
static void RunCrouchDiveProbe(Engine& game)
{
    float standaloneDiveAxis=0;
    for(bool network:{false,true})for(bool swimming:{false,true}) {
        game.NetworkAuthority=network;
        auto url=game.GetDefaultURL(swimming?"SinkingShip2.run":"DM-Hildir.run");
        url.AddOrReplaceOption(network?"game=RuneI.RuneMultiPlayer":"game=RuneI.RuneSinglePlayer");
        game.LoadMap(url);game.LoadKeybindings();
        UPlayerPawn* pawn;
        if(network){pawn=game.LoginNetworkPlayer("CrouchProbe");game.viewport->Actor()=pawn;}
        else {game.LoginPlayer();pawn=game.viewport->Actor();}
        pawn->Health()=10000;pawn->bShowMenu()=false;
        std::unique_ptr<RuneNetworkAuthority> authority;
        if(network){authority=std::make_unique<RuneNetworkAuthority>(game);check(authority->bind(1,pawn,1),"crouch fixture bind");}
        uint64_t tick=0;bool held=false;
        auto step=[&](int frames){for(int n=0;n<frames;++n){
            if(network){
                authority->beginTick(++tick,1000+tick*17);Input input;input.life=1;input.sequence=tick;input.createdMs=1000+tick*17;input.held=held?4:0;
                check(authority->input(1,input),"crouch complete input admitted");authority->commitContacts();
            }else {
                ++tick;
                // Headless fixture has no OS event pump. Publish the real C
                // alias's held button/axis exactly as UpdateInput does, then run
                // the unmodified standalone PlayerInput/PlayerTick scripts.
                for(const auto& button:game.activeInputButtons)pawn->SetBool(button.first,true);
                for(const auto& axis:game.activeInputAxes)pawn->SetFloat(axis.first,axis.second.Value);
            }
            game.LevelInfo->TimeSeconds()+=1.f/60;game.TotalTime+=1.f/60;
            game.Level->Tick(1.f/60,false);
        }};
        auto key=[&](bool down){held=down;if(!network){if(down)game.OnWindowKeyDown(IK_C);else game.OnWindowKeyUp(IK_C);}};
        if(!swimming) {
            pawn->GotoState("PlayerWalking","Begin");pawn->SetPhysics(PHYS_Walking);step(60);
            float standing=pawn->CollisionHeight();
            key(true);step(20);
            std::cout<<"CROUCH_HELD network="<<network<<" crouched="<<bool(pawn->bIsCrouching())<<" height="<<pawn->CollisionHeight()<<" standing="<<standing<<std::endl;
            check(pawn->bDuck() && pawn->bIsCrouching() && pawn->CollisionHeight()<standing,"held crouch changes real stance/collision");
            key(false);step(20);
            check(!pawn->bDuck() && !pawn->bIsCrouching() && std::abs(pawn->CollisionHeight()-standing)<.01f,"release stands with clear headroom");
        }else {
            bool water=pawn->Region().Zone && pawn->Region().Zone->bWaterZone();
            if(!water)for(const auto& vertex:game.Level->Model->Points) {
                if(pawn->SetLocation(vertex+vec3(0,0,80)) && pawn->Region().Zone && pawn->Region().Zone->bWaterZone() && pawn->HeadRegion().Zone && pawn->HeadRegion().Zone->bWaterZone()) {water=true;break;}
            }
            check(water,"real water volume available");
            pawn->GotoState("PlayerSwimming","Begin");pawn->SetPhysics(PHYS_Swimming);
            pawn->SetBool("bSurfaceSwimming",false);pawn->Velocity()=pawn->Acceleration()=vec3(0);
            float start=pawn->Location().z;key(true);step(20);
            std::cout<<"DIVE_HELD network="<<network<<" aUp="<<pawn->aUp()<<" accel_z="<<pawn->Acceleration().z<<" displacement_z="<<pawn->Location().z-start<<std::endl;
            check(pawn->bDuck() && pawn->aUp()<0 && pawn->Acceleration().z<0 && pawn->Location().z<start,"held dive descends in real water");
            if(!network)standaloneDiveAxis=pawn->aUp();
            else check(std::abs(pawn->aUp()-standaloneDiveAxis)<.01f,"network dive matches original standalone axis strength");
            key(false);step(2);check(!pawn->bDuck() && std::abs(pawn->aUp())<.001f,"dive release clears downward input");
            if(network){held=true;step(1);authority->unbind(1);check(!pawn->bDuck() && pawn->aUp()==0,"disconnect clears dive");}
        }
        std::cout<<"CROUCH_DIVE_PASS network="<<network<<" swimming="<<swimming<<std::endl;
    }
}

void RunRuneNetworkProbe(Engine& game)
{
    if(commandline->HasArg("","--rune-crouch-probe")){RunCrouchDiveProbe(game);return;}
    game.NetworkAuthority=true;
    auto url=game.GetDefaultURL("DM-Hildir.run"); url.Options.push_back("game=RuneI.RuneMultiPlayer");
    game.LoadMap(url);
    check(game.LevelInfo->NetMode()==1,"dedicated mode before actor startup");
    if(commandline->HasArg("","--rune-net-mover-inspect"))for(auto* actor:game.Level->Actors)if(auto* b=GetMovingBrush(actor);b && b->Brush()) {
        auto box=b->Brush()->BoundingBox.transform(GetMovingBrushTransform(b));auto c=box.center(),e=box.extents();
        std::cout<<"MOVER_VIEW name="<<actor->Name.ToString()<<" center="<<c.x<<","<<c.y<<","<<c.z<<" extents="<<e.x<<","<<e.y<<","<<e.z<<" bake_key="<<int(b->GetByte("BrushRaytraceKey"))<<" dynamic="<<MovingBrushDynamicLight(b)<<" delay="<<(b->HasProperty("DelayTime")?b->GetFloat("DelayTime"):0)<<" state="<<b->GetStateName().ToString()<<" use="<<(b->HasProperty("bUseTriggered")?b->GetBool("bUseTriggered"):false)<<" glide="<<(b->HasProperty("MoverGlideType")?int(b->GetByte("MoverGlideType")):-1)<<std::endl;
        auto key=MovingBrushKeyPos(b,b->GetByte("BrushRaytraceKey"));std::cout<<"MOVER_KEY offset="<<key.x<<","<<key.y<<","<<key.z<<std::endl;
        if(b->Brush()->Polys)for(const auto& p:b->Brush()->Polys->Polys)if(p.Normal.z>.5f){
            std::cout<<"MOVER_TOP lm="<<p.BrushPolyIndex<<" texture="<<(p.Texture?p.Texture->Name.ToString():"none")<<std::endl;
        }
    }
    auto* first=game.LoginNetworkPlayer("PeerOne");
    auto* second=game.LoginNetworkPlayer("PeerTwo");
    game.viewport->Actor()=first;
    check(first!=second,"two distinct logged-in pawns");
    first->bShowMenu()=false; second->bShowMenu()=false;
    if(commandline->HasArg("","--rune-net-door-probe")) {
        auto* door=static_cast<UActor*>(nullptr);
        for(auto* a:game.Level->Actors)if(a && a->Name==NameString("Mover0"))door=a;
        check(door && door->GetStateName()=="TriggerControl" && door->GetFloat("DelayTime")==0,"automatic door fixture");
        auto start=door->Location();
        CallEvent(door,"Trigger",{ExpressionValue::ObjectValue(first),ExpressionValue::ObjectValue(first)});
        for(int n=1;n<=12;++n) {
            game.LevelInfo->TimeSeconds()+=1.f/60;game.TotalTime+=1.f/60;game.Level->Tick(1.f/60,false);
            if(n==1 || n==3 || n==6 || n==12)std::cout<<"RUNE_DOOR_ONSET frame="<<n<<" distance="<<length(door->Location()-start)<<" alpha="<<door->PhysAlpha()<<" opening="<<door->GetBool("bOpening")<<std::endl;
        }
        auto* brush=GetMovingBrush(door);auto end=door->GetVector("BasePos")+MovingBrushKeyPos(brush,door->GetByte("KeyNum"));
        check(length(door->Location()-(start+(end-start)*door->PhysAlpha()))<.05f,"automatic door starts linearly without slow easing");
        auto alpha=door->PhysAlpha();check(alpha>0 && alpha<1,"opening interval measured");
        std::cout<<"RUNE_DOOR_ONSET_OK delay_preserved="<<door->GetFloat("DelayTime")<<" move_time="<<door->GetFloat("MoveTime")<<std::endl;
        return;
    }
    RuneNetworkAuthority authority(game);
    check(authority.bind(1,first,1) && authority.bind(2,second,1),"owned pawn binding");
    check(!authority.bind(3,first,1),"duplicate ownership rejected");
    auto aStart=first->Location(), bStart=second->Location();
    std::cout<<"RUNE_NETWORK_READY actors="<<game.Level->Actors.size()<<" first="<<first->Name.ToString()<<" second="<<second->Name.ToString()<<'\n';
    auto key=generateInvitationKey();
    SecureSession server1(true,key,"one",{1,2},1),client1(false,key,"one",{1,3},1);
    SecureSession server2(true,key,"two",{1,4},1),client2(false,key,"two",{1,5},1);
    auto match=MakeRuneMatchContract(game,"DM-Hildir",2);
    ReplicationPeer host1(server1,match),player1(client1,match),host2(server2,match),player2(client2,match);
    World initial; check(authority.capture(initial),"initial capture");
    check(host1.snapshots.capture(initial,1) && host2.snapshots.capture(initial,1),"bootstrap worlds");
    RuneNetworkReplica replica;
    std::map<uint32_t,UActor*> proxyActors;
    auto install=[&](const Snapshot& snapshot)
    {
        for(const auto& entity:snapshot.world) if(!proxyActors.contains(entity.ref.id))
        {
            UActor* source=nullptr;
            for(auto* a:game.Level->Actors)if(a && authority.identity(a)==entity.ref){source=a;break;}
            check(source!=nullptr,"fixture archetype lookup");
            auto* proxy=UObject::Cast<UActor>(game.packages->GetTransientPackage()->NewObject("NetworkReplica"+std::to_string(entity.ref.id),source->Class,ObjectFlags::Transient));
            proxy->XLevel()=game.Level;proxy->Level()=game.LevelInfo;proxy->Index=int(game.Level->Actors.size());
            proxy->LifeSpan()=.01f;game.Level->Actors.push_back(proxy);
            if(source->HasProperty("AnimProxy"))if(auto* original=source->GetUObject("AnimProxy"))
            {
                auto* animationProxy=UObject::Cast<UActor>(game.packages->GetTransientPackage()->NewObject("NetworkAnimation"+std::to_string(entity.ref.id),original->Class,ObjectFlags::Transient));
                animationProxy->Owner()=proxy;animationProxy->NetworkReplica=true;proxy->SetObject("AnimProxy",animationProxy);
            }
            check(replica.bind(entity.ref,proxy),"replica binding");proxyActors[entity.ref.id]=proxy;
            if(auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(proxy));model && !model->Meshes.empty()) {
                auto* flags=static_cast<int*>(proxy->GetProperty("SkelGroupFlags"));
                for(size_t i=0;i<16;++i)check(flags[i]==model->Meshes[proxy->GetByte("SkelMesh")].GroupFlags[i],"replica visible polygon defaults");
                auto* joints=static_cast<uint8_t*>(proxy->GetProperty("JointFlags"));
                for(size_t i=0;i<model->Joints.size() && i<50;++i)check(joints[i]==uint8_t(model->Joints[i].Flags),"replica authored joint flags");
            }
        }
        check(replica.install(snapshot),"atomic engine installation");
        replica.present(1); // Probe the authoritative endpoint explicitly.
        for(const auto& entity:snapshot.world)
        {
            auto* p=proxyActors.at(entity.ref.id);
            check(std::abs(p->Location().x-double(entity.position[0])/8)<.0001,"installed quantized pose");
            check(p->LifeSpan()==.01f,"replica did not run local lifespan/script tick");
        }
    };
    FixedSimulation clock;
    size_t executed=0, installed=0; uint64_t inputAt=0; bool issued=false,observerExercised=false;
    std::vector<CombatOutcome> expectedCombat,receivedFirst,receivedSecond;
    std::vector<ActorEvent> expectedActors,receivedActorsFirst,receivedActorsSecond;
    double firstDistance=0,secondDistance=0;
    for(uint64_t t=0;t<=8000;t+=5)
    {
        server1.step(t+10000); server2.step(t+10000); client1.step(t+50000); client2.step(t+70000);
        check(!server1.failed() && !server2.failed() && !client1.failed() && !client2.failed(),"authenticated connection");
        if(player1.world.candidate()){install(*player1.world.candidate());check(player1.world.commit(player1.world.candidate()->id),"engine world commit");++installed;}
        if(player2.world.candidate()){check(player2.world.commit(player2.world.candidate()->id),"second world commit");++installed;}
        for(auto result:player1.drainCombat())
        {
            auto applied=replica.applyCombat(result);
            check(applied==RuneNetworkReplica::CombatApply::Applied ||
                applied==RuneNetworkReplica::CombatApply::Superseded,"client combat application");
            receivedFirst.push_back(result);
        }
        for(auto result:player2.drainCombat())receivedSecond.push_back(result);
        for(auto event:player1.drainActors())receivedActorsFirst.push_back(event);
        for(auto event:player2.drainActors())receivedActorsSecond.push_back(event);
        if(t>=inputAt)
        {
            Input in; in.life=1; in.yaw=uint16_t(first->Rotation().Yaw); in.forward=(t>1800 && t<2700)?1000:0;
            check(bool(player1.inputs.capture(in,t+50000)),"first input capture");
            in.forward=0; in.strafe=(t>3000 && t<3900)?1000:0; in.yaw=uint16_t(second->Rotation().Yaw);
            check(bool(player2.inputs.capture(in,t+70000)),"second input capture");inputAt=t+20;
            if(t>=4500 && !issued){check(bool(player1.inputs.act(ActionKind::Jump,t+50000)),"jump queued");issued=true;}
        }
        host1.step();host2.step();player1.step();player2.step();
        clock.advance(t*1000,[&](uint64_t tick,float dt)
        {
            authority.beginTick(tick,t+10000);
            check(authority.input(1,host1.commands.held(t+10000)),"first owned input");
            check(authority.input(2,host2.commands.held(t+10000)),"second owned input");
            for(auto action:host1.commands.drainActions(t+10000)){
                bool started=authority.action(1,action,t+10000); if(started)++executed;
                check(host1.decideAction(action.action.id,started?Admission::Started:Admission::ExecutionRejected),"first action decision");
            }
            for(auto action:host2.commands.drainActions(t+10000)){
                bool started=authority.action(2,action,t+10000); if(started)++executed;
                check(host2.decideAction(action.action.id,started?Admission::Started:Admission::ExecutionRejected),"second action decision");
            }
            authority.commitContacts();
            game.LevelInfo->TimeSeconds()+=dt;game.TotalTime+=dt;game.Level->Tick(dt,false);
            for(auto cancelled:authority.drainCancelledActions())
            {
                check((cancelled.peer==1 || cancelled.peer==2) && (cancelled.peer==1?host1:host2).decideAction(cancelled.id,Admission::Cancelled),
                    "interrupted move decision");
            }
            uint64_t actorsThrough=0;
            for(const auto& event:authority.pendingActors())
            {
                if(host1.lastActorPublished()<event.id && !host1.publishActor(event))break;
                if(host2.lastActorPublished()<event.id && !host2.publishActor(event))break;
                actorsThrough=event.id;expectedActors.push_back(event);
            }
            if(actorsThrough)check(authority.releaseActorsThrough(actorsThrough),"release to both clients");
            if(!observerExercised && t>=6000 && server1.ready() && server2.ready())
            {
                // Exercise the actual engine impact observer without claiming a
                // collision or applying damage in this transport fixture.
                RuneFeedback::Impact(first,second,true,false);
                observerExercised=true;
            }
            uint64_t publishedThrough=0;
            for(const auto& result:authority.pendingCombat())
            {
                if(host1.lastCombatPublished()<result.id && !host1.publishCombat(result))break;
                if(host2.lastCombatPublished()<result.id && !host2.publishCombat(result))break;
                publishedThrough=result.id;
                expectedCombat.push_back(result);
            }
            if(publishedThrough)check(authority.releaseCombatThrough(publishedThrough),"release delivered combat");
            firstDistance=std::max(firstDistance,double(length((first->Location()-aStart).xy())));
            secondDistance=std::max(secondDistance,double(length((second->Location()-bStart).xy())));
            if(tick%2==0){World world;check(authority.capture(world),"world capture");check(host1.snapshots.capture(world,tick+1) && host2.snapshots.capture(world,tick+1),"publish worlds");}
        });
        auto transfer=[&](SecureSession& a,SecureSession& b){for(auto& bytes:a.takeDatagrams())b.receiveDatagram(bytes);};
        transfer(server1,client1);transfer(client1,server1);transfer(server2,client2);transfer(client2,server2);
    }
    check(clock.ticks()==480 && clock.droppedTicks()==0,"480 independent 60 Hz ticks");
    check(firstDistance>20 && secondDistance>20,"both network pawns moved");
    check(executed==1 && installed>10,"one jump and ongoing snapshots");
    check(observerExercised && !authority.combatFaulted() && !expectedCombat.empty(),"engine impact observer");
    check(receivedFirst==expectedCombat && receivedSecond==expectedCombat,"both clients received committed impact once");
    check(!expectedActors.empty() && receivedActorsFirst==expectedActors && receivedActorsSecond==expectedActors,
        "both clients received actor lifecycle records");
    check(replica.appliedCombatId()==1,"client consumed exactly one combat result");
    auto targetRef=authority.identity(second),sourceRef=authority.identity(first);
    auto* targetProxy=UObject::Cast<UPawn>(proxyActors.at(targetRef.id));
    auto targetHealth=second->Health();
    CombatOutcome future{2,replica.installedTick()+5,0,sourceRef,targetRef,1,1,
        CombatMove::Bash,CombatContact::Hit,77,true};
    check(replica.applyCombat(future)==RuneNetworkReplica::CombatApply::Applied &&
        targetProxy->Health()==77 && second->Health()==targetHealth,"result applies without server damage replay");
    check(targetProxy->RuneBashImpactStrength==1,"confirmed bash carries head reaction");
    auto reactionStamp=targetProxy->RuneBashImpactAt;
    auto older=*player1.world.installed(); older.id=replica.installedId()+1;older.tick=future.tick-1;
    check(replica.install(older) && targetProxy->Health()==77,"older snapshot cannot undo committed health");
    auto current=older;current.id++;current.tick=future.tick;
    for(auto& e:current.world)if(e.ref==targetRef)e.health=66;
    check(replica.install(current) && targetProxy->Health()==66,"authoritative snapshot replaces combat overlay");
    check(replica.applyCombat(future)==RuneNetworkReplica::CombatApply::Superseded &&
        targetProxy->Health()==66,"duplicate result does not replay damage");
    check(targetProxy->RuneBashImpactAt==reactionStamp,"duplicate outcome does not restart head recoil");
    auto futureLife=future;futureLife.id=3;futureLife.tick=current.tick+2;futureLife.targetLife=2;
    check(replica.applyCombat(futureLife)==RuneNetworkReplica::CombatApply::Deferred &&
        targetProxy->Health()==66,"future life cannot affect current pawn");
    current.id++;current.tick=futureLife.tick;
    for(auto& e:current.world)if(e.ref==targetRef){e.life=2;e.health=80;}
    check(replica.install(current) &&
        replica.applyCombat(futureLife)==RuneNetworkReplica::CombatApply::Superseded &&
        targetProxy->Health()==80,"new life snapshot supersedes delayed result");
    current.id++;current.tick++;
    for(auto& e:current.world)if(e.ref==targetRef)
    {e.animationPhase=8192;e.torsoPhase=3277;e.animationRate=256;e.torsoRate=512;e.flags&=~24;}
    check(replica.install(current),"first replicated animation pose");
    current.id++;current.tick++;
    for(auto& e:current.world)if(e.ref==targetRef)
    {e.animationPhase=24575;e.torsoPhase=9830;e.animationRate=512;e.torsoRate=1024;}
    check(replica.install(current),"second replicated animation pose");
    replica.present(.5);
    auto* targetAnimation=UObject::TryCast<UActor>(targetProxy->GetUObject("AnimProxy"));
    check(targetAnimation && std::abs(targetProxy->AnimFrame()-.5f)<.001f &&
        std::abs(targetAnimation->AnimFrame()-.2f)<.001f &&
        std::abs(targetProxy->AnimRate()-1.5f)<.001f &&
        std::abs(targetAnimation->AnimRate()-3.0f)<.001f,
        "body and upper-body phases interpolate independently");
    auto* goblin=UObject::Cast<UActor>(game.packages->GetTransientPackage()->NewObject(
        "NetworkGoblinReplica",game.packages->GetPackage("RuneI")->GetClass("GoblinWarrior"),ObjectFlags::Transient));
    goblin->XLevel()=game.Level;goblin->Level()=game.LevelInfo;
    goblin->Index=int(game.Level->Actors.size());game.Level->Actors.push_back(goblin);
    auto* goblinModel=UObject::Cast<USkelModel>(GetRuneDisplayMesh(goblin));
    size_t goblinSequence=0;
    for(size_t i=0;i<goblinModel->Sequences.size();++i)
        if(goblinModel->Sequences[i].Info.NumFrames>6){goblinSequence=i+1;break;}
    check(goblinSequence && replica.bind({1000,1},goblin),"generic monster replica binding");
    Entity mob;mob.ref={1000,1};mob.kind=EntityKind::Pawn;mob.health=100;mob.flags=1;
    mob.bodyAnimation=uint16_t(goblinSequence);mob.animationPhase=8192;mob.animationRate=256;
    current.world.push_back(mob);current.id++;current.tick++;
    check(replica.install(current),"monster animation baseline");
    current.world.back().animationPhase=24575;current.id++;current.tick++;
    check(replica.install(current),"monster animation update");
    replica.present(.5);
    check(goblin->NetworkReplica && std::abs(goblin->AnimFrame()-.5f)<.001f &&
        std::abs(goblin->AnimRate()-1.0f)<.001f,"monster uses interpolated multiplayer animation");
    size_t nextSequence=0;
    for(size_t i=0;i<goblinModel->Sequences.size();++i)
        if(i+1!=goblinSequence && goblinModel->Sequences[i].Info.NumFrames>2){nextSequence=i+1;break;}
    check(nextSequence!=0,"monster transition sequence");
    int frames=goblinModel->Sequences[nextSequence-1].Info.NumFrames;
    current.world.back().bodyAnimation=uint16_t(nextSequence);
    current.world.back().animationPhase=int16_t(std::lround(-.5*32767.0/frames));
    current.id++;current.tick++;
    check(replica.install(current),"monster animation transition snapshot");
    replica.present(.5);
    check(goblin->RuneTweenModel==goblinModel && goblin->RuneTweenPose.size()!=0 &&
        goblin->RuneTweenTarget==goblinModel->Sequences[nextSequence-1].Info.Name &&
        std::abs(goblin->AnimFrame()+.75f/frames)<.002f,
        "monster retains authored tween between multiplayer snapshots");
    (void)goblinModel->ActorJointMatrices(goblin);
    check(!authority.input(99,Input{}),"unowned input rejected");
    auto before=first->Location(); Input wrong;wrong.life=2;wrong.forward=1000;check(!authority.input(1,wrong) && first->Location()==before,"old or future life cannot control pawn");
    auto bad=current;++bad.id;++bad.tick;bad.world[0].bodyAnimation=65535;
    auto oldPosition=proxyActors.begin()->second->Location();
    check(!replica.install(bad) && proxyActors.begin()->second->Location()==oldPosition,"invalid animation rejected before any actor write");
    auto delayed=futureLife;delayed.id=4;delayed.tick=current.tick+2;delayed.targetLife=3;
    check(replica.queueCombat(delayed,1000) &&
        replica.flushCombat(1000)==RuneNetworkReplica::CombatQueue::Waiting &&
        replica.flushCombat(11000)==RuneNetworkReplica::CombatQueue::Resync &&
        replica.flushCombat(21000)==RuneNetworkReplica::CombatQueue::Fault,
        "missing combat dependency stays bounded and requests resynchronization");
    int removedIndex=targetProxy->Index;
    check(replica.unbind(targetRef) && targetProxy->bDeleteMe() && game.Level->Actors[removedIndex]==nullptr,
        "despawn removes replica without script destruction");
    replica.present(1); // A delayed old snapshot can still name the retired actor.
    check(replica.referencesRetired(current),"late world referencing a retired actor requests resync");
    auto refreshed=current;++refreshed.id;++refreshed.tick;
    std::erase_if(refreshed.world,[&](const Entity& e){return e.ref==targetRef;});
    for(auto& e:refreshed.world) {
        if(e.owner==targetRef){e.owner={};e.attachmentJoint=255;}
        if(e.weapon==targetRef)e.weapon={};if(e.shield==targetRef)e.shield={};
    }
    check(!replica.referencesRetired(refreshed) && replica.install(refreshed),"fresh world recovers after cross-lane despawn");
    // Buffered remote presentation must not delay the owned pawn or bridge lives.
    auto historyBefore=refreshed,historyAfter=refreshed;
    auto sourceEntity=std::find_if(historyBefore.world.begin(),historyBefore.world.end(),[&](const Entity& e){return e.ref==sourceRef;});
    check(sourceEntity!=historyBefore.world.end(),"buffered fixture pawn");sourceEntity->position[0]-=80;
    auto* viewActor=replica.actor(sourceRef);auto endpoint=viewActor->Location();auto healthBefore=viewActor->GetInt("Health");
    replica.presentAdaptive(historyAfter,historyAfter,1,{999,1},1);
    replica.presentAdaptive(historyBefore,historyAfter,.5,{999,1},1);
    check(std::abs(viewActor->Location().x-(endpoint.x-5))<.001f && viewActor->GetInt("Health")==healthBefore,"remote history changes visuals, not gameplay health");
    replica.presentAdaptive(historyBefore,historyAfter,.5,sourceRef,1);
    check(length(viewActor->Location()-endpoint)<.001f,"owned pawn bypasses adaptive delay");
    sourceEntity->position[0]-=5000;replica.presentAdaptive(historyBefore,historyAfter,.5,{999,1},1);
    check(length(viewActor->Location()-endpoint)<.001f,"teleport is not a buffered sweep");
    sourceEntity->position[0]+=5000;++sourceEntity->life;
    replica.presentAdaptive(historyBefore,historyAfter,.5,{999,1},1);
    check(length(viewActor->Location()-endpoint)<.001f,"new life cannot inherit remote history");
    replica.present(1);
    // Replay real map collision without moving a gameplay actor or dispatching
    // Touch/HitWall/pickup/damage scripts. Check open travel, blocking geometry
    // and conservative fallback outside supported grounded locomotion.
    auto* predicted=UObject::Cast<UPlayerPawn>(proxyActors.at(sourceRef.id));
    MovementAck ack{sourceRef,1,1,1,1,uint16_t(first->GroundSpeed()*8),uint16_t(first->AccelRate()),2048,uint16_t(first->MaxStepHeight()*8)};
    auto original=predicted->Location();auto health=predicted->Health();auto actorCount=game.Level->Actors.size();
    // Visual stair compensation must move the body and attached equipment
    // together without changing the collision location or authoritative actors.
    auto meshOrigin=[&](UActor* a){return (GetRuneMeshToWorld(a)*vec4(0,0,0,1)).xyz();};
    auto beforeMesh=meshOrigin(predicted);std::vector<std::pair<UActor*,vec3>> carried;
    for(const auto& [id,a]:proxyActors)if(a && a->bCarriedItem() && a->GetUObject("AttachParent")==predicted)
        carried.push_back({a,meshOrigin(a)});
    check(!carried.empty(),"stair fixture has attached equipment");
    predicted->NetworkStairOffset=-12;
    check(length(meshOrigin(predicted)-beforeMesh-vec3(0,0,-12))<.001f && predicted->Location()==original,
        "stair mesh offset leaves collision unchanged");
    for(auto [item,at]:carried)check(length(meshOrigin(item)-at-vec3(0,0,-12))<.001f,"stair equipment stays attached");
    predicted->NetworkStairOffset=0;
    auto authorityMesh=meshOrigin(first);first->NetworkStairOffset=-12;
    check(length(meshOrigin(first)-authorityMesh)<.001f,"authority ignores visual stair offset");first->NetworkStairOffset=0;
    size_t movedSteps=0,blockedSteps=0;
    for(int direction=0;direction<16;++direction) {
        MovementPose pose;for(int axis=0;axis<3;++axis)pose.position[axis]=first->Location()[axis];
        Input intent;intent.life=1;intent.forward=1000;intent.yaw=uint16_t(direction*4096);
        for(int step=0;step<180;++step) {
            auto before=pose;if(!StepRunePredictedMovement(game,predicted,pose,intent,ack,1.0f/60))break;
            vec3 start(before.position[0],before.position[1],before.position[2]),end(pose.position[0],pose.position[1],pose.position[2]);
            auto distance=length(end-start);if(distance>.01f)++movedSteps;else ++blockedSteps;
            check(distance<first->GroundSpeed()/60+first->MaxStepHeight()+1,"prediction displacement bound");
            check(predicted->Location()==original && predicted->Health()==health && game.Level->Actors.size()==actorCount,"collision replay has no gameplay side effects");
        }
    }
    std::cout<<"PREDICTION_GEOMETRY moved="<<movedSteps<<" blocked="<<blockedSteps<<" start="<<aStart.x<<","<<aStart.y<<","<<aStart.z<<" current="<<first->Location().x<<","<<first->Location().y<<","<<first->Location().z<<std::endl;
    check(movedSteps>100 && blockedSteps>10,"prediction traverses lane and stops at geometry");
    // Check presentation follows collision-valid steps without re-hitting the riser.
    unsigned stairSteps=0,clippedSteps=0;
    for(auto* start:game.Level->Actors)if(start && start->IsA(commandline->HasArg("","--rune-net-stair-scan")?"NavigationPoint":"PlayerStart"))for(int direction=0;direction<16;++direction) {
        MovementPose pose;for(int axis=0;axis<3;++axis)pose.position[axis]=start->Location()[axis];
        unsigned routeSteps=0;float routeRise=0;
        Input intent;intent.life=1;intent.forward=1000;intent.yaw=uint16_t(direction*4096);
        for(int n=0;n<180;++n) {
            auto before=pose;if(!StepRunePredictedMovement(game,predicted,pose,intent,ack,1.f/60))break;
            if(pose.position[2]-before.position[2]<1)continue;
            ++routeSteps;routeRise+=pose.position[2]-before.position[2];
            ++stairSteps;predicted->Location()={before.position[0],before.position[1],before.position[2]};
            PresentRunePredictedMovement(predicted,pose);
            vec3 desired(pose.position[0],pose.position[1],pose.position[2]);
            if(length(predicted->Location()-desired)>.5f)++clippedSteps;
        }
        if(routeSteps>=4)std::cout<<"STAIR_ROUTE start="<<start->Location().x<<","<<start->Location().y<<","<<start->Location().z<<" yaw="<<direction*4096<<" steps="<<routeSteps<<" rise="<<routeRise<<std::endl;
    }
    if(commandline->HasArg("","--rune-net-corner-probe")) {
        // This single-process fixture contains both server pawns and client
        // replicas. Those duplicate bodies do not exist on a real client.
        // Remove them only for the terrain scan, then restore the fixture.
        struct PawnState{UActor* actor;bool hidden,inserted;};std::vector<PawnState> pawnStates;
        for(auto* a:game.Level->Actors)if(a && a!=predicted && a->IsA("Pawn")){
            pawnStates.push_back({a,a->bHidden(),a->Collision.Inserted});game.Level->Collision.RemoveFromCollision(a);a->bHidden()=true;
        }
        unsigned routes=0,clipped=0,stalls=0,correctionCases=0,correctionStalls=0;float correctionGap=0,worst=0;vec3 worstStart;int worstYaw=0;
        for(auto* start:game.Level->Actors)if(start && start->IsA("NavigationPoint")) {
            auto placement=predicted->CheckLocation(start->Location(),predicted->CollisionRadius(),predicted->CollisionHeight(),true);
            if(!placement.first)continue;
            MovementPose standing;for(int axis=0;axis<3;++axis)standing.position[axis]=placement.second[axis];
            Input idle;idle.life=1;
            if(!StepRunePredictedMovement(game,predicted,standing,idle,ack,1.f/60))continue;
            vec3 validStart(standing.position[0],standing.position[1],standing.position[2]);
            auto clear=predicted->CheckLocation(validStart,predicted->CollisionRadius(),predicted->CollisionHeight(),true);
            if(!clear.first || length(clear.second-validStart)>.01f)continue;
            if(++routes>80)break;
            for(int direction=0;direction<16;++direction) {
                auto pose=standing;
                predicted->Location()=validStart;std::vector<vec3> history;
                Input intent;intent.life=1;intent.forward=1000;intent.yaw=uint16_t(direction*4096);
                for(int n=0;n<150;++n) {
                    // Keep pressing into a wall, then steer along/away from it.
                    if(n==70)intent.yaw+=8192;if(n==110)intent.yaw+=8192;
                    auto before=predicted->Location();
                    if(!StepRunePredictedMovement(game,predicted,pose,intent,ack,1.f/60))break;
                    PresentRunePredictedMovement(predicted,pose);
                    auto desired=vec3(pose.position[0],pose.position[1],pose.position[2]);
                    float gap=length(predicted->Location()-desired);
                    if(gap>3)++clipped;if(gap>20 && length(predicted->Location()-before)<.05f)++stalls;
                    if(gap>worst){worst=gap;worstStart=validStart;worstYaw=direction*4096;}
                    history.push_back(desired);
                    if(n%10==0)for(int lag:{12,24,36})if(history.size()>size_t(lag)) {
                        auto old=history[history.size()-1-lag];
                        if(std::abs(old.z-desired.z)>.5f || length(old-desired)>=150 || length(old-desired)<10)continue;
                        ++correctionCases;auto saved=predicted->Location();predicted->Location()=old;
                        for(int retry=0;retry<16;++retry)PresentRunePredictedMovement(predicted,pose);
                        float remaining=length(predicted->Location()-desired);correctionGap=std::max(correctionGap,remaining);
                        if(remaining>10){++correctionStalls;
                            if(correctionStalls<=3)std::cout<<"CORNER_REMAIN old="<<old.x<<","<<old.y<<","<<old.z<<" desired="<<desired.x<<","<<desired.y<<","<<desired.z<<" result="<<predicted->Location().x<<","<<predicted->Location().y<<","<<predicted->Location().z<<std::endl;
                        }
                        predicted->Location()=saved;
                    }
                }
            }
        }
        for(auto state:pawnStates){state.actor->bHidden()=state.hidden;if(state.inserted)game.Level->Collision.AddToCollision(state.actor);}
        if(commandline->HasArg("","--rune-fix-check"))check(routes>10 && stalls==0 && clipped==0 && correctionCases>1000 && correctionStalls==0 && correctionGap<3,"corner motion must not pin the display position");
        std::cout<<"CORNER_PRESENTATION routes="<<routes<<" gaps="<<clipped<<" stalls="<<stalls<<" max_gap="<<worst<<" correction_cases="<<correctionCases<<" correction_stalls="<<correctionStalls<<" correction_gap="<<correctionGap<<" start="<<worstStart.x<<","<<worstStart.y<<","<<worstStart.z<<" yaw="<<worstYaw<<std::endl;
    }
    predicted->Location()=original;
    std::cout<<"NET_STAIR_PRESENTATION steps="<<stairSteps<<" clipped="<<clippedSteps<<std::endl;
    check(stairSteps>100 && clippedSteps==0,"predicted stair presentation clears risers");
    MovementPose invalid;Input crouch;crouch.held=4;
    check(!StepRunePredictedMovement(game,predicted,invalid,crouch,ack,1.0f/60),"unsupported crouch does not speculate");
    // Compare the read-only air adapter to actual native TickFalling in a
    // clear part of the owned map. No gameplay actor is moved by prediction.
    const auto savedLocation=first->Location(),savedVelocity=first->Velocity(),savedAcceleration=first->Acceleration();
    auto savedPhysics=first->Physics();vec3 air{},floorAnchor{};bool airLane=false;
    for(auto* start:game.Level->Actors)if(start && start->IsA("PlayerStart")) {
        vec3 floor,normal;
        if(!first->Trace(floor,normal,start->Location()-vec3(0,0,140),start->Location(),false,vec3(0)) || normal.z<.7071f)continue;
        auto candidate=floor+vec3(0,0,first->CollisionHeight()+128);
        if(!first->CheckLocation(candidate,88,64,true).first)continue;
        air=candidate;floorAnchor=floor;airLane=true;break;
    }
    check(airLane,"clear falling oracle lane");
    unsigned airChecks=0;double airPositionError=0,airVelocityError=0;
    for(float dt:{1.f/60,1.f/120})for(float vz:{-4000.f,-700.f,-50.f,0.f,300.f,900.f})for(int heading=0;heading<16;++heading) {
        check(first->SetLocation(air),"place native falling oracle");first->Physics()=PHYS_Falling;
        first->Velocity()=vec3(90,35,vz);
        Input intent;intent.life=1;intent.forward=1000;intent.yaw=uint16_t(heading*4096);
        float angle=float(intent.yaw)*6.28318530718f/65536;
        first->Acceleration()=vec3(std::cos(angle)*2400,std::sin(angle)*2400,0);
        auto fall=*authority.movementAck(1);check(fall.mode==MovementFalling,"authority grants only ordinary falling prediction");
        MovementPose pose;pose.mode=MovementFalling;
        for(int i=0;i<3;++i){pose.position[i]=air[i];pose.velocity[i]=first->Velocity()[i];}
        game.Level->Collision.RemoveFromCollision(first);
        bool advanced=StepRunePredictedMovement(game,predicted,pose,intent,fall,dt);
        game.Level->Collision.AddToCollision(first);
        check(advanced,"clear falling replay");
        check(predicted->Location()==original && predicted->Health()==health && game.Level->Actors.size()==actorCount,"air replay has no VM or actor side effects");
        first->TickFalling(dt);
        vec3 pos(pose.position[0],pose.position[1],pose.position[2]),vel(pose.velocity[0],pose.velocity[1],pose.velocity[2]);
        airPositionError=std::max(airPositionError,double(length(pos-first->Location())));
        airVelocityError=std::max(airVelocityError,double(length(vel-first->Velocity())));++airChecks;
    }
    check(airPositionError<.03 && airVelocityError<.1,"fall prediction matches native gravity, air control and terminal cap");
    check(first->SetLocation(air),"restore landing start");first->Physics()=PHYS_Falling;
    auto landingAck=*authority.movementAck(1);MovementPose landing;landing.mode=MovementFalling;
    for(int i=0;i<3;++i)landing.position[i]=air[i];landing.velocity[2]=-200;
    Input idle;idle.life=1;game.Level->Collision.RemoveFromCollision(first);
    bool landed=false;
    for(int i=0;i<180;++i){if(!StepRunePredictedMovement(game,predicted,landing,idle,landingAck,1.f/60))break;if(landing.mode==3){landed=true;break;}}
    game.Level->Collision.AddToCollision(first);
    check(landed && landing.position[2]>=floorAnchor.z+predicted->CollisionHeight()-.5f,"predicted fall stops at floor without Landed callback");
    auto stopped=landing;check(!StepRunePredictedMovement(game,predicted,landing,idle,landingAck,1.f/60) && landing.position==stopped.position,"landing awaits authoritative movement mode");
    first->Physics()=savedPhysics;check(first->SetLocation(savedLocation),"restore falling oracle position");
    first->Velocity()=savedVelocity;first->Acceleration()=savedAcceleration;
    std::cout<<"NET_AIR_PREDICTION checks="<<airChecks<<" position_error="<<airPositionError<<" velocity_error="<<airVelocityError<<" landed="<<landed<<std::endl;
    std::cout<<"RUNE_NETWORK_PREDICTION_COLLISION_OK moved="<<movedSteps<<" blocked="<<blockedSteps<<'\n';
    // Exact real-engine commitment tests, independent of network delivery.
    // Positions are frozen when a valid contact is proposed; only defense gets
    // a bounded grace period. No client-supplied hit or location is accepted.
    bool nearValue=false;
    auto origin=first->Location();
    for(int i=0;i<16 && !nearValue;++i) {
        float angle=float(i)*3.14159265f/8;vec3 candidate=origin+vec3(std::cos(angle),std::sin(angle),0)*75.0f;
        if(first->FastTrace(candidate,origin) && second->SetLocation(candidate))nearValue=true;
    }
    check(nearValue,"melee window lane");
    if(!second->GetUObject("Shield")) {
        auto* shield=second->Spawn(game.packages->GetPackage("RuneI")->GetClass("VikingShield"),{},{},second->Location()+vec3(0,0,100),Rotator(0,0,0));
        check(shield!=nullptr,"defender shield spawn");
        auto* copy=CallEvent(shield,"SpawnCopy",{ExpressionValue::ObjectValue(second)}).ToObject();
        check(copy!=nullptr,"defender shield copy");CallEvent(second,"AcquireInventory",{ExpressionValue::ObjectValue(copy)});
    }
    auto* shield=UObject::Cast<UActor>(second->GetUObject("Shield"));
    uint64_t testTick=500,contactAt=20000,actionId=100,inputSequence=10000;
    auto resetDefense=[&] {
        first->Health()=second->Health()=100;second->RuneKickImmunity=second->RuneKickRecoilTime=0;
        first->Velocity()=second->Velocity()=vec3(0);
        first->GotoState("PlayerWalking","Begin");second->GotoState("PlayerWalking","Begin");
        first->Physics()=second->Physics()=PHYS_Walking;
        first->bShowMenu()=second->bShowMenu()=false;second->RuneBashTapTime=-1;
        shield->SetInt("Health",100);shield->GotoState("Idle","Begin");
        if(auto* proxy=second->GetUObject("AnimProxy"))proxy->GotoState("Idle","Begin");
        auto rotation=Rotator::FromVector(first->Location()-second->Location());second->SetRotation(rotation);second->ViewRotation()=rotation;
        authority.beginTick(testTick,contactAt);
        Input release;release.life=authority.playerLife(2);release.sequence=++inputSequence;release.createdMs=contactAt-100;
        AdmittedAction lower{{++actionId,release.sequence,contactAt-100,authority.playerLife(2),ActionKind::LowerShield},release,contactAt-100};
        check(authority.action(2,lower,contactAt),"defense baseline release");
    };
    auto raise=[&](uint64_t pressed,uint64_t received,bool facing) {
        Input context;context.life=authority.playerLife(2);context.sequence=++inputSequence;context.createdMs=pressed;context.held=2;
        context.yaw=uint16_t(second->Rotation().Yaw+(facing?0:32768));
        check(authority.input(2,context),"block input context");
        AdmittedAction action{{++actionId,context.sequence,pressed,authority.playerLife(2),ActionKind::RaiseShield},context,pressed};
        check(authority.action(2,action,received),"late block admission");
        check(shield->GetStateName()=="Active","late defense active");
    };
    for(int scenario=0;scenario<5;++scenario) {
        resetDefense();bool called=false,defended=false;
        check(authority.deferContact(first,second,false,nullptr,[&](bool blocked){called=true;defended=blocked;RuneKick::CommitImpact(first,second,blocked);}),"pending kick contact");
        authority.beginTick(testTick+1,contactAt+15);
        raise(contactAt+(scenario==1?1:0),contactAt+(scenario==2?34:15),scenario!=3);
        if(scenario==4)shield->SetInt("Health",0);
        authority.commitContacts();check(!called && second->Health()==100,"damage waits for both tick and wall watermark");
        authority.beginTick(testTick+2,contactAt+34);authority.commitContacts();
        check(called && defended==(scenario==0),"only on-time, in-window, facing, intact block counts");
        check((second->Health()==100)==defended,"committed kick health matches defense");
        auto health=second->Health();authority.commitContacts();check(second->Health()==health,"committed contact is single use");
        if(!authority.pendingCombat().empty())check(authority.releaseCombatThrough(authority.pendingCombat().back().id),"probe releases impacts");
        testTick+=10;contactAt+=200;
    }
    resetDefense();bool cancelled=false;
    check(authority.deferContact(first,second,false,nullptr,[&](bool){cancelled=true;}),"pending death control");
    second->Health()=0;authority.beginTick(testTick+2,contactAt+34);authority.commitContacts();
    check(!cancelled && authority.pendingContacts()==0,"dead target cancels pending contact");
    testTick+=10;contactAt+=200;
    // Exercise the VM interception too: ordinary weapon damage and its hit
    // effect share the contact queue, with copied values after args go away.
    auto* weapon=UObject::Cast<UActor>(first->GetUObject("Weapon"));check(weapon!=nullptr,"window weapon");
    for(bool block:{false,true}) {
        resetDefense();weapon->GotoState("Swinging","Begin");
        CallEvent(weapon,"DoWeaponSwipe",{ExpressionValue::ObjectValue(second),ExpressionValue::IntValue(0),ExpressionValue::IntValue(0),
            ExpressionValue::VectorValue(second->Location()),ExpressionValue::VectorValue(vec3(1,0,0)),ExpressionValue::VectorValue(vec3(0))});
        check(authority.pendingContacts()==1 && second->Health()==100,"weapon damage deferred at VM boundary");
        authority.beginTick(testTick+1,contactAt+15);if(block)raise(contactAt,contactAt+15,true);
        authority.commitContacts();check(second->Health()==100,"weapon contact awaits window");
        authority.beginTick(testTick+2,contactAt+34);authority.commitContacts();
        check(authority.pendingContacts()==0 && (second->Health()==100)==block,"late weapon block resolves on shield, otherwise body");
        if(block)check(shield->GetInt("Health")<100,"blocked weapon still damages shield");
        testTick+=10;contactAt+=200;
    }
    for(bool releasedBefore:{false,true}) {
        resetDefense();bool defended=false;
        check(authority.deferContact(first,second,false,nullptr,[&](bool block){defended=block;}),"pending release-order control");
        // A complete held-state release supersedes an earlier raise even when
        // its separate action packet has not arrived. Preserve the interval.
        Input released;released.life=authority.playerLife(2);released.sequence=(inputSequence+=2);released.createdMs=releasedBefore?contactAt-1:contactAt+1;
        released.yaw=uint16_t(second->Rotation().Yaw);
        authority.beginTick(testTick+1,contactAt+15);check(authority.input(2,released),"complete release context");
        Input context=released;--context.sequence;context.held=2;context.createdMs=contactAt-5;
        AdmittedAction oldRaise{{++actionId,context.sequence,contactAt-5,authority.playerLife(2),ActionKind::RaiseShield},context,contactAt-5};
        check(authority.action(2,oldRaise,contactAt+15),"ordered late raise");
        authority.beginTick(testTick+2,contactAt+34);authority.commitContacts();
        check(defended!=releasedBefore,"release timestamp preserves exact defense interval");
        testTick+=10;contactAt+=200;
    }
    testTick+=10;contactAt+=200;resetDefense();bool applied=false;
    check(authority.deferContact(first,second,true,nullptr,[&](bool){applied=true;}),"pending shield-transfer control");
    second->SetObject("Shield",nullptr);authority.beginTick(testTick+2,contactAt+34);authority.commitContacts();
    check(!applied && !authority.pendingContacts(),"shield transfer invalidates pending shield contact");second->SetObject("Shield",shield);
    testTick+=10;contactAt+=200;resetDefense();applied=false;
    check(authority.deferContact(first,second,true,nullptr,[&](bool){applied=true;}),"pending stored shield control");
    shield->bHidden()=true;authority.beginTick(testTick+2,contactAt+34);authority.commitContacts();
    check(!applied && !authority.pendingContacts(),"stored shield cannot retain active block protection");shield->bHidden()=false;
    std::cout<<"RUNE_NETWORK_MELEE_WINDOW_OK cases=12 late_block=1 weapon_window=1 no_rollback=1\n";
    authority.unbind(1);check(!first->NetworkDriven && first->aForward()==0,"disconnect clears control");
    std::cout<<"RUNE_NETWORK_ENGINE_OK ticks="<<clock.ticks()<<" worlds="<<installed<<" actions="<<executed<<" combat="<<expectedCombat.size()<<" distance_first="<<firstDistance<<" distance_second="<<secondDistance<<'\n';
}
