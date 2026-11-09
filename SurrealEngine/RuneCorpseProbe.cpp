#include "RuneBattleLaws.h"
#include "Precomp.h"
#include "RuneCorpse.h"
#include "RuneCharacters.h"
#include "RuneKick.h"
#include "RuneBash.h"
#include "RuneNetwork.h"
#include "RuneBot.h"
#include "Engine.h"
#include "Utils/CommandLine.h"
#include "Package/PackageManager.h"
#include "Package/Package.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Inventory/UWeapon.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UGameInfo.h"
#include "Packages/Engine/Actors/Info/UPlayerReplicationInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "VM/ScriptCall.h"
#include <iostream>
using namespace openrune::net;
#include "RuneCorpseMotionProbe.h"
#include "RuneSlamProbe.h"

void RunRuneCorpseProbe(Engine& game) {
    unsigned checks=0;auto check=[&](bool v,const char* why){++checks;if(!v)throw std::runtime_error(std::string("corpse probe: ")+why);};
    auto mode=commandline->GetArg("","--rune-corpse-probe","single");bool multi=mode!="single",teamMode=mode=="team";
    game.NetworkAuthority=true;RuneTactics::Enabled=false;
    auto url=game.GetDefaultURL("DM-Hildir.run");url.AddOrReplaceOption(teamMode?"game=RuneI.TeamGame":"game=RuneI.RuneMultiPlayer");game.LoadMap(url);
    auto* p=game.LoginNetworkPlayer("Thrower");auto* target=game.LoginNetworkPlayer("Target");auto* donor=game.LoginNetworkPlayer("Body");game.viewport->Actor()=p;
    if(teamMode){p->PlayerReplicationInfo()->SetByte("Team",0);target->PlayerReplicationInfo()->SetByte("Team",1);game.GameInfo->SetFloat("FriendlyFireScale",0);}
    RuneNetworkAuthority authority(game);check(authority.bind(1,p,1)&&authority.bind(2,target,1),"bind");
    game.NetworkAuthority=multi;
    uint64_t tickId=0,now=1000,actionId=0,snapshotId=0;
    auto tick=[&](int n){for(int i=0;i<n;++i){now+=17;authority.beginTick(++tickId,now);authority.commitContacts();game.LevelInfo->TimeSeconds()+=1.f/60;game.TotalTime+=1.f/60;game.Level->Tick(1.f/60,false);}};
    tick(30);for(auto* a:{p,target,donor}){a->DisableEvent("PlayerTick");a->SetCollision(true,false,false);a->Health()=1000;}
    vec3 origin=p->Location(),direction(1,0,0);bool lane=false;
    for(auto* a:game.Level->Actors)if(a && a->IsA("PlayerStart") && !lane) {
        if(!p->SetLocation(a->Location()))continue;p->SetPhysics(PHYS_Walking);p->Velocity()=vec3(0);tick(60);
        for(int i=0;i<16&&!lane;++i){float angle=i*3.14159265f/8;direction={std::cos(angle),std::sin(angle),0};auto end=p->Location()+direction*600.f;vec3 hit,normal;
            lane=p->FastTrace(end,p->Location()) && p->CheckLocation(end,22,42,true).first && p->Trace(hit,normal,end-vec3(0,0,140),end,false,vec3(10,10,0)) && normal.z>.7f;
        }if(lane)origin=p->Location();
    }check(lane,"clear lane");
    for(auto* a:{p,target,donor}){a->SetLocation(origin+direction*(a==p?0.f:a==target?260.f:400.f));a->Velocity()=a->Acceleration()=vec3(0);a->SetPhysics(PHYS_Walking);}
    p->SetRotation(Rotator::FromVector(direction));p->ViewRotation()=p->Rotation();target->SetRotation(Rotator::FromVector(-direction));target->ViewRotation()=target->Rotation();
    if(commandline->HasArg("","--rune-slam-probe")){CheckRuneSlam(game,p,target,origin,direction,tick,check,authority);return;}
    donor->Health()=0;CallEvent(donor,"Died",{ExpressionValue::ObjectValue(p),ExpressionValue::NameValue("blunt"),ExpressionValue::VectorValue(donor->Location())});tick(210);
    UActor* body=nullptr;for(auto* a:game.Level->Actors)if(a && a->IsA("RuneCarcass") && !a->bDeleteMe())body=a;
    check(body,"original death produces body");body->TimerRate()=0;
    auto printBody=[&](){std::cout<<"CORPSE_BODY state="<<body->GetStateName().ToString()<<" finished="<<body->bAnimFinished()<<" anim="<<body->AnimSequence().ToString()<<" frame="<<body->AnimFrame()<<" scale="<<body->DrawScale()<<" mass="<<body->Mass()<<" radius="<<body->CollisionRadius()<<" height="<<body->CollisionHeight()<<" base="<<(body->ActorBase()?body->ActorBase()->Name.ToString():"none")<<" groups=";auto* g=static_cast<int*>(body->GetProperty("SkelGroupFlags"));for(int i=0;i<16;++i)std::cout<<g[i]<<",";std::cout<<std::endl;};printBody();
    check(RuneCorpse::Throwable(body),"intact humanoid eligible");
    // A dropped pose must articulate, never shrink local lengths/scales or the
    // world-space bones. Exercise original roster skeletons, including HOV.
    unsigned rosterModels=0,boneChecks=0;
    for(const auto& character:openrune::Characters) {
        auto* cls=game.packages->FindClass(character.className);check(cls,"roster class");
        auto* actor=cls->GetDefaultObject<UActor>();auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(actor));check(model,"roster skeleton");
        auto reference=RuneCorpse::ReferencePose(*model),relaxed=RuneCorpse::RelaxedPose(*model);
        auto a=model->JointMatrices(reference),b=model->JointMatrices(relaxed);check(reference.size()==relaxed.size(),"pose topology");
        for(size_t j=0;j<reference.size();++j) {
            check(reference[j].Position==relaxed[j].Position && reference[j].Scale==relaxed[j].Scale,"reference proportions preserved");
            if(model->Joints[j].Parent>=0) {
                auto parent=model->Joints[j].Parent;auto lengthAt=[&](const Array<mat4>& m){return length(vec3(m[j][12]-m[parent][12],m[j][13]-m[parent][13],m[j][14]-m[parent][14]));};
                check(std::abs(lengthAt(a)-lengthAt(b))<.001f*std::max(1.f,lengthAt(a)),"world bone length preserved");++boneChecks;
            }
        }
        ++rosterModels;
    }
    std::cout<<"CORPSE_PROPORTIONS_OK models="<<rosterModels<<" bones="<<boneChecks<<std::endl;
    auto moveBody=[&](){body->SetBase(nullptr,true);body->SetPhysics(PHYS_None);body->Velocity()=vec3(0);body->SetLocation(p->Location()+direction*65.f-vec3(0,0,24));};moveBody();
    // Remove floor loot from this isolated lane so ordinary Use can select it.
    for(auto* a:game.Level->Actors)if(a && a->IsA("Inventory") && !a->Owner() && length(a->Location()-p->Location())<120)a->bHidden()=true;
    auto action=[&](ActionKind k,uint64_t peer=1){
        if(!multi){if(k==ActionKind::Use)CallEvent(peer==1?p:target,"Use");else if(k==ActionKind::Attack)CallEvent(p,"Fire",{ExpressionValue::FloatValue(0)});else if(k==ActionKind::Throw)CallEvent(p,"Throw");else if(k==ActionKind::Jump)CallEvent(p,"Jump",{ExpressionValue::FloatValue(0)});return true;}
        Input input;input.life=authority.playerLife(peer);input.sequence=++actionId;input.createdMs=now;input.yaw=uint16_t(p->Rotation().Yaw);
        return authority.action(peer,{{actionId,actionId,now,input.life,k},input,now},now);
    };
    RuneNetworkReplica replica;
    auto snapshot=[&](){
        World world;check(authority.capture(world),"capture");uint64_t release=0;
        for(const auto& e:authority.pendingActors()) {if(e.operation==ActorOperation::Spawn)check(replica.spawn(game,e),"spawn replica");else if(e.operation==ActorOperation::Appearance)check(replica.appearance(game,e),"appearance");else check(replica.unbind(e.ref),"unbind");release=e.id;}
        if(release)check(authority.releaseActorsThrough(release),"release");Snapshot s{++snapshotId,0,tickId,world},decoded;Bytes wire;
        check(encodeSnapshot(s,nullptr,wire)&&decodeSnapshot(wire,nullptr,decoded)&&s==decoded,"snapshot wire");check(replica.install(decoded),"snapshot install");replica.present(1);
        return world;
    };
    {auto laws=RuneBattleLaws::Campaign.load();RuneBattleLaws::Campaign=laws&~RuneBattleLaws::Corpse;
     check(RuneCorpse::Throwable(body)&&!RuneCorpse::Pickup(p,body)&&!RuneCorpse::Use(p),"unchecked corpse interaction rejects valid reachable body");RuneBattleLaws::Campaign=laws;}
    check(action(ActionKind::Use),"use action");check(RuneCorpse::Held(p)==body,"Use lifts body");check(!RuneCorpse::Pickup(target,body),"cannot steal carried body");
    check(RuneCorpse::SpeedScale(p)<.7f,"carrying slows movement");
    auto carryOrigin=p->Location();p->Acceleration()=direction*p->AccelRate();tick(30);
    check(RuneCorpse::Busy(p) && length(p->Location()-carryOrigin)>30,"player can walk while carrying");
    check(length(p->Velocity().xy())<=p->GroundSpeed()*.62f+1,"carry walking speed is capped");
    auto ack=authority.movementAck(1);check(ack && std::abs(float(ack->speed)/8-p->GroundSpeed()*.62f)<.2f,"carry prediction receives reduced speed");
    p->Acceleration()=p->Velocity()=vec3(0);check(p->SetLocation(carryOrigin),"restore carry lane");tick(1);
    auto* weapon=p->Weapon();check(weapon,"starting weapon retained");auto at=p->Location();action(ActionKind::Jump);tick(20);check(p->Physics()==PHYS_Walking && p->Location().z<at.z+10,"jump suppressed");
    check(!RuneKick::Start(p) && !RuneBash::Start(p) && !RuneBloodlust::Readiness(p),"other moves suppressed");
    snapshot();auto* remote=UObject::TryCast<UPawn>(replica.actor(authority.identity(p)));auto* remoteBody=replica.actor(authority.identity(body));
    check(remote && RuneCorpse::Held(remote)==remoteBody,"replica resolves carried body");
    auto center=body->CorpseBody.center;
    check(length((GetRuneMeshToWorld(body)*vec4(center,1)).xyz()-(GetRuneMeshToWorld(remoteBody)*vec4(remoteBody->CorpseBody.center,1)).xyz())<.3f,"overhead render matches");
    check(!RuneCorpse::Throw(remote) && !RuneCorpse::Pickup(remote,body),"replicas lack authority");
    auto pose=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(p))->ActorPose(p);check(pose.size()>20,"carry pose evaluates");
    tick(10);int before=target->Health();check(action(ActionKind::Attack),"attack throws body");tick(16);check(RuneCorpse::Held(p)==body,"visible release windup");tick(5);check(!RuneCorpse::Held(p) && body->CorpseBody.flying,"launch after windup");
    unsigned renderChecks=0;
    auto compareSkin=[&](UActor* copy) {
        auto* m=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(body));auto* other=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(copy));
        auto x=GetRuneMeshToWorld(body),y=GetRuneMeshToWorld(copy);
        m->PrepareMesh(body->GetByte("SkelMesh"),body->AnimSequence(),body->AnimFrame(),body);auto vertices=m->Verts;
        other->PrepareMesh(copy->GetByte("SkelMesh"),copy->AnimSequence(),copy->AnimFrame(),copy);
        check(vertices.size()==other->Verts.size(),"replicated skin topology");
        if(RuneCorpse::Posed(body)) {
            body->UpdateBspInfo();copy->UpdateBspInfo();auto bounds=body->BspInfo.BoundingBox;auto replicaBounds=copy->BspInfo.BoundingBox;
            auto* groups=static_cast<int*>(body->GetProperty("SkelGroupFlags"));
            for(const auto& tri:other->Tris)if(!(groups[tri.TextureIndex]&PF_Invisible))for(auto index:tri.Indices) {
                auto source=(x*vec4(vertices[index],1)).xyz(),remote=(y*vec4(other->Verts[index],1)).xyz();
                for(int axis=0;axis<3;++axis){check(source[axis]>=bounds.min[axis]-.01f && source[axis]<=bounds.max[axis]+.01f,"source culling contains posed geometry");check(remote[axis]>=replicaBounds.min[axis]-.01f && remote[axis]<=replicaBounds.max[axis]+.01f,"replica culling contains posed geometry");}
            }
        }
        for(size_t j=0;j<vertices.size();++j)check(length((x*vec4(vertices[j],1)).xyz()-(y*vec4(other->Verts[j],1)).xyz())<.4f,"replicated posed vertices");
    };
    for(int i=0;i<140;++i) {
        bool wasActive=body->CorpseBody.active;tick(1);auto world=snapshot();if(body->bDeleteMe())break;auto x=GetRuneMeshToWorld(body),y=GetRuneMeshToWorld(remoteBody);
        for(vec3 v:{vec3(0),vec3(10,20,30)})check(length((x*vec4(v,1)).xyz()-(y*vec4(v,1)).xyz())<.4f,"flight/settle render replication");
        if(i==0 || i==30 || i==90)compareSkin(remoteBody);
        if(wasActive && !body->CorpseBody.active){replica.present(.5);compareSkin(remoteBody);replica.present(1);}
        if(i==0) {
            RuneNetworkReplica late;auto ref=authority.identity(body);
            for(const auto& e:authority.actorBaseline())if(e.ref==ref){if(e.operation==ActorOperation::Spawn)check(late.spawn(game,e),"late corpse spawn");else if(e.operation==ActorOperation::Appearance)check(late.appearance(game,e),"late corpse appearance");}
            auto entity=std::find_if(world.begin(),world.end(),[&](const Entity& e){return e.ref==ref;});check(entity!=world.end() && (entity->flags&4),"flight explicitly identifies relaxed pose");
            Snapshot joined{1,0,tickId,{*entity}},decoded;Bytes bytes;check(encodeSnapshot(joined,nullptr,bytes)&&decodeSnapshot(bytes,nullptr,decoded),"late corpse wire");
            check(late.install(decoded),"late corpse install");late.present(1);compareSkin(late.actor(ref));
        }
        ++renderChecks;
    }
    check(target->Health()<before && p->CorpseCarry.hits==1,"one impact damages once");check(!body->CorpseBody.active && !body->bCarriedItem() && !body->GetUObject("AttachParent"),"settles independently");
    check(p->Weapon()==weapon && weapon->Owner()==p,"weapon ownership retained");
    // Reuse, drop, save normalization and death cleanup.
    p->SetLocation(origin);p->SetPhysics(PHYS_Walking);p->Velocity()=vec3(0);p->GotoState("PlayerWalking","Begin");p->GetUObject("AnimProxy")->GotoState("Idle","Begin");target->SetLocation(origin+direction*400.f);tick(80);moveBody();
    check(RuneCorpse::Pickup(p,body),"body reusable");
    {auto laws=RuneBattleLaws::Campaign.load();RuneBattleLaws::Campaign=laws&~RuneBattleLaws::Corpse;RuneCorpse::Tick(p,.02f);
     check(!RuneCorpse::Held(p)&&!body->bCarriedItem(),"disabling corpse law safely drops held body");RuneBattleLaws::Campaign=laws;}
    tick(100);moveBody();check(RuneCorpse::Pickup(p,body),"re-enabled corpse law restores pickup");
    std::cout<<"LAWS_CORPSE_INTERACTION_OK valid_body disabled_pickup safe_drop reenable\n";
    RuneCorpse::BeforeSave();check(!RuneCorpse::Busy(p) && !body->CorpseBody.active,"save drops transient carry");tick(100);moveBody();
    check(RuneCorpse::Pickup(p,body),"pickup after save");p->Health()=0;RuneCorpse::Tick(p,.02f);check(!RuneCorpse::Held(p),"death drops carry");p->Health()=1000;tick(100);moveBody();
    // Shield defence and an original inventory item's Use priority.
    auto* shield=UObject::TryCast<UActor>(target->GetUObject("Shield"));
    if(!shield){auto* item=target->Spawn(game.packages->FindClass("RuneI.VikingShield"),{},{},target->Location()+vec3(0,0,80),{});check(item,"spawn shield");item->SetFloat("RespawnTime",0);auto* copy=CallEvent(item,"SpawnCopy",{ExpressionValue::ObjectValue(target)}).ToObject();CallEvent(target,"AcquireInventory",{ExpressionValue::ObjectValue(copy)});shield=UObject::TryCast<UActor>(target->GetUObject("Shield"));}
    check(shield,"equipped shield");
    p->SetLocation(origin);target->SetLocation(origin+direction*260.f);target->Velocity()=vec3(0);target->SetPhysics(PHYS_Walking);target->SetRotation(Rotator::FromVector(-direction));
    p->CorpseCarry.cooldown=0;moveBody();check(RuneCorpse::Pickup(p,body),"shield case pickup");tick(25);RuneCorpse::Throw(p);
    int guardedHealth=target->Health();unsigned blocks=p->CorpseCarry.blocks;
    for(int i=0;i<150;++i){target->bAltFire()=1;shield->GotoState("Active","Begin");tick(1);}
    check(p->CorpseCarry.blocks==blocks+1 && target->Health()==guardedHealth,"shield stops damage and records block");
    target->bAltFire()=0;CallEvent(target,"ActivateShield",{ExpressionValue::BoolValue(false)});
    if(teamMode) {
        p->SetLocation(origin);p->Velocity()=vec3(0);p->SetPhysics(PHYS_Walking);p->GetUObject("AnimProxy")->GotoState("Idle","Begin");
        target->SetLocation(origin+direction*260.f);target->SetPhysics(PHYS_Walking);target->Velocity()=vec3(0);target->PlayerReplicationInfo()->SetByte("Team",0);
        moveBody();p->CorpseCarry.cooldown=0;check(RuneCorpse::Pickup(p,body),"friendly case pickup");tick(25);before=target->Health();auto oldHits=p->CorpseCarry.hits;
        RuneCorpse::Throw(p);tick(160);check(target->Health()==before && p->CorpseCarry.hits==oldHits,"friendly fire off suppresses damage and shove");target->PlayerReplicationInfo()->SetByte("Team",1);
    }
    // Normal bot decisions consume only replicated observations and emit Use,
    // then a Throw intent; they cannot attach or damage anything themselves.
    p->SetLocation(origin);p->Velocity()=vec3(0);p->SetPhysics(PHYS_Walking);p->GetUObject("AnimProxy")->GotoState("Idle","Begin");p->CorpseCarry.cooldown=0;moveBody();
    auto world=snapshot();auto own=authority.identity(p),foe=authority.identity(target);MatchStatus status;status.mode=GameMode::Deathmatch;status.phase=MatchPhase::Playing;
    PlayerScore score1;score1.player=1;score1.pawn=own;score1.team=0;PlayerScore score2;score2.player=2;score2.pawn=foe;score2.team=1;status.scores={score1,score2};
    auto self=std::find_if(world.begin(),world.end(),[&](const Entity& e){return e.ref==own;});check(self!=world.end(),"bot self snapshot");
    RuneBot::Brain brain(717,1);bool botUse=false;
    for(uint64_t time=1000;time<5000 && !botUse;time+=100){auto d=brain.think(game,replica,world,status,own,time);botUse=d.action==ActionKind::Use;}
    check(botUse,"bot chooses nearby corpse pickup");check(RuneCorpse::Pickup(p,body),"bot intent authority pickup");tick(30);world=snapshot();bool botThrow=false;
    for(uint64_t time=5000;time<8500 && !botThrow;time+=100){auto d=brain.think(game,replica,world,status,own,time);botThrow=d.action==ActionKind::Throw;}
    check(botThrow,"bot chooses aimed corpse throw");RuneCorpse::Drop(p);tick(100);moveBody();p->CorpseCarry.cooldown=0;
    // Eligibility and safety cases are independent of gore presentation.
    auto scale=body->DrawScale();body->DrawScale()=2;check(!RuneCorpse::Throwable(body),"giant rejected");body->DrawScale()=scale;
    body->SetBool("bDecorative",true);check(!RuneCorpse::Throwable(body),"decorative body rejected");body->SetBool("bDecorative",false);
    auto* flags=static_cast<int*>(body->GetProperty("SkelGroupFlags"));auto** skins=static_cast<UObject**>(body->GetProperty("SkelGroupSkins"));
    auto oldFlags=flags[5];auto* oldSkin=skins[5];flags[5]=0;skins[5]=game.packages->GetPackage("RuneFX")->GetUObject("Texture","gore_bone");
    check(skins[5] && !RuneCorpse::Throwable(body),"severed body rejected");flags[5]=oldFlags;skins[5]=oldSkin;
    if(!multi) {
        p->SetLocation(origin+direction*300.f);p->SetPhysics(PHYS_Walking);p->Velocity()=vec3(0);target->SetLocation(origin+direction*500.f);
        auto* npc=UObject::TryCast<UPawn>(p->Spawn(game.packages->FindClass("RuneI.DarkViking"),{},{},origin,{}));check(npc,"campaign humanoid spawn");
        tick(60);npc->Enemy()=p;npc->GotoState("Fighting","Begin");npc->SetPhysics(PHYS_Walking);npc->Velocity()=npc->Acceleration()=vec3(0);npc->CorpseCarry.think=0;
        npc->SetBool("bSwingingHigh",false);npc->SetBool("bSwingingLow",false);if(auto* proxy=npc->GetUObject("AnimProxy"))proxy->GotoState("Idle","Begin");
        body->SetBase(nullptr,true);body->SetPhysics(PHYS_None);body->Velocity()=vec3(0);body->SetLocation(npc->Location()+direction*65.f-vec3(0,0,24));
        check(RuneCorpse::Eligible(npc),"eligible campaign humanoid");RuneCorpse::Tick(npc,.02f);check(RuneCorpse::Held(npc)==body,"campaign AI opportunistic pickup");
        tick(105);check(npc->CorpseCarry.throws==1,"campaign AI winds up and throws");tick(100);check(!body->CorpseBody.active,"campaign throw settles");
        npc->Destroy();p->SetLocation(origin);p->SetPhysics(PHYS_Walking);p->Velocity()=vec3(0);moveBody();p->CorpseCarry.cooldown=0;
    }
    check(RuneCorpse::Pickup(p,body),"disconnect pickup");CheckRuneCorpseMotion(game,p,body,check);
    snapshot();auto* cosmeticReplica=replica.actor(authority.identity(body));
    check(cosmeticReplica && RuneCorpse::Posed(cosmeticReplica),"remote carried body uses cosmetic pose");
    RuneCorpse::TickMotion(cosmeticReplica,1.f/120);
    check(cosmeticReplica->CorpseBody.motionReady && !cosmeticReplica->CorpseBody.limbs.empty(),"replica runs local limb dynamics without physics authority");
    authority.unbind(1);check(!RuneCorpse::Busy(p),"disconnect drops body");tick(100);
    RuneCorpse::TickMotion(body,1.f/60);check(!body->CorpseBody.motionReady,"settled corpse discards carry dynamics");moveBody();p->CorpseCarry.cooldown=0;
    check(RuneCorpse::Pickup(p,body),"pickup for deletion");body->Destroy();check(!RuneCorpse::Held(p) && !p->CorpseCarry.body,"destroyed body unlinks");
    std::cout<<"RUNE_CORPSE_OK mode="<<mode<<" checks="<<checks<<" render_checks="<<renderChecks<<" hits="<<p->CorpseCarry.hits<<std::endl;
}
