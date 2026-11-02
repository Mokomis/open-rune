#include "Precomp.h"
#include "RuneNetwork.h"
#include "RuneBot.h"
#include "Utils/CommandLine.h"
#include "RuneBloodPresentation.h"
#include "Packages/Engine/Resources/UPalette.h"
#include "RuneDualWield.h"
#include "RuneBloodlust.h"
#include "RuneBash.h"
#include "RuneParticles.h"
#include "RuneRushPose.h"
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
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "VM/ScriptCall.h"
#include <iostream>
#include <set>
using namespace openrune::net;
void RunRuneBotGoalProbe(Engine& game);
void RunRuneBotTeamProbe(Engine& game);
void RunRuneBotCrowdProbe(Engine& game);
void RunRuneNetworkNativeProbe(Engine& game)
{
    if(commandline->HasArg("","--rune-bot-crowd-probe") || commandline->HasArg("","--rune-bot-group-probe")){RunRuneBotCrowdProbe(game);return;}
    if(commandline->HasArg("","--rune-bot-team-probe")){RunRuneBotTeamProbe(game);return;}
    if(commandline->HasArg("","--rune-bot-goal-probe") || commandline->HasArg("","--rune-bot-navigation-probe")){RunRuneBotGoalProbe(game);return;}
    auto check=[](bool v,const char* why){if(!v)throw std::runtime_error(std::string("network native combat: ")+why);};
    RuneDualWield::Enabled=true;RuneBloodlust::Enabled=true;RuneTactics::Enabled=false;
    game.NetworkAuthority=true;
    auto url=game.GetDefaultURL("DM-Hildir.run");url.AddOrReplaceOption("game=RuneI.RuneMultiPlayer");game.LoadMap(url);
    auto* p=game.LoginNetworkPlayer("NativeOne");auto* target=game.LoginNetworkPlayer("NativeTwo");game.viewport->Actor()=p;
    RuneNetworkAuthority authority(game);check(authority.bind(1,p,1)&&authority.bind(2,target,1),"bind players");
    uint64_t tickId=0,now=1000,actionId=0,snapshotId=0;
    auto tick=[&](int n){for(int i=0;i<n;++i){now+=17;authority.beginTick(++tickId,now);authority.commitContacts();
        game.LevelInfo->TimeSeconds()+=1.f/60;game.TotalTime+=1.f/60;game.Level->Tick(1.f/60,false);}};
    tick(30);p->DisableEvent("PlayerTick");target->DisableEvent("PlayerTick");
    p->SetCollision(true,false,false);target->SetCollision(true,false,false);
    vec3 origin=p->Location(),direction(1,0,0);bool lane=false;
    std::vector<UActor*> starts;for(auto* a:game.Level->Actors)if(a && a->IsA("PlayerStart"))starts.push_back(a);
    for(auto* a:starts)if(!lane){
        if(!p->SetLocation(a->Location()))continue;p->SetPhysics(PHYS_Walking);p->Velocity()=p->Acceleration()=vec3(0);tick(60);
        if(p->Physics()!=PHYS_Walking)continue;
        for(int i=0;i<16&&!lane;++i){float angle=i*3.14159265f/8;direction={std::cos(angle),std::sin(angle),0};auto end=p->Location()+direction*600.f;vec3 hit,normal;
            lane=p->FastTrace(end,p->Location()) && p->CheckLocation(end,22,42,true).first && p->Trace(hit,normal,end-vec3(0,0,140),end,false,vec3(10,10,0)) && normal.z>.7f;
        }if(lane)origin=p->Location();
    }
    check(lane,"clear lane");
    auto reset=[&](float distance){
        for(auto* a:{p,target}) {
            a->Health()=10000;auto* parts=static_cast<int*>(a->GetProperty("BodyPartHealth"));for(int j=0;j<15;++j)parts[j]=10000;
            a->Bloodlust={};a->RuneKickActive=a->RuneKickPending=false;a->RuneBashPhase=0;a->RuneBashPending=false;
            a->RuneKickRecoilTime=0;a->RuneKickImmunity=0;a->Dual.lastWholeBody=-1;a->Dual.guard=false;a->Dual.guardBlend=0;a->Dual.recovery=0;
            a->SetBool("bSwingingHigh",false);a->SetBool("bSwingingLow",false);a->bFire()=a->bAltFire()=0;
            a->SetBool("bBloodLust",false);a->SetInt("Strength",0);a->GotoState("PlayerWalking","Begin");
            a->GetUObject("AnimProxy")->GotoState("Idle","Begin");a->SetPhysics(PHYS_Walking);a->Velocity()=a->Acceleration()=vec3(0);
            CallEvent(a,"WeaponDeactivate");
        }
        check(p->SetLocation(origin)&&target->SetLocation(origin+direction*distance),"reset placement");
        p->SetRotation(Rotator::FromVector(direction));target->SetRotation(Rotator::FromVector(-direction));
        p->ViewRotation()=p->Rotation();target->ViewRotation()=target->Rotation();
    };
    auto give=[&](UPlayerPawn* a,const char* type,bool acquire){
        auto* item=a->Spawn(game.packages->GetPackage("RuneI")->GetClass(type),{},{},a->Location()+vec3(0,0,100),{});check(item,"spawn item");
        item->SetFloat("RespawnTime",0);auto* copy=CallEvent(item,"SpawnCopy",{ExpressionValue::ObjectValue(a)}).ToObject();check(copy==item,"real inventory item");
        if(acquire)CallEvent(a,"AcquireInventory",{ExpressionValue::ObjectValue(copy)});copy->GotoState("Active","Begin");return copy;
    };
    auto action=[&](uint64_t peer,ActionKind kind,uint16_t held=0,uint64_t pressed=0){
        Input input;input.life=authority.playerLife(peer);input.sequence=++actionId;input.createdMs=pressed?pressed:now;
        input.yaw=uint16_t((peer==1?p:target)->Rotation().Yaw);input.held=held;input.forward=1000;
        AdmittedAction a{{actionId,actionId,input.createdMs,input.life,kind},input,input.createdMs};return authority.action(peer,a,now);
    };
    RuneNetworkReplica replica;std::set<uint32_t> spawned;
    auto snapshot=[&](){
        World world;check(authority.capture(world),"capture native world");
        uint64_t released=0;
        for(const auto& event:authority.pendingActors()) {
            if(event.operation==ActorOperation::Spawn) {check(replica.spawn(game,event)!=nullptr,"spawn presentation actor");spawned.insert(event.ref.id);}
            else if(event.operation==ActorOperation::Appearance)check(replica.appearance(game,event),"install appearance");
            else check(replica.unbind(event.ref),"remove presentation actor");
            released=event.id;
        }
        if(released)check(authority.releaseActorsThrough(released),"release actor events");
        Snapshot s{++snapshotId,0,tickId,world},decoded;Bytes wire;
        check(encodeSnapshot(s,nullptr,wire)&&decodeSnapshot(wire,nullptr,decoded)&&s==decoded,"wire roundtrip native world");
        check(replica.install(decoded),"install native world");replica.present(1);
        return UObject::TryCast<UPlayerPawn>(replica.actor(authority.identity(p)));
    };
    reset(150);give(p,"VikingShortSword",true);auto* off=UObject::TryCast<UWeapon>(give(p,"HandAxe",false));
    check(RuneDualWield::Equip(p,off),"authority equips dual pair");
    int rightDamage=0,leftDamage=0;
    for(int i=0;i<4;++i){
        reset(45);tick(1);int before=target->Health();check(action(1,ActionKind::Attack),"attack intent");
        tick(80);int damage=before-target->Health();if(i%2)leftDamage+=damage;else rightDamage+=damage;
        std::cout<<"NET_NATIVE_STRIKE n="<<i<<" damage="<<damage<<" left="<<p->Dual.leftSwings<<" right="<<p->Dual.rightSwings<<std::endl;
    }
    check(rightDamage>0 && leftDamage>0 && p->Dual.leftSwings>=2 && p->Dual.rightSwings>=2,"both weapons damage through authority");
    reset(150);p->Dual.left=true;p->Dual.guard=true;p->Dual.guardBlend=1;
    auto* remote=snapshot();check(remote && RuneDualWield::Equipped(remote) && remote->Dual.left && remote->Dual.guardBlend==1,"replica pair/pose state");
    PlayerScore tagScore;tagScore.player=2;tagScore.pawn=authority.identity(p);
    check(replica.scoredPawn(tagScore)==remote,"team tag resolves current pawn independently of slot");
    ++tagScore.pawn.generation;check(replica.scoredPawn(tagScore)==nullptr,"team tag rejects retired pawn generation");
    tagScore.pawn={};check(replica.scoredPawn(tagScore)==nullptr,"vacant slot has no team tag");
    // Ordinary brains must not emit close combat against absent, hidden,
    // dead or distant opponents. Stress inputs deliberately bypass the brain.
    unsigned emptyKickChecks=0;
    for(int scenario=0;scenario<4;++scenario) {
        World observations;check(authority.capture(observations),"negative bot observations");
        const auto own=authority.identity(p),enemy=authority.identity(target);
        if(scenario==0)std::erase_if(observations,[&](const Entity& e){return e.kind==EntityKind::Pawn && e.ref!=own;});
        for(auto& e:observations)if(e.ref==enemy) {
            if(scenario==1)e.flags|=32;
            if(scenario==2){e.health=0;e.flags&=~1;}
            if(scenario==3)e.position[0]+=10000;
        }
        MatchStatus status;status.mode=GameMode::Deathmatch;status.phase=MatchPhase::Playing;
        RuneBot::Brain brain(717+scenario,1);
        for(uint64_t time=1000;time<=6000;time+=100) {
            auto decision=brain.think(game,replica,observations,status,own,time);
            check(!decision.action || (*decision.action!=ActionKind::Kick && *decision.action!=ActionKind::Bash && *decision.action!=ActionKind::Attack),"normal bot needs a live nearby opponent for close combat");
            ++emptyKickChecks;
        }
    }
    std::cout<<"NET_BOT_NO_EMPTY_KICKS checks="<<emptyKickChecks<<" scenarios=4"<<std::endl;
    {
        World nearby;check(authority.capture(nearby),"positive bot observations");
        auto own=authority.identity(p),enemy=authority.identity(target);
        auto self=std::find_if(nearby.begin(),nearby.end(),[&](const Entity& e){return e.ref==own;});
        auto foe=std::find_if(nearby.begin(),nearby.end(),[&](const Entity& e){return e.ref==enemy;});
        check(self!=nearby.end() && foe!=nearby.end(),"positive bot roster");
        foe->position=self->position;foe->position[0]+=45*8;foe->health=100;foe->flags=3;
        RuneBot::Brain brain(717,1);MatchStatus status;status.mode=GameMode::Deathmatch;status.phase=MatchPhase::Playing;bool kicked=false;
        for(uint64_t time=1000;time<=6000;++time) {
            auto decision=brain.think(game,replica,nearby,status,own,time);
            if(decision.action==ActionKind::Kick){kicked=true;break;}
        }
        check(kicked,"eligible normal bot still kicks a close visible enemy");
    }
    auto* serverModel=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(p));
    auto serverPose=serverModel->ActorJointMatrices(p);
    auto remotePose=serverModel->ActorJointMatrices(remote);float poseError=0;
    for(size_t j=0;j<serverPose.size();++j)for(int k=0;k<16;++k)poseError=std::max(poseError,std::abs(serverPose[j][k]-remotePose[j][k]));
    std::cout<<"NET_NATIVE_POSE_ERROR "<<poseError<<std::endl;
    check(poseError<.15f,"dual replica pose agrees with authority");
    check(!RuneDualWield::Equip(remote,off) && !RuneBloodlust::Charge(remote),"replicas cannot simulate combat");
    auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(remote));
    auto pose=model->ActorJointMatrices(remote);check(!pose.empty(),"replica dual skeleton");
    // Presentation-only crossed guard never changes authoritative defense.
    reset(150);UObject::Cast<UActor>(p->GetUObject("AnimProxy"))->PlayAnim(p->Weapon()->GetName("A_Idle"),1,0);tick(90);remote=snapshot();
    auto* previewProxy=UObject::Cast<UActor>(remote->GetUObject("AnimProxy"));
    std::cout<<"PREVIEW_IDLE sequence="<<previewProxy->AnimSequence().ToString()<<" loop="<<previewProxy->bAnimLoop()<<" frame="<<previewProxy->AnimFrame()<<" idle="<<p->Weapon()->GetName("A_Idle").ToString()<<std::endl;
    Input previewInput;previewInput.life=1;previewInput.held=2;
    check(replica.anticipate(authority.identity(p),9001,ActionKind::RaiseShield,previewInput,now),"anticipate crossed guard");
    replica.presentAnticipation(now+60);
    check(remote->RuneCombatPreviewGuard>.4f && !remote->Dual.guard && !p->Dual.guard,"crossed visual guard is not gameplay defense");
    replica.clearAnticipation();check(remote->RuneCombatPreviewGuard<0,"crossed preview cleared");
    // Server contact uses the frozen guard; a later release cannot erase it.
    // Submit actual planner commands, without manually setting bAltFire.
    // A reliable guard action may arrive before its held-input datagram.
    auto planned=[&](RuneBot::Brain& brain,ActionKind wanted,int attempts){
        MatchStatus status;status.mode=GameMode::Deathmatch;status.phase=MatchPhase::Playing;
        for(int i=0;i<attempts;++i){
            tick(6);snapshot();World observations;check(authority.capture(observations),"planner observations");
            std::erase_if(observations,[](const Entity& e){return e.kind!=EntityKind::Pawn;});
            auto decision=brain.think(game,replica,observations,status,authority.identity(p),now);
            if(!decision.action)continue;
            auto input=decision.input;input.sequence=++actionId;input.createdMs=now;
            AdmittedAction request{{actionId,actionId,now,input.life,*decision.action},input,now};
            bool accepted=authority.action(1,request,now);
            if(*decision.action==wanted)return accepted;
        }return false;
    };
    bool botGuard=false;
    for(int seed=1;seed<=12&&!botGuard;++seed){reset(100);p->SetInt("RunePower",0);RuneBot::Brain brain(seed,1);botGuard=planned(brain,ActionKind::RaiseShield,10);}
    check(botGuard,"bot chooses crossed block");tick(8);remote=snapshot();
    check(RuneDualWield::Blocks(p,target)&&remote->Dual.guard&&remote->Dual.guardBlend>.5f,"bot held guard survives before input datagram and reaches replica");
    check(action(1,ActionKind::LowerShield),"bot guard release");tick(20);check(!p->bAltFire()&&!p->Dual.guard,"release survives without datagram");
    auto heldInput=[&](uint64_t sequence,uint16_t held){Input i;i.life=authority.playerLife(1);i.sequence=sequence;i.createdMs=now;i.held=held;i.yaw=uint16_t(p->Rotation().Yaw);return authority.input(1,i);};
    const auto pressSequence=actionId+2;
    check(heldInput(pressSequence,2),"newer held input");tick(8);
    check(action(1,ActionKind::LowerShield),"older release delivered late");tick(8);
    check(p->bAltFire() && p->Dual.guard,"older release cannot cancel newer held guard");
    actionId=pressSequence;check(action(1,ActionKind::LowerShield),"current release");tick(8);
    check(heldInput(pressSequence,2),"older held datagram after release");tick(8);
    check(!p->bAltFire()&&!p->Dual.guard,"old datagram cannot undo reliable release");
    const auto releaseSequence=actionId+2;check(heldInput(releaseSequence,0),"newer release input");
    check(action(1,ActionKind::RaiseShield,2),"older press delivered late");tick(8);
    check(!p->bAltFire()&&!p->Dual.guard,"older press cannot undo newer release");
    actionId=releaseSequence;

    std::cout<<"NET_BOT_CROSS_GUARD_OK actual_planner held_action replica_pose\n";
    reset(45);p->bAltFire()=1;check(action(1,ActionKind::RaiseShield,2),"cross guard action");tick(8);
    check(RuneDualWield::Blocks(p,target),"cross guard raised");
    int before=p->Health();
    check(authority.deferContact(target,p,false,nullptr,[&](bool blocked){
        check(!blocked,"cross guard must not become full shield block");
        CallEvent(p,"JointDamaged",{ExpressionValue::IntValue(20),ExpressionValue::ObjectValue(target),ExpressionValue::VectorValue(p->Location()),ExpressionValue::VectorValue(vec3(0)),ExpressionValue::NameValue("blunt"),ExpressionValue::IntValue(0)});
    },false,true),"queue crossed contact");
    p->bAltFire()=0;RuneDualWield::Guard(p,false);tick(2);
    check(before-p->Health()>0 && before-p->Health()<20,"crossed partial damage survives release");
    std::cout<<"NET_NATIVE_DUAL_OK right_damage="<<rightDamage<<" left_damage="<<leftDamage<<" crossed_damage="<<before-p->Health()<<std::endl;
    // Remove the real offhand from inventory for shield tests; no phantom weapon.
    RuneDualWield::Unequip(p,true);off->SetLocation(origin+direction*350.f);
    // Original multiplayer loadout may also contain a stored starting axe.
    // Clear fixture spares so automatic dual equip cannot replace the dropped one.
    for(auto* item=p->Inventory();item;) {auto* next=item->Inventory();
        if(item!=p->Weapon() && item->IsA("Weapon")){CallEvent(p,"DeleteInventory",{ExpressionValue::ObjectValue(item)});item->Destroy();}
        item=next;
    }
    reset(450);give(p,"VikingShield",true);tick(1);
    check(!RuneDualWield::Equipped(p),"offhand dropped");remote=snapshot();check(!remote->Dual.offhand,"replica clears offhand");
    reset(450);UObject::Cast<UActor>(p->GetUObject("AnimProxy"))->PlayAnim(p->Weapon()->GetName("A_Idle"),1,0);tick(90);remote=snapshot();
    previewProxy=UObject::Cast<UActor>(remote->GetUObject("AnimProxy"));
    auto originalSequence=previewProxy->AnimSequence();auto originalFrame=previewProxy->AnimFrame();
    const auto unchangedHealth=target->Health();auto previewStart=model->ActorJointMatrices(remote);
    previewInput.held=1;
    check(replica.anticipate(authority.identity(p),9002,ActionKind::Attack,previewInput,now),"anticipate attack windup");
    replica.presentAnticipation(now+50);auto previewMid=model->ActorJointMatrices(remote);float previewMotion=0;
    for(size_t j=0;j<previewMid.size();++j)for(int k=0;k<16;++k)previewMotion=std::max(previewMotion,std::abs(previewMid[j][k]-previewStart[j][k]));
    check(previewMotion>.01f && previewProxy->AnimSequence()==originalSequence && previewProxy->AnimFrame()==originalFrame,"preview moves skeleton without changing replicated animation clock");
    check(target->Health()==unchangedHealth && !remote->bFire() && !p->bFire(),"prediction has no attack/damage side effects");
    replica.resolveAnticipation({9002,Admission::ExecutionRejected},now+60);replica.presentAnticipation(now+100);
    check(previewProxy->RuneCombatPreviewWeight>0 && previewProxy->RuneCombatPreviewWeight<1,"rejected preview blends back");
    replica.presentAnticipation(now+141);check(previewProxy->RuneCombatPreviewWeight==0,"rejected preview fully clears");
    previewInput.held=2;
    check(replica.anticipate(authority.identity(p),9003,ActionKind::RaiseShield,previewInput,now+150),"anticipate shield raise");
    replica.presentAnticipation(now+190);check(previewProxy->RuneCombatPreviewWeight==1,"shield raise visible before host response");
    check(!p->bAltFire() && !remote->bAltFire(),"visual shield cannot grant block");
    check(replica.anticipate(authority.identity(p),9004,ActionKind::LowerShield,previewInput,now+200),"rapid release replaces raise");
    replica.resolveAnticipation({9003,Admission::ExecutionRejected},now+210);
    replica.presentAnticipation(now+220);check(previewProxy->RuneCombatPreviewWeight==1,"old decision cannot cancel newer release");
    replica.presentAnticipation(now+551);replica.presentAnticipation(now+632);check(previewProxy->RuneCombatPreviewWeight==0,"outage preview expires");
    check(replica.anticipate(authority.identity(p),9005,ActionKind::RaiseShield,previewInput,now+650),"preview before confirmation");
    p->bAltFire()=1;check(action(1,ActionKind::RaiseShield,2),"host confirms shield");tick(3);snapshot();
    replica.presentAnticipation(now+650);replica.presentAnticipation(now+690);
    check(previewProxy->RuneCombatPreviewWeight>0 && previewProxy->RuneCombatPreviewWeight<1,"confirmed shield hands off gradually");
    replica.presentAnticipation(now+731);check(previewProxy->RuneCombatPreviewWeight==0,"confirmed shield handoff ends");
    p->bAltFire()=0;check(action(1,ActionKind::LowerShield),"host lowers shield");tick(90);snapshot();
    check(replica.anticipate(authority.identity(p),9006,ActionKind::RaiseShield,previewInput,now+750),"preview before death");
    remote->Health()=0;replica.presentAnticipation(now+760);
    check(previewProxy->RuneCombatPreviewWeight==0,"immediate combat outcome clears preview before snapshot");
    p->Health()=0;snapshot();replica.presentAnticipation(now+760);check(previewProxy->RuneCombatPreviewWeight==0,"death clears preview");
    std::cout<<"NET_NATIVE_ANTICIPATION_OK motion="<<previewMotion<<" authoritative_damage=0"<<std::endl;
    reset(45);p->RuneBashCooldown=0;target->RuneBashImmunity=0;authority.drainSounds();
    check(action(1,ActionKind::Bash),"shield bash starts");bool bashSeen=false,reactionSeen=false;float bashPoseError=0,reactionMotion=0;
    auto* reactionModel=UObject::Cast<USkelModel>(GetRuneDisplayMesh(target));
    auto restPose=reactionModel->ActorJointMatrices(target);
    auto targetBefore=target->Location();auto healthBefore=target->Health();
    for(int n=0;n<40;++n){tick(1);snapshot();
        auto* rp=replica.actor(authority.identity(p));auto* rt=replica.actor(authority.identity(target));
        if(p->RuneBashPhase==1 && p->AnimSequence()=="H3_DefendAttack") {
            check(rp->AnimSequence()==p->AnimSequence(),"bash animation reaches attacker replica");bashSeen=true;
        }
        if(target->RuneBashPhase==2){
            check(rt->AnimSequence()==target->AnimSequence(),"stagger animation reaches target replica");reactionSeen=true;
            if(target->AnimFrame()>=0) {
                auto localPose=reactionModel->ActorJointMatrices(target),remotePose=reactionModel->ActorJointMatrices(rt);
                for(size_t j=0;j<localPose.size();++j)for(int k=0;k<16;++k) {
                    float error=std::abs(localPose[j][k]-remotePose[j][k]);
                    // Look-at head/jaw offsets are local presentation; compare
                    // the authored torso/limb reaction independently of them.
                    auto joint=reactionModel->Joints[j].Name;
                    if(joint!="Head" && joint!="Jaw")bashPoseError=std::max(bashPoseError,error);
                    reactionMotion=std::max(reactionMotion,std::abs(remotePose[j][k]-restPose[j][k]));
                }
            }
        }
    }
    auto bashSounds=authority.drainSounds();
    check(bashSeen && reactionSeen && target->Health()<healthBefore && length(target->Location()-targetBefore)>1,"bash impact and recoil");
    check(bashSounds.size()>=2,"bash swing and impact sound cues");
    std::cout<<"NET_NATIVE_BASH_PRESENTATION_OK sounds="<<bashSounds.size()<<" recoil="<<length(target->Location()-targetBefore)<<" body_pose_error="<<bashPoseError<<" joint_motion="<<reactionMotion<<std::endl;
    check(bashPoseError<.15f && reactionMotion>1,"visible replica stagger pose matches authority");
    reset(150);tick(240);
    auto full=[&](){p->SetBool("bBloodLust",true);p->SetInt("Strength",p->GetInt("MaxStrength"));};
    reset(450);check(!action(1,ActionKind::ShieldCharge,2),"charge without bloodlust rejected");full();
    check(!action(1,ActionKind::ShieldCharge,0),"charge without block intent rejected");
    check(!action(1,ActionKind::BloodLeap),"leap without first tap rejected");
    check(action(1,ActionKind::Jump),"first normal jump");tick(5);
    check(action(1,ActionKind::BloodLeap,0,now-34),"authenticated double tap leap");
    check(p->Physics()==PHYS_Falling && p->Bloodlust.cooldown>3.9f && p->GetInt("Strength")<=p->GetInt("MaxStrength")/2,"leap spends meter and cooldown");
    check(!action(1,ActionKind::BloodLeap),"repeat leap denied");
    remote=snapshot();check(remote->Bloodlust.phase==RuneBloodlust::Leaping && RuneBloodlust::Active(remote) && remote->Bloodlust.ready==0,"replica leap halo cooldown");
    check(!authority.movementAck(1)->mode,"no grounded prediction during leap");
    tick(180);check(!RuneBloodlust::Busy(p),"leap recovers");
    reset(130);full();check(action(1,ActionKind::ShieldCharge,2),"charge action");
    remote=snapshot();check(remote->Bloodlust.rushing && remote->Bloodlust.phase==RuneBloodlust::ChargeWindup,"replica rush preparation");
    tick(25);check(length(p->Location()-origin)<5,"rush preserves windup");
    auto* rushModel=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(remote));remote->Bloodlust.age=.5f;check(!rushModel->ActorJointMatrices(remote).empty(),"rush pose on replica");
    before=target->Health();tick(90);check(p->Bloodlust.hits==1 && target->Health()<before,"charge exactly one impact");
    check(!RuneBloodlust::Busy(p),"charge recovery");
    int openDamage=before-target->Health();
    reset(130);give(target,"VikingShield",true);target->bAltFire()=1;CallEvent(target,"ActivateShield",{ExpressionValue::BoolValue(true)});full();
    before=target->Health();check(action(1,ActionKind::ShieldCharge,2),"blocked charge action");tick(110);
    check(p->Bloodlust.hits==1 && target->Health()<before && before-target->Health()<openDamage,"blocked charge reduced impact");
    std::cout<<"NET_NATIVE_BLOOD_OK open_damage="<<openDamage<<" blocked_damage="<<before-target->Health()<<" leap=1 charge=1 replica=1\n";
    reset(62);p->SetInt("RunePower",0);give(target,"VikingShield",true);target->bAltFire()=1;
    check(action(2,ActionKind::RaiseShield,2),"bot bash defender raises shield");tick(8);
    RuneBot::Brain bashBrain(73,1);auto beforeBash=target->Location();
    check(planned(bashBrain,ActionKind::Bash,30),"bot chooses and admits shield bash");
    check(p->RuneBashPhase==1,"bot bash has visible windup");tick(60);snapshot();
    check(length(target->Location()-beforeBash)>1,"bot shield bash makes physical contact");
    check(action(2,ActionKind::LowerShield),"defender releases shield");target->bAltFire()=0;
    std::cout<<"NET_BOT_SHIELD_BASH_OK actual_planner contact_recoil="<<length(target->Location()-beforeBash)<<std::endl;
    // Exercise the real bot planner, then admit its commands through the
    // production authority. Only this fixture's placement/meter are supplied;
    // the planner must produce a valid double tap and held-block charge itself.
    for(bool outside:{false,true})for(bool leap : {false,true}) {
        reset(300);RuneBloodlust::AllowNormal=outside;
        if(outside){p->SetInt("Strength",0);p->SetBool("bBloodLust",false);check(RuneBloodlust::Readiness(p)&1,"normal combat readiness without rage");}
        else full();
        auto* savedShield=p->GetUObject("Shield");
        if(leap)p->SetObject("Shield",nullptr);
        RuneBot::Brain brain(73,1);
        MatchStatus status;status.mode=GameMode::Deathmatch;status.phase=MatchPhase::Playing;
        bool acceptedSpecial=false,sawJump=false;
        for(int i=0;i<20 && !acceptedSpecial;++i) {
            tick(6);snapshot();World observations;check(authority.capture(observations),"bot decision observations");
            // Exclude optional supplies from this focused two-player duel.
            std::erase_if(observations,[](const Entity& e){return e.kind!=EntityKind::Pawn;});
            auto decision=brain.think(game,replica,observations,status,authority.identity(p),now);
            if(!decision.action)continue;
            auto input=decision.input;input.sequence=++actionId;input.createdMs=now;
            AdmittedAction request{{actionId,actionId,now,input.life,*decision.action},input,now};
            bool admitted=authority.action(1,request,now);
            if(*decision.action==ActionKind::Jump){check(admitted,"bot first jump admitted");sawJump=true;}
            if(*decision.action==(leap?ActionKind::BloodLeap:ActionKind::ShieldCharge)) {
                check(admitted,"bot special met real gesture/equipment contract");
                check(leap?sawJump:bool(input.held&2),"bot special includes required input");acceptedSpecial=true;
            }
        }
        if(leap)p->SetObject("Shield",savedShield);
        check(acceptedSpecial,leap?"bot never performed rage leap":"bot never performed shield charge");
        if(outside)check(p->GetInt("Strength")==0 && p->Bloodlust.cooldown>3.8f,"normal combat move keeps zero meter and real cooldown");
        tick(180);
    }
    RuneBloodlust::AllowNormal=false;reset(300);p->SetInt("Strength",0);p->SetBool("bBloodLust",false);
    check(RuneBloodlust::Readiness(p)==0,"disabling normal mode restores rage requirement");
    std::cout<<"NET_BOT_SPECIALS_OK full_rage_and_normal_combat double_tap held_block authority_admitted cooldowns\n";
    // The charge uses the shared contact filter before damage OR recoil runs.
    reset(130);
    auto* originalGame=game.GameInfo;
    auto* one=p->GetUObject("PlayerReplicationInfo");auto* two=target->GetUObject("PlayerReplicationInfo");
    auto oldOne=one->GetByte("Team"),oldTwo=two->GetByte("Team");one->SetByte("Team",0);two->SetByte("Team",0);
    for(const auto& spec:std::vector<std::pair<const char*,const char*>>{{"RuneI","TeamGame"},{"Arena","ArenaGameInfo"},{"HeadBall","HeadBallGame"}}) {
        auto* cls=game.packages->GetPackage(spec.first)->GetClass(spec.second);check(cls!=nullptr,"mode rules fixture class");
        game.GameInfo=UObject::Cast<UGameInfo>(game.packages->GetTransientPackage()->NewObject("NativeRules",cls,ObjectFlags::Transient));
        if(game.GameInfo->IsA("TeamGame"))game.GameInfo->SetFloat("FriendlyFireScale",0);
        if(game.GameInfo->IsA("HeadBallGame"))game.GameInfo->SetBool("bGameStarted",false);
        bool impact=false;check(authority.deferContact(p,target,false,nullptr,[&](bool){impact=true;},false,true),"native protected contact consumed");
        check(!authority.pendingContacts() && !impact,"protected charge causes neither damage nor recoil");
        game.GameInfo=originalGame;
    }
    one->SetByte("Team",oldOne);two->SetByte("Team",oldTwo);
    std::cout<<"NET_NATIVE_MODE_PROTECTION_OK team arena headball\n";
    reset(450);full();check(action(1,ActionKind::ShieldCharge,2),"death cancellation setup");p->Health()=0;tick(2);
    check(!RuneBloodlust::Busy(p),"death cancels native phase");remote=snapshot();check(!remote->Bloodlust.phase && !RuneBloodlust::Active(remote) && !remote->Bloodlust.ready,"dead replica clears cues");
    reset(450);p->SetInt("MaxPower",100);p->SetInt("RunePower",0);tick(1);
    check(!action(1,ActionKind::Powerup),"Rune Power without energy rejected");
    auto pickupPower=[&](const char* type,int amount){
        auto* rune=p->Spawn(game.packages->FindClass(std::string("RuneI.")+type),{},{},p->Location()+vec3(0,0,100),{});
        check(rune,"spawn actual rune pickup");rune->SetFloat("RespawnTime",0);rune->SetFloat("RunePower",float(amount));
        bool autoPickup=game.GameInfo->GetBool("bAutoPickup");game.GameInfo->SetBool("bAutoPickup",true);
        CallEvent(rune,"Touch",{ExpressionValue::ObjectValue(p)});game.GameInfo->SetBool("bAutoPickup",autoPickup);
    };
    pickupPower("RuneOfPowerRefill",50);check(p->GetInt("RunePower")==50,"real refill supplies energy");
    auto* poweredWeapon=p->GetUObject("Weapon");check(poweredWeapon && poweredWeapon->GetBool("bCanBePoweredUp"),"powerable weapon equipped");
    int powerCost=poweredWeapon->GetInt("RunePowerRequired");check(powerCost>0 && powerCost<=50,"weapon Rune Power cost");
    check(action(1,ActionKind::Powerup),"authoritative Rune Power activation");
    check(p->GetInt("RunePower")==50-powerCost && poweredWeapon->GetBool("bPoweredUp"),"Rune Power cost and weapon state");
    remote=snapshot();check(remote->GetInt("RunePower")==50-powerCost && remote->GetInt("MaxPower")==100,"replicated Rune Power meter");
    auto* remoteWeapon=remote->GetUObject("Weapon");check(remoteWeapon && remoteWeapon->GetBool("bPoweredUp"),"replicated powered weapon");
    std::cout<<"NET_NATIVE_RUNE_POWER_OK cost="<<powerCost<<" meter="<<remote->GetInt("RunePower")<<std::endl;
    CallEvent(poweredWeapon,"PowerupEnd");tick(180);
    p->SetInt("RunePower",0);pickupPower("RuneOfPowerRefill",100);remote=snapshot();
    check(p->GetInt("RunePower")==100 && remote->GetInt("RunePower")==100,"actual pickup replicates filled meter");
    reset(450);RuneBot::Brain powerBrain(73,1);
    check(planned(powerBrain,ActionKind::Powerup,20),"bot chooses and admits weapon power");
    check(poweredWeapon->GetBool("bPoweredUp") && p->GetInt("RunePower")==100-powerCost,"bot activation applies effect and cost");
    snapshot();std::cout<<"NET_BOT_RUNE_POWER_OK real_pickup planner activation meter_replica\n";
    p->SetInt("MaxPower",200);p->SetInt("RunePower",0);pickupPower("RuneOfPower",20);remote=snapshot();
    check(p->GetInt("RunePower")==200 && p->GetInt("MaxPower")==200 && remote->GetInt("RunePower")==200,"capacity rune still refills energy at maximum capacity");
    std::cout<<"NET_RUNE_MAX_CAPACITY_REFILL_OK capacity=200 energy=200\n";
    reset(450);give(p,"DwarfWorkHammer",true);tick(150);give(p,"HandAxe",true);tick(150);
    for(auto selection:{ActionKind::SelectFists,ActionKind::SelectSword,ActionKind::SelectHammer,ActionKind::SelectAxe}) {
        reset(450);check(action(1,selection),"weapon selection admitted");tick(150);
        remote=snapshot();auto* equipped=p->GetUObject("Weapon");
        int expected=int(selection)-int(ActionKind::SelectFists)-1;
        std::cout<<"NET_NATIVE_SELECTION kind="<<int(selection)<<" weapon="<<(equipped?equipped->Name.ToString():"none")<<std::endl;
        if(expected<0)check(!equipped,"select fists");
        else check(equipped && equipped->GetByte("MeleeType")==expected,"select inventory weapon family");
        check(remote->GetUObject("Weapon")==replica.actor(authority.identity(UObject::TryCast<UActor>(equipped))),"replicated selected weapon");
    }
    std::cout<<"NET_NATIVE_INVENTORY_OK selections=4"<<std::endl;
    auto* flags=static_cast<int32_t*>(p->GetProperty("SkelGroupFlags"));auto* joints=static_cast<uint8_t*>(p->GetProperty("JointFlags"));
    flags[3]^=2;joints[7]^=1;remote=snapshot();
    check(static_cast<int32_t*>(remote->GetProperty("SkelGroupFlags"))[3]==flags[3] && static_cast<uint8_t*>(remote->GetProperty("JointFlags"))[7]==joints[7],"replicated dismemberment flags");
    auto** skins=static_cast<UObject**>(p->GetProperty("SkelGroupSkins"));auto** remoteSkins=static_cast<UObject**>(remote->GetProperty("SkelGroupSkins"));
    for(int i=0;i<16;++i)check(skins[i]==remoteSkins[i],"replicated skin identity");
    flags[3]^=2;joints[7]^=1;
    std::cout<<"NET_NATIVE_APPEARANCE_OK groups=16 joints=50 skins=16"<<std::endl;
    check(std::any_of(authority.pendingCombat().begin(),authority.pendingCombat().end(),[](const auto& event){return event.move==CombatMove::Weapon && event.contact==CombatContact::Hit;}),"real weapon strikes produce feedback records");
    std::set<UActor*> owned,remoteOwned;
    for(auto* item=p->GetUObject("Inventory");item;item=item->GetUObject("Inventory")) {
        auto* actor=UObject::TryCast<UActor>(item);check(owned.insert(actor).second,"authority inventory acyclic");
        check(replica.actor(authority.identity(actor)),"all inventory actors replicated");
    }
    for(auto* item=remote->GetUObject("Inventory");item;item=item->GetUObject("Inventory"))check(remoteOwned.insert(UObject::TryCast<UActor>(item)).second,"replica inventory acyclic");
    check(owned.size()==remoteOwned.size(),"complete inventory chain replicated");
    std::cout<<"NET_NATIVE_INVENTORY_CHAIN_OK items="<<owned.size()<<std::endl;
    p->Velocity().x=12000;remote=snapshot();check(remote->Velocity().x==float(INT16_MAX)/4 && p->Velocity().x==12000,"bounded velocity hint preserves authority");
    p->Velocity().x=0;std::cout<<"NET_NATIVE_VELOCITY_BOUND_OK"<<std::endl;
    check(static_cast<int32_t*>(remote->GetProperty("SkelGroupFlags"))[3]==flags[3] && static_cast<uint8_t*>(remote->GetProperty("JointFlags"))[7]==joints[7],"appearance restored to original defaults");
    std::cout<<"NET_NATIVE_APPEARANCE_RESTORE_OK"<<std::endl;
    reset(150);p->Health()=50;
    auto* food=p->Spawn(game.packages->GetPackage("RuneI")->GetClass("LegOMeat1"),p,{},p->Location(),{});
    check(food!=nullptr,"food fixture spawn");snapshot();
    CallEvent(food,"InventorySpecial1");snapshot();tick(2);snapshot();
    check(p->Health()>50,"food restores health");
    std::cout<<"NET_NATIVE_FOOD_OK health="<<p->Health()<<std::endl;
    for(const char* effect:{"BloodSpray","BloodMist","BloodSpurt","HitWood","HitMetal"}) {
        auto* source=p->Spawn(game.packages->GetPackage("RuneI")->GetClass(effect),nullptr,{},p->Location()+vec3(0,0,30),{});
        check(source!=nullptr,"native effect spawn");snapshot();auto* copy=replica.actor(authority.identity(source));
        check(copy && copy->DrawType()==source->DrawType(),"effect emitter replicated");
        replica.tickEffects(1.f/120);check(copy->GetBool("IsLoaded"),"client native particles initialized");
        RuneParticles::ArrayView particles(copy);int alive=0;for(int j=0;j<copy->GetByte("ParticleCount");++j)alive+=particles.Valid(j);
        check(alive>0,"authored particles visible");
        auto* tx=static_cast<UTexture**>(copy->GetProperty("ParticleTexture"))[0];
        std::cout<<"NET_NATIVE_EFFECT_OK class="<<effect<<" particles="<<alive<<" style="<<int(particles.Ref<uint8_t>(0,RuneParticles::Field::Style))<<" texture="<<(tx?tx->Name.ToString():"none")<<std::endl;
        if(std::string(effect)=="BloodMist" || std::string(effect)=="BloodSpray")check(RuneBloodPresentation::RedEmitter(copy),"red replica effect eligibility");
        if(std::string(effect)=="BloodMist" || std::string(effect)=="BloodSpray") {
            TextureInfo texture;texture.Texture=tx;texture.Format=TextureFormat::P8;texture.Palette=(TextureColor*)tx->Palette()->Colors.data();
            auto original=texture.Palette[0];bool enabled=RuneVisualEffects::Enabled.load();RuneVisualEffects::Enabled=true;
            int style=particles.Ref<uint8_t>(0,RuneParticles::Field::Style);uint32_t drawFlags=style==4?PF_Modulated:PF_Masked;
            check(RuneBloodPresentation::Recolor(copy,texture,drawFlags,style),"real blood texture conversion");
            check((drawFlags&PF_Highlighted) && !(drawFlags&PF_Modulated),"blood alpha blend path");
            if(style==5)check(texture.Palette[0].A==0,"cloud mask remains a hole");
            auto after=((TextureColor*)tx->Palette()->Colors.data())[0];
            check(after.R==original.R && after.B==original.B,"source palette unchanged");
            RuneVisualEffects::Enabled=false;check(!RuneBloodPresentation::Recolor(copy,texture,drawFlags,style),"Original effects bypass blood correction");RuneVisualEffects::Enabled=enabled;
        }
        for(int n=0;n<600;++n)replica.tickEffects(1.f/120);
        check(copy->bHidden() && !copy->bDeleteMe(),"cosmetic expiry preserves replication identity");
        source->Destroy();snapshot();
    }
    // Real live-player dismemberment must leave an independent world pickup.
    reset(150);
    auto* parts=static_cast<int*>(target->GetProperty("BodyPartHealth"));parts[1]=1;
    std::set<UActor*> oldLimbs;
    for(auto* a:game.Level->Actors)if(a && a->IsA("LimbWeapon"))oldLimbs.insert(a);
    CallEvent(target,"DamageBodyPart",{ExpressionValue::IntValue(40),ExpressionValue::ObjectValue(p),
        ExpressionValue::VectorValue(target->Location()),ExpressionValue::VectorValue(vec3(0)),
        ExpressionValue::NameValue("sever"),ExpressionValue::IntValue(1)});
    tick(1);snapshot();UActor* severed=nullptr;
    for(auto* a:game.Level->Actors)if(a && !a->NetworkReplica && !a->bDeleteMe() && a->IsA("LimbWeapon") && !oldLimbs.contains(a))severed=a;
    check(target->Health()>0 && parts[1]<=0 && severed,"live sever creates limb");
    auto* remoteLimb=replica.actor(authority.identity(severed));check(remoteLimb,"severed limb replica");
    std::cout<<"NET_NATIVE_SEVER_INITIAL_BASE base="<<(severed->ActorBase()?severed->ActorBase()->Name.ToString():"none")<<std::endl;
    check(severed->ActorBase()!=target,"severed limb has no amputee physics base");
    for(int n=0;n<60;++n) {
        tick(1);snapshot();
        check(!remoteLimb->bCarriedItem() && !remoteLimb->GetUObject("AttachParent"),"severed limb not attached on replica");
        check(length(remoteLimb->Location()-severed->Location())<.12f,"severed limb world motion replicated");
    }
    std::cout<<"NET_NATIVE_SEVER_BASE base="<<(severed->ActorBase()?severed->ActorBase()->Name.ToString():"none")<<std::endl;
    auto limbBeforeMove=severed->Location();
    target->TryMove(direction*100.f,false,false);
    check(length(severed->Location()-limbBeforeMove)<.001f,"moving amputee does not carry severed limb");
    check(target->SetLocation(origin+direction*400.f),"move amputee away");tick(1);snapshot();
    auto world=GetRuneActorToWorld(remoteLimb);
    std::cout<<"NET_NATIVE_SEVER_OK class="<<severed->Class->Name.ToString()<<" owner="<<(severed->Owner()?severed->Owner()->Name.ToString():"none")
        <<" carried="<<severed->bCarriedItem()<<" distance="<<length(remoteLimb->Location()-replica.actor(authority.identity(target))->Location())<<std::endl;
    check(length((world*vec4(0,0,0,1)).xyz()-remoteLimb->Location())<.12f,"severed limb render transform independent of amputee");
    // The real Rune death scripts replace the pawn with a carcass decoration.
    // It must remain visible independently of the player's next life.
    reset(150);give(target,"DwarfWorkHammer",true);tick(150);
    auto* dropped=UObject::TryCast<UActor>(target->GetUObject("Weapon"));
    check(dropped && dropped->IsA("DwarfWorkHammer"),"nonstarter death weapon equipped");
    authority.drainSounds();target->Health()=0;
    CallEvent(target,"Died",{ExpressionValue::ObjectValue(p),ExpressionValue::NameValue("blunt"),ExpressionValue::VectorValue(target->Location())});
    tick(1);snapshot();
    auto* dying=replica.actor(authority.identity(target));
    // PlayerPawn.Dying.BeginState transfers the animation to a carcass immediately.
    UActor* deathBody=target;
    if(target->bHidden())for(auto* actor:game.Level->Actors)
        if(actor && !actor->NetworkReplica && !actor->bDeleteMe() && actor->IsA("RuneCarcass") && actor->AnimSequence()==target->AnimSequence())deathBody=actor;
    auto* remoteDeathBody=replica.actor(authority.identity(deathBody));
    check(remoteDeathBody && !remoteDeathBody->bHidden() && remoteDeathBody->AnimSequence()==target->AnimSequence(),"death animation visible on pawn or replacement corpse");
    check(dropped && !dropped->bDeleteMe() && dropped->Owner()!=target,"nonstarter weapon drops on death");
    auto* remoteDrop=replica.actor(authority.identity(dropped));
    check(remoteDrop && !remoteDrop->bHidden() && remoteDrop->Owner()!=dying,"dropped weapon presentation");
    auto deathSounds=authority.drainSounds();check(!deathSounds.empty(),"real death script emits replicated sound");
    float corpseOriginError=0;int corpseOriginChecks=0;bool sawInheritedPivot=false,sawResizedPivot=false;
    for(int frame=0;frame<239;++frame) {
        tick(1);snapshot();
        for(auto* source:game.Level->Actors)if(source && !source->NetworkReplica && !source->bDeleteMe() && !source->bHidden() && source->IsA("RuneCarcass")) {
            auto* remote=replica.actor(authority.identity(source));check(remote!=nullptr,"corpse origin replica");
            // Compare actual render transforms, not raw actor positions: Rune's
            // death scripts change the collision origin and visual pivot together.
            auto expected=(GetRuneActorToWorld(source)*vec4(0,0,0,1)).xyz();
            auto actual=(GetRuneActorToWorld(remote)*vec4(0,0,0,1)).xyz();
            corpseOriginError=std::max(corpseOriginError,length(expected-actual));++corpseOriginChecks;
            check(length(expected-actual)<.12f,"corpse visual origin follows death animation and collision resize");
            sawInheritedPivot|=std::abs(source->PrePivot().z)<.01f;
            sawResizedPivot|=source->PrePivot().z>1.f;
        }
    }
    check(corpseOriginChecks>=200 && sawInheritedPivot && sawResizedPivot,"death origin test crosses both pivot states");
    std::cout<<"NET_NATIVE_CORPSE_ORIGIN_OK checks="<<corpseOriginChecks<<" max_error="<<corpseOriginError<<std::endl;
    size_t corpses=0,debris=0;UActor* carcass=nullptr;
    for(auto* actor:game.Level->Actors)if(actor && !actor->NetworkReplica && !actor->bDeleteMe() && actor->IsA("RuneCarcass")) {
        auto* remoteCorpse=replica.actor(authority.identity(actor));check(remoteCorpse && !remoteCorpse->bHidden(),"corpse retained on replica");
        check(GetRuneDisplayMesh(remoteCorpse)==GetRuneDisplayMesh(actor),"corpse inherited skeleton");
        check(remoteCorpse->AnimSequence()==actor->AnimSequence(),"corpse authored animation");++corpses;carcass=actor;
    }
    check(corpses>0,"real death produced carcass");
    CallEvent(carcass,"ChunkUp",{ExpressionValue::IntValue(100)});snapshot();
    for(auto* chunk:game.Level->Actors)if(chunk && !chunk->NetworkReplica && !chunk->bDeleteMe() && chunk->IsA("DebrisFlesh")) {
        auto* remoteChunk=replica.actor(authority.identity(chunk));check(remoteChunk && !remoteChunk->bHidden(),"authored gib replicated");
        std::cout<<"NET_NATIVE_GIB_SCALE authority="<<chunk->DrawScale()<<" replica="<<remoteChunk->DrawScale()<<std::endl;
        check(std::abs(remoteChunk->DrawScale()-chunk->DrawScale())<.002f,"gib size preserved");++debris;
    }
    check(debris>0,"real carcass ChunkUp produces gibs");
    std::cout<<"NET_NATIVE_DEATH_PRESENTATION_OK corpses="<<corpses<<" debris="<<debris<<std::endl;
    check(!authority.combatFaulted(),"authority remained healthy");
    std::cout<<"RUNE_NETWORK_NATIVE_OK snapshots="<<snapshotId<<" ticks="<<tickId<<std::endl;
}
