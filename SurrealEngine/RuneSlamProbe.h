#pragma once
#include "RuneSlam.h"
template<class Tick,class Check>
void CheckRuneSlam(Engine& game,UPlayerPawn* p,UPlayerPawn* target,vec3 origin,vec3 forward,Tick tick,Check check,RuneNetworkAuthority& authority) {
    auto give=[&](UPawn* owner,const char* name){
        auto* a=owner->Spawn(game.packages->FindClass(std::string("RuneI.")+name),{},{},origin+vec3(0,0,80),{});check(a,"spawn item");a->SetFloat("RespawnTime",0);
        auto* copy=CallEvent(a,"SpawnCopy",{ExpressionValue::ObjectValue(owner)}).ToObject();check(copy,"inventory copy");CallEvent(owner,"AcquireInventory",{ExpressionValue::ObjectValue(copy)});return a;
    };
    auto* loose=p->Spawn(game.packages->FindClass("RuneI.HandAxe"),{},{},origin+forward*200.f+vec3(0,0,90),{});check(loose,"spawn loose weapon");
    check(loose->RuneSpawnedDuringPlay,"runtime spawn distinguished from map placement");loose->SetFloat("RespawnTime",.01f);
    tick(120);check(loose->Location().z<origin.z+15 && !loose->Owner(),"unsupported spawn settles without pickup");
    loose->SetLocation(origin+forward*200.f+vec3(0,0,110));loose->GotoState("Sleeping","Begin");tick(120);
    check(!loose->bHidden() && loose->Location().z<origin.z+15,"respawn restores gravity");
    loose->RuneSpawnedDuringPlay=false;loose->SetPhysics(PHYS_None);loose->SetLocation(origin+forward*200.f+vec3(0,0,90));auto placed=loose->Location();tick(10);
    check(loose->Location()==placed,"authored placement retained");loose->Destroy();
    std::cout<<"RUNE_PICKUP_PLACEMENT_OK spawned=1 respawned=1 authored_unchanged=1\n";
    check(!RuneSlam::Heavy(p->Weapon()),"one-handed starting axe rejected");
    for(auto name:{"DwarfWorkSword","DwarfBattleSword","VikingAxe","DwarfBattleAxe","TrialPitMace","DwarfWorkHammer","DwarfBattleHammer"}) {
        auto* item=UObject::Cast<UWeapon>(game.packages->GetTransientPackage()->NewObject(std::string("SlamEligibility_")+name,game.packages->FindClass(std::string("RuneI.")+name),ObjectFlags::Transient));check(RuneSlam::Heavy(item),"heavy family eligibility");
    }
    for(auto name:{"HandAxe","GoblinAxe","RustyMace","VikingShortSword","RomanSword"}) {
        auto* item=UObject::Cast<UWeapon>(game.packages->GetTransientPackage()->NewObject(std::string("SlamEligibility_")+name,game.packages->FindClass(std::string("RuneI.")+name),ObjectFlags::Transient));check(!RuneSlam::Heavy(item),"light weapon ineligible");
    }
    give(p,"DwarfWorkHammer");check(RuneSlam::Heavy(p->Weapon()) && RuneSlam::Supported(p),"heavy hammer eligible");
    auto* weapon=p->Weapon();
    auto reset=[&](){RuneSlam::Cancel(p);p->Slam={};p->Health()=target->Health()=1000;
        p->GotoState("PlayerWalking","Begin");target->GotoState("PlayerWalking","Begin");
        p->SetLocation(origin+vec3(0,0,45));target->SetLocation(origin+forward*85.f);
        p->SetRotation(Rotator::FromVector(forward));target->SetRotation(Rotator::FromVector(-forward));
        p->SetPhysics(PHYS_Falling);p->Velocity()=p->Acceleration()=vec3(0);target->Velocity()=target->Acceleration()=vec3(0);
    };
    reset();check(RuneSlam::Arm(p),"arm airborne combo");CallEvent(p,"Fire",{ExpressionValue::FloatValue(0)});check(p->Slam.phase==1,"real Fire starts slam");
    check(p->Velocity().z>=RuneSlam::LiftSpeed,"slam gains real lift");
    auto lift=p->Velocity().z;check(!RuneSlam::Start(p)&&p->Velocity().z==lift,"held attack cannot stack lift");
    for(int i=0;i<48;++i)RuneSlam::Tick(p,1.f/120);
    check(target->Health()==1000,"no early windup damage");
    openrune::net::World world;check(authority.capture(world),"capture flip world");
    openrune::net::Snapshot snapshot{1,0,1,world},decoded;openrune::net::Bytes bytes;
    check(openrune::net::encodeSnapshot(snapshot,nullptr,bytes) && openrune::net::decodeSnapshot(bytes,nullptr,decoded),"flip snapshot roundtrip");
    RuneNetworkReplica replica;uint64_t release=0;
    for(const auto& e:authority.pendingActors()) {
        if(e.operation==openrune::net::ActorOperation::Spawn)check(replica.spawn(game,e),"replica spawn");
        else if(e.operation==openrune::net::ActorOperation::Appearance)check(replica.appearance(game,e),"replica appearance");
        else check(replica.unbind(e.ref),"replica remove");release=e.id;
    }
    if(release)check(authority.releaseActorsThrough(release),"release actor declarations");
    check(replica.install(decoded),"replica installs flip");replica.present(1);
    bool remoteFlip=false;for(const auto& e:world)if(e.kind==openrune::net::EntityKind::Pawn && e.combat.phase==6){auto* remote=UObject::TryCast<UPawn>(replica.actor(e.ref));remoteFlip=remote && remote->Slam.phase==1 && std::abs(remote->Slam.age-.4f)<.002f;}
    check(remoteFlip,"replica retains fractional flip phase");
    for(int i=0;i<8;++i)RuneSlam::Tick(p,1.f/120);
    check(target->Health()==1000 && p->Slam.phase==1 && p->Velocity().z<=-RuneSlam::DiveSpeed,"dives after flip without midair damage");
    p->SetPhysics(PHYS_Walking);RuneSlam::Tick(p,1.f/120);tick(4);
    check(target->Health()<1000 && p->Slam.hits==1,"one heavy hit on landing after windup");
    int damaged=target->Health();tick(60);check(target->Health()==damaged,"no repeated landing hit");
    p->SetPhysics(PHYS_Falling);check(!RuneSlam::Arm(p),"cooldown rejects repeated combo");
    reset();target->GotoState("Scripting","Begin");target->RuneKickRecoilTime=0;
    check(RuneSlam::Arm(p)&&RuneSlam::Start(p),"scripted target scenario starts");
    for(int i=0;i<56;++i)RuneSlam::Tick(p,1.f/120);
    p->SetPhysics(PHYS_Walking);RuneSlam::Tick(p,1.f/120);
    check(target->Health()==1000 && target->RuneKickRecoilTime==0,"scripted target cannot be damaged or shoved");
    reset();target->ViewTarget()=p;target->RuneKickRecoilTime=0;
    check(RuneSlam::Arm(p)&&RuneSlam::Start(p),"cinematic target scenario starts");
    for(int i=0;i<56;++i)RuneSlam::Tick(p,1.f/120);
    p->SetPhysics(PHYS_Walking);RuneSlam::Tick(p,1.f/120);
    check(target->Health()==1000 && target->RuneKickRecoilTime==0,"cinematic target cannot be damaged or shoved");target->ViewTarget()=nullptr;
    std::cout<<"RUNE_SLAM_PROTECTED_TARGETS_OK scripted=1 cinematic=1\n";
    give(target,"VikingShield");reset();CallEvent(target,"ActivateShield",{ExpressionValue::BoolValue(true)});
    check(RuneKick::ShieldBlocks(target,p),"raised facing shield");check(RuneSlam::Arm(p)&&RuneSlam::Start(p),"shield scenario starts");
    for(int i=0;i<56;++i)RuneSlam::Tick(p,1.f/120);
    p->SetPhysics(PHYS_Walking);RuneSlam::Tick(p,1.f/120);tick(4);
    check(target->Health()==1000,"facing shield stops heavy damage");
    reset();check(RuneSlam::Arm(p)&&RuneSlam::Start(p),"ledge scenario starts");
    for(int i=0;i<240;++i)RuneSlam::Tick(p,1.f/120);
    check(!RuneSlam::Busy(p) && target->Health()==1000 && p->Physics()==PHYS_Falling,"long fall cancels without a remote hit or stopping gravity");
    reset();check(RuneSlam::Arm(p)&&RuneSlam::Start(p),"latent launch scenario starts");
    p->SetPhysics(PHYS_None);p->Velocity()=vec3(0);RuneSlam::Tick(p,.01f);
    check(p->Physics()==PHYS_Falling && p->Velocity().z==RuneSlam::LiftSpeed && p->Slam.launchRestored,"original latent entry cannot erase launch");
    p->SetPhysics(PHYS_None);p->Velocity()=vec3(0);RuneSlam::Tick(p,.01f);
    check(p->Velocity().z==0,"latent recovery cannot repeatedly add lift");
    reset();check(RuneSlam::Arm(p)&&RuneSlam::Start(p),"point-blank collision scenario starts");
    bool worldCollision=p->bCollideWorld();p->bCollideWorld()=false;
    std::array<bool,3> pc{bool(p->bCollideActors()),bool(p->bBlockActors()),bool(p->bBlockPlayers())};
    std::array<bool,3> tc{bool(target->bCollideActors()),bool(target->bBlockActors()),bool(target->bBlockPlayers())};
    target->SetCollision(false,false,false);
    check(p->SetLocation(target->Location()+vec3(0,0,p->CollisionHeight()+target->CollisionHeight()-8)),"place shallow pawn overlap");
    target->SetCollision(true,true,true);p->SetCollision(true,true,true);auto overlap=p->Location();
    auto leaving=p->TryMove(vec3(0,0,20));
    check(leaving.Fraction>.999f && p->Location().z-overlap.z>19.99f,"ascending slam escapes a separating pawn contact");
    auto entering=p->TryMove(vec3(0,0,-30));
    check(entering.Fraction<1,"descending slam still collides with pawn");p->SetCollision(pc[0],pc[1],pc[2]);target->SetCollision(tc[0],tc[1],tc[2]);p->bCollideWorld()=worldCollision;
    CallEvent(target,"ActivateShield",{ExpressionValue::BoolValue(false)});
    for(int i=0;i<56;++i)RuneSlam::Tick(p,1.f/120);
    check(p->Slam.hits==0,"head landing waits for real contact");
    p->PhysLanded(target,vec3(0,0,1));tick(4);
    check(p->Slam.phase==2 && p->Slam.hits==1 && target->Health()<1000,"head contact hits before original pawn bounce");
    p->PhysLanded(target,vec3(0,0,1));tick(4);check(p->Slam.hits==1,"head bounce cannot repeat slam damage");
    std::cout<<"RUNE_SLAM_FLIGHT_OK lift="<<RuneSlam::LiftSpeed<<" dive="<<RuneSlam::DiveSpeed<<" landing_contact=1 ledge_cancel=1\n";
    reset();check(RuneSlam::Arm(p)&&RuneSlam::Start(p),"cancellation scenario starts");p->Health()=0;RuneSlam::Tick(p,.01f);check(!RuneSlam::Busy(p),"death cancels move");p->Health()=1000;
    auto* old=p->GetUObject("Skeletal");auto scale=p->DrawScale();unsigned poses=0;
    p->Slam.phase=1;p->Slam.weapon=weapon;
    for(const auto& choice:openrune::Characters) {
        auto* prototype=game.packages->FindClass(choice.className)->GetDefaultObject<UActor>();auto* m=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(prototype));if(!m)continue;
        p->SetObject("Skeletal",m);p->DrawScale()=prototype->DrawScale();
        p->Slam.phase=2;p->Slam.age=0;auto base=m->SampleFrame(0,0,false);RuneSlam::Apply(*m,p,base);auto upright=m->JointMatrices(base);
        p->Slam.phase=1;p->Slam.age=RuneSlam::Windup*.5f;auto middle=m->SampleFrame(0,0,false);RuneSlam::Apply(*m,p,middle);auto flipped=m->JointMatrices(middle);
        float turn=0;for(int c=0;c<3;++c)turn=std::max(turn,length(vec3(flipped[0][c*4],flipped[0][c*4+1],flipped[0][c*4+2])-vec3(upright[0][c*4],upright[0][c*4+1],upright[0][c*4+2])));
        check(turn>.5f,"whole root flips, not just torso");
        for(size_t j=1;j<m->Joints.size();++j){auto parent=m->Joints[j].Parent;
            auto point=[](const mat4& x){return vec3(x[12],x[13],x[14]);};
            check(std::abs(length(point(flipped[j])-point(flipped[parent]))-length(point(upright[j])-point(upright[parent])))<.03f,"flip preserves bone lengths");
            check(middle[j].Scale==base[j].Scale,"flip preserves joint scale");
        }
        for(int sample=0;sample<=54;++sample){p->Slam.age=RuneSlam::Windup*sample/54;auto pose=m->SampleFrame(0,0,false);RuneSlam::Apply(*m,p,pose);auto bones=m->JointMatrices(pose);
            for(auto& matrix:bones)for(int k=0;k<16;++k)check(std::isfinite(matrix[k]),"finite full-body flip transform");++poses;
        }
    }
    p->SetObject("Skeletal",old);p->DrawScale()=scale;RuneSlam::Cancel(p);
    auto* npc=UObject::TryCast<UPawn>(p->Spawn(game.packages->FindClass("RuneI.DarkViking"),{},{},origin+forward*85.f,{}));check(npc,"spawn eligible enemy");
    auto* npcWeapon=give(npc,"DwarfWorkHammer");npc->SetObject("Weapon",npcWeapon);npc->Enemy()=p;npc->SetPhysics(PHYS_Falling);npc->SetRotation(Rotator::FromVector(-forward));
    npc->GotoState("Scripting","Begin");check(!RuneSlam::Arm(npc),"scripted campaign behavior untouched");
    npc->GotoState("Fighting","Begin");npc->Enemy()=p;npc->SetPhysics(PHYS_Falling);npc->Health()=1000;npc->SetRotation(Rotator::FromVector(forward));
    RuneSlam::Tick(npc,.05f);RuneSlam::Tick(npc,.05f);RuneSlam::Tick(npc,.05f);check(!RuneSlam::Busy(npc),"enemy does not slam away from its target");
    npc->SetRotation(Rotator::FromVector(-forward));RuneSlam::Tick(npc,.05f);
    std::cout<<"RUNE_SLAM_ENEMY supported="<<RuneSlam::Supported(npc)<<" physics="<<int(npc->Physics())<<" state="<<npc->GetStateName().ToString()<<" air="<<npc->Slam.air<<" distance="<<length(npc->Location()-p->Location())<<std::endl;
    check(RuneSlam::Busy(npc),"eligible enemy chooses telegraphed airborne slam");RuneSlam::Cancel(npc);npc->Destroy();
    std::cout<<"RUNE_SLAM_OK roster_poses="<<poses<<" windup=0.45 snapshots=1 shield=1 cooldown=1 cancellation=1\n";
}
