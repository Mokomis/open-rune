#include "RuneFinisher.h"
#include "RuneLeg.h"
#include "RuneCorpse.h"
#include "RuneSlam.h"
#include "RuneBattleLaws.h"
#include "Precomp.h"
#include "RuneNetwork.h"
#include "RuneBot.h"
#include "Utils/CommandLine.h"
#include "RuneBloodPresentation.h"
#include "Packages/Engine/Resources/UPalette.h"
#include "RuneDualWield.h"
#include "RuneBloodlust.h"
#include "RunePhysics.h"
#include "RuneBash.h"
#include "RuneKick.h"
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
void RunRuneBotTraversalProbe(Engine& game);
void RunRuneBotTeamProbe(Engine& game);
void RunRuneBotCrowdProbe(Engine& game);
void RunRuneCeilingBotProbe(Engine& game) {
 auto check=[](bool b,const char* why){if(!b)throw std::runtime_error(std::string("ceiling bots: ")+why);};
 RuneBattleLaws::Campaign=RuneBattleLaws::All;RuneBloodlust::AllowNormal=true;RuneBloodlust::Enabled=true;RuneTactics::Enabled=false;RunePhysics::RequestedMode=2;
 game.NetworkAuthority=true;auto url=game.GetDefaultURL("DM-Hildir.run");url.AddOrReplaceOption("game=RuneI.RuneMultiPlayer");game.LoadMap(url);
 RuneNetworkAuthority authority(game);RuneNetworkReplica replica;std::vector<UPlayerPawn*> pawns;std::vector<RuneBot::Brain> brains;
 std::vector<uint64_t> actions;std::vector<int> frozen;std::vector<bool> reported;
 for(int i=0;i<7;++i){auto* v=game.LoginNetworkPlayer("CeilingBot"+std::to_string(i),-1,i*2,0);check(v,"login");check(authority.bind(i+1,v,1),"bind");pawns.push_back(v);brains.emplace_back(73+i*17,2);actions.push_back(0);frozen.push_back(0);reported.push_back(false);}
 game.viewport->Actor()=pawns[0];uint64_t tickId=0,snapshotId=0,now=1000;World world;MatchStatus status;status.mode=GameMode::Deathmatch;status.phase=MatchPhase::Playing;
 unsigned hangs=0,grabs=0;int frames=commandline->GetArgInt("","--ceiling-bot-frames",7200);
 for(int frame=0;frame<frames;++frame) {
  now=1000+(tickId*1000)/60;authority.beginTick(++tickId,now);authority.commitContacts();
  if(frame%6==0){check(authority.capture(world),"world capture");uint64_t release=0;
   for(const auto& event:authority.pendingActors()) {if(event.operation==ActorOperation::Spawn)check(replica.spawn(game,event),"spawn replica");else if(event.operation==ActorOperation::Appearance)check(replica.appearance(game,event),"appearance");else check(replica.unbind(event.ref),"despawn");release=event.id;}
   if(release)authority.releaseActorsThrough(release);check(replica.install(Snapshot{++snapshotId,0,tickId,world}),"install");replica.present(1);
   for(auto* a:game.Level->Actors)if(a && a->NetworkReplica && a->IsA("Pawn"))game.Level->Collision.RemoveFromCollision(a);
   for(int i=0;i<7;++i){auto d=brains[i].think(game,replica,world,status,authority.identity(pawns[i]),now);auto in=d.input;in.sequence=tickId;in.createdMs=now;authority.input(i+1,in);if(d.action){AdmittedAction a{{++actions[i],in.sequence,now,in.life,*d.action},in,now};authority.action(i+1,a,now);}}
   const auto& combat=authority.pendingCombat();if(!combat.empty())authority.releaseCombatThrough(combat.back().id);authority.drainSounds();
  }
  game.LevelInfo->TimeSeconds()+=1.f/60;game.TotalTime+=1.f/60;game.Level->Tick(1.f/60,false);
  for(int i=0;i<7;++i){auto* v=pawns[i];bool trapped=v->Health()>0 && !RunePhysics::Controls(v) && (v->GetStateName()=="EdgeHanging" || v->Physics()==PHYS_None || v->Physics()==PHYS_Flying);if(trapped)++frozen[i];else frozen[i]=0;
   if(frozen[i]==1)++grabs;
   if(frozen[i]>=180 && !reported[i]){reported[i]=true;++hangs;std::cout<<"BOT_CEILING_STUCK class="<<v->Class->Name.ToString()<<" t="<<frame/60.f<<" state="<<v->GetStateName().ToString()<<" physics="<<int(v->Physics())<<" anim="<<v->AnimSequence().ToString()<<" frame="<<v->AnimFrame()<<" rate="<<v->AnimRate()<<" xyz="<<v->Location().x<<","<<v->Location().y<<","<<v->Location().z<<std::endl;}
  }
 }
 unsigned leaps=0,attacks=0;for(auto& b:brains){leaps+=b.leaps;attacks+=b.attacks;}
 std::cout<<"RUNE_CEILING_BOT_OK seconds="<<frames/60.f<<" transitions="<<grabs<<" stuck="<<hangs<<" leaps="<<leaps<<" attacks="<<attacks<<std::endl;
}

void RunRuneNetworkNativeProbe(Engine& game)
{
    if(commandline->HasArg("","--rune-ceiling-bot-probe")){RunRuneCeilingBotProbe(game);return;}
    if(commandline->HasArg("","--rune-bot-traversal-probe")){RunRuneBotTraversalProbe(game);return;}
    if(commandline->HasArg("","--rune-bot-crowd-probe") || commandline->HasArg("","--rune-bot-group-probe")){RunRuneBotCrowdProbe(game);return;}
    if(commandline->HasArg("","--rune-bot-team-probe")){RunRuneBotTeamProbe(game);return;}
    if(commandline->HasArg("","--rune-bot-goal-probe") || commandline->HasArg("","--rune-bot-navigation-probe")){RunRuneBotGoalProbe(game);return;}
 // This fixture starts with the original contact/pose baseline before its explicit mode tests.
 if(!commandline->HasArg("","--rune-physics"))RunePhysics::RequestedMode=RunePhysics::Legacy;
 struct RestoreLaws{uint32_t value=RuneBattleLaws::Campaign.load();~RestoreLaws(){RuneBattleLaws::Campaign=value;}} restoreLaws;
 RuneBattleLaws::Campaign=RuneBattleLaws::All; // Explicit fixture rules; real campaign still rejects leap.
    auto check=[](bool v,const char* why){if(!v)throw std::runtime_error(std::string("network native combat: ")+why);};
    check(RuneBloodlust::AllowNormal.load(),"normal-combat moves are enabled by default");
    struct RestoreNormal{bool value;~RestoreNormal(){RuneBloodlust::AllowNormal=value;}} restoreNormal{RuneBloodlust::AllowNormal.load()};
    RuneBloodlust::AllowNormal=false; // Explicit legacy-mode baseline before normal-combat cases.
    RuneDualWield::Enabled=true;RuneBloodlust::Enabled=true;RuneTactics::Enabled=false;
    game.NetworkAuthority=true;
    auto url=game.GetDefaultURL("DM-Hildir.run");url.AddOrReplaceOption(commandline->GetArg("","--fin-case","")=="friendly"?"game=RuneI.TeamGame":"game=RuneI.RuneMultiPlayer");game.LoadMap(url);
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
            RunePhysics::Release(a);
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
        auto* klass=std::string(type).find('.')==std::string::npos?game.packages->GetPackage("RuneI")->GetClass(type):game.packages->FindClass(type);
        auto* item=a->Spawn(klass,{},{},a->Location()+vec3(0,0,100),{});check(item,"spawn item");
        item->SetFloat("RespawnTime",0);auto* copy=CallEvent(item,"SpawnCopy",{ExpressionValue::ObjectValue(a)}).ToObject();check(copy==item,"real inventory item");
        if(acquire)CallEvent(a,"AcquireInventory",{ExpressionValue::ObjectValue(copy)});copy->GotoState("Active","Begin");return copy;
    };
    auto action=[&](uint64_t peer,ActionKind kind,uint16_t held=0,uint64_t pressed=0){
        Input input;input.life=authority.playerLife(peer);input.sequence=++actionId;input.createdMs=pressed?pressed:now;
        input.yaw=uint16_t((peer==1?p:target)->Rotation().Yaw);input.held=held;input.forward=1000;
        AdmittedAction a{{actionId,actionId,input.createdMs,input.life,kind},input,input.createdMs};return authority.action(peer,a,now);
    };
    RuneNetworkReplica replica;std::set<uint32_t> spawned;
    const bool oldSwipeHost=commandline->HasArg("","--rune-swipe-old-host");
    auto snapshot=[&](){
        World world;check(authority.capture(world),"capture native world");
        if(oldSwipeHost)for(auto& entity:world)if(entity.kind==EntityKind::Weapon)entity.flags&=~uint8_t(4);
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
    if(commandline->HasArg("","--rune-swipe-replica-probe")) {
        RuneDualWield::Enabled=false;reset(240);give(p,"VikingShortSword",true);
        auto* remote=snapshot();check(remote && remote->Weapon(),"replica equipped for swipe");
        replica.tickEffects(1.f/60);check(!remote->Weapon()->GetUObject("Swipe"),"no idle swipe");
        check(action(1,ActionKind::Attack),"real sword swing for swipe");
        int hostFrames=0,clientFrames=0,overlap=0;float maxArea=0;
        for(int i=0;i<100;++i) {
            tick(1);
            if(i%2==0)remote=snapshot();
            replica.tickEffects(1.f/60);
            bool host=p->Weapon()->GetUObject("Swipe") || p->Weapon()->GetStateName()=="Swinging";
            auto* swipe=UObject::TryCast<UActor>(remote->Weapon()->GetUObject("Swipe"));
            bool visible=false;
            if(swipe && swipe->GetBool("IsLoaded")) {
                check(swipe->Class->Name=="WeaponSwipeBlue","authored swipe color");
                RuneParticles::ArrayView particles(swipe);
                for(int j=0;j<int(swipe->GetByte("ParticleCount"));++j)if(particles.Valid(j)) {
                    auto pt=[&](int k){return particles.Ref<vec3>(j,RuneParticles::Field::Points,k);};
                    float area=length(cross(pt(1)-pt(0),pt(3)-pt(0)));
                    maxArea=std::max(maxArea,area);visible|=area>1.f;
                }
            }
            hostFrames+=host;clientFrames+=visible;overlap+=visible && host;
            if(i==99)check(!visible,"swipe fades after swing");
        }
        std::cout<<"REPLICA_SWIPE host="<<hostFrames<<" visible="<<clientFrames<<" overlap="<<overlap<<" area="<<maxArea<<std::endl;
        check(hostFrames && overlap && maxArea>1.f,"replica blade sweeps a visible strip during the real attack");
        return;
    }
    if(commandline->HasArg("","--rune-ceiling-recovery-probe")) {
      reset(150);p->SetLocation(origin+vec3(0,0,100));p->GotoState("EdgeHanging","Begin");p->SetPhysics(PHYS_Flying);
      p->AnimSequence()="intropullupA";p->AnimFrame()=.99f;p->AnimRate()=0;
      p->GetUObject("AnimProxy")->SetName("AnimSequence","intropullupA");p->GetUObject("AnimProxy")->SetFloat("AnimRate",0);
      const auto at=p->Location();snapshot();World observed;check(authority.capture(observed),"capture hanging avatar");
      MatchStatus status;status.mode=GameMode::Deathmatch;status.phase=MatchPhase::Playing;RuneBot::Brain brain(73);
      auto own=authority.identity(p);auto d=brain.think(game,replica,observed,status,own,now);
      check(!d.action || *d.action!=ActionKind::Jump,"normal climb given time");
      d=brain.think(game,replica,observed,status,own,now+1100);check(d.action && *d.action==ActionKind::Jump,"bot recognizes its own stalled climb");
      now+=1100;check(action(1,ActionKind::Jump),"authority admits ledge recovery");
      check(p->Physics()==PHYS_Falling && p->GetStateName()=="PlayerWalking" && length(p->Location()-at)<.001f,"stale climb releases without teleport");
      check(!action(1,ActionKind::BloodLeap),"ledge release cannot arm an airborne leap");
      tick(120);check(p->Location().z<at.z-20 && p->Physics()==PHYS_Walking,"released pawn lands using world collision");
      std::cout<<"RUNE_CEILING_RECOVERY_OK bot_jump=1 swept_landing=1 no_teleport=1"<<std::endl;return;
    }
    if(commandline->HasArg("","--rune-stagger-network-probe")) {
      reset(150);RunePhysics::RequestedMode=2;give(target,"VikingShield",true);
      int before=target->Health();CallEvent(target,"JointDamaged",{ExpressionValue::IntValue(24),ExpressionValue::ObjectValue(p),ExpressionValue::VectorValue(target->Location()),ExpressionValue::VectorValue(vec3(0)),ExpressionValue::NameValue("blunt"),ExpressionValue::IntValue(0)});
      check(target->Health()<before && target->RuneBashPhase==3,"committed authority damage staggers");snapshot();
      auto* rt=UObject::Cast<UPawn>(replica.actor(authority.identity(target)));
      check(rt->AnimSequence()==target->AnimSequence() && rt->AnimRate()>0,"directional stagger and playback rate replicate");
      tick(45);check(target->GetStateName()=="PlayerWalking" && target->RuneBashPhase==0,"stagger returns control");
      check(action(2,ActionKind::RaiseShield,2),"blocking available after stagger");
      check(!RuneBash::DamageReaction(target,p,24),"authority immunity prevents repeat stun");
      std::cout<<"RUNE_STAGGER_NETWORK_OK replicated=1 control_restored=1 immunity=1"<<std::endl;return;
    }
    if(commandline->HasArg("","--rune-finisher-probe")){
        auto type=commandline->GetArg("","--rune-finisher-probe","VikingShortSword");
        auto mode=commandline->GetArg("","--fin-case","rear");
        std::string second="RomanSword";
        if(mode.starts_with("pairtorso-")){second=mode.substr(10); mode="pairtorso";}
        if(type=="roster"){
            auto package=commandline->GetArg("","--fin-case","RuneI");
            auto all=game.packages->GetPackage(package=="list"?"RuneI":package)->GetAllObjects<UClass>();
            for(auto* c:all){
                for(auto* base=static_cast<UStruct*>(c);base;base=base->BaseStruct)if(base->Name=="Weapon"){
                    std::cout<<"FIN_WEAPON "<<c->Name.ToString()<<" damage="<<c->GetDefaultObject<UWeapon>()->GetInt("Damage")<<std::endl;
                    break;
                }
            }
            return;
        }
        bool pinTest=mode=="pinattack" || mode=="pinmove" || mode=="pinbot" || mode=="pindestroy";
        RuneDualWield::Enabled=mode=="pair" || mode=="pairtorso" || mode=="cross";
        reset(45);
        auto* w=UObject::Cast<UWeapon>(give(p,type.c_str(),true));
        if(mode=="pair" || mode=="pairtorso"){
            auto* spare=UObject::Cast<UWeapon>(give(p,second.c_str(),false));
            check(RuneDualWield::Equip(p,spare),"equip finisher pair");
        }
        check(w==p->Weapon(),"finisher fixture weapon equipped");
        auto faceAway=[&](){target->SetRotation(Rotator::FromVector(direction));target->ViewRotation()=target->Rotation();};
        if(mode=="rear" || mode=="pair" || mode=="off" || mode=="sweep" || mode=="sever" || mode=="leg" || mode=="bot" || mode=="throw" || mode=="pairtorso" || pinTest)faceAway();
        if(mode=="off")RuneBattleLaws::Campaign=RuneBattleLaws::All&~RuneBattleLaws::Finishers;
        if(mode=="stun")target->RuneBashPhase=2;
        if(mode=="natural" || mode=="ability")target->Health()=1;
        if(mode=="miss" || mode=="expired" || mode=="riposte"){
            give(target,"VikingShortSword",true);
            reset(240);
            check(action(2,ActionKind::Attack),"defender swings outside reach");
            int n=0;
            while(target->finisherMissUntil<=game.TotalTime && n++<110)tick(1);
            std::cout<<"FIN_MISS start="<<target->finisherSwingStart<<" end="<<target->finisherMissUntil<<" now="<<game.TotalTime<<std::endl;
            check(target->finisherMissUntil>game.TotalTime,"actual missed swing opens a response");
            check(target->SetLocation(origin+direction*45.f),"close on recovery");
            if(mode=="expired")tick(22);
            World observed;check(authority.capture(observed),"capture missed swing opening");
            auto it=std::find_if(observed.begin(),observed.end(),[&](const Entity& e){return e.ref==authority.identity(target);});
            check(it!=observed.end() && bool(it->flags&FinisherOpeningFlag)==(mode!="expired"),"bot sees actual whiff interval only");
        }
        if(mode=="friendly"){
            faceAway();game.GameInfo->SetFloat("FriendlyFireScale",0);
            for(auto* fighter:{p,target})fighter->GetUObject("PlayerReplicationInfo")->SetByte("Team",0);
        }
        if(mode=="cross"){
            give(target,"VikingShortSword",true);check(RuneDualWield::Equip(target,UObject::Cast<UWeapon>(give(target,"RomanSword",false))),"crossed guard fixture");
            tick(2);check(action(2,ActionKind::RaiseShield,2),"real crossed guard input");tick(18);
            check(RuneDualWield::Blocks(target,p),"crossed guard ready");
            target->finisherMissUntil=game.TotalTime+.20; // Expose a frontal opening to challenge the defense.
        }
        if(mode=="block"){give(target,"VikingShield",true);check(action(2,ActionKind::RaiseShield,2),"raise shield");tick(20);}
        auto* m=UObject::Cast<USkelModel>(GetRuneDisplayMesh(target));
        int joint=-1;for(size_t j=0;j<m->Joints.size();++j)if(m->Joints[j].Name==(mode=="sever"?"head":mode=="leg"?"lknee":"chest"))joint=int(j);
        check(joint>=0 && joint<32,"fixture hit joint");
        int initial=target->Health();
        if(mode=="kickopening" || mode=="kickwounded"){
            faceAway();
            target->RuneBashPhase=2;
            if(mode=="kickwounded")target->Health()=1;
            initial=target->Health();
            check(RuneFinisher::Opening(p,target),"kick fixture has a genuine finisher opening");
            RuneKick::CommitImpact(p,target,false);
            std::cout<<"KICK_ORDINARY before="<<initial<<" after="<<target->Health()<<" hands="<<RuneFinisher::Hands(p)<<std::endl;
            check(target->Health()<initial && (target->Health()<=0)==(mode=="kickwounded"),"kick uses ordinary damage");
            check(!RuneFinisher::Hands(p) && !p->finisherCut,"kick cannot start a kill move");
            return;
        }
        if(mode=="ability" || mode=="abilityfront"){
            CallEvent(target,"JointDamaged",{ExpressionValue::IntValue(12),ExpressionValue::ObjectValue(p),ExpressionValue::VectorValue(target->Location()),ExpressionValue::VectorValue(direction*1200.f),ExpressionValue::NameValue("Blunt"),ExpressionValue::IntValue(0)});
        }else if(mode=="bot"){
            RuneBot::Brain brain(717,1);MatchStatus status;status.mode=GameMode::Deathmatch;status.phase=MatchPhase::Playing;
            bool attacked=false;
            for(int attempt=0;attempt<35 && !attacked;++attempt){
                tick(3);snapshot();World seen;check(authority.capture(seen),"bot sees combatants");
                for(auto* a:game.Level->Actors)if(a && a->NetworkReplica)game.Level->Collision.RemoveFromCollision(a);
                auto d=brain.think(game,replica,seen,status,authority.identity(p),now);
                if(!d.action)continue;
                auto input=d.input;input.sequence=++actionId;input.createdMs=now;
                AdmittedAction request{{actionId,actionId,now,input.life,*d.action},input,now};
                bool admitted=authority.action(1,request,now);
                attacked=admitted && *d.action==ActionKind::Attack;
            }
            check(attacked,"multiplayer bot selects and commits real Attack");
        }else if(mode=="throw" || mode=="throwfront"){
            w->SetOwner(p);w->GotoState("Throw","Begin");w->Velocity()=direction*700.f;
            CallEvent(w,"Touch",{ExpressionValue::ObjectValue(target)});
        }else if(mode=="sweep" || mode=="pair" || mode=="riposte"){
            check(action(1,ActionKind::Attack),"real attack action");
        }else{
            CallEvent(w,"StartAttack");
            CallEvent(w,"DoWeaponSwipe",{ExpressionValue::ObjectValue(target),ExpressionValue::IntValue(1u<<joint),ExpressionValue::IntValue(0),
                ExpressionValue::VectorValue(target->Location()),ExpressionValue::VectorValue(vec3(0,0,1)),ExpressionValue::VectorValue(direction*100.f)});
        }
        bool lethal=mode!="off" && mode!="front" && mode!="expired" && mode!="block" && mode!="throwfront" && mode!="friendly" && mode!="cross" && mode!="abilityfront";
        const bool ordinaryWeapon=w->IsA("Torch") || w->IsA("LimbWeapon");
        if(ordinaryWeapon)lethal=mode=="natural" || mode=="ability";
        bool physical=false,shown=false,severed=false;int delay=-1;
        for(int i=0;i<75;++i){
            tick(1);
            if(target->Health()>0)continue;
            if(delay<0)delay=i;
            unsigned bodies=0;UActor* body=nullptr;
            for(auto* a:game.Level->Actors)if(a && !a->NetworkReplica && !a->bDeleteMe() && a->IsA("RuneCarcass") && RunePhysics::Controls(a)){
                physical=true;++bodies;body=a;
                if(mode=="sever" && a->HasProperty("SkelGroupFlags"))severed=static_cast<int*>(target->GetProperty("BodyPartHealth"))[5]<=0;
            }
            if(i==delay){
                auto* remote=snapshot();
                shown=remote && RuneFinisher::Hands(remote)==((mode=="pair" || mode=="pairtorso")?3:(mode=="throw" || mode=="ability" || ordinaryWeapon)?0:1);
                check(remote->finisherCut==p->finisherCut,"remote finisher motion matches hit region");
                check(physical && bodies==1,"single articulated corpse on kill tick");
                auto* copy=replica.actor(authority.identity(body));
                check(copy && RunePhysics::Controls(copy),"remote articulated body on first death snapshot");
            }
            if(i==delay+6 && (mode=="pairtorso" || (mode=="pair" && !p->finisherCut))){
                check(!p->finisherCut,"torso impale");
                auto* model=UObject::Cast<USkelModel>(GetRuneDisplayMesh(p));auto bones=model->ActorJointMatrices(p);
                for(auto* blade:{p->Weapon(),p->Dual.offhand}){
                    auto grip=GetRuneMeshToWorld(p)*RuneDualWield::Attachment(*model,p,blade,bones);
                    float side=blade==p->Weapon()?1.f:-1.f;auto family=RuneFinisher::Style(blade);
                    vec3 aim=family==RuneFinisher::Family::Sword?vec3(1,-side*.08f,0):family==RuneFinisher::Family::Axe?vec3(.5f,-side*.75f,-.45f):vec3(.55f,-side*.20f,-.82f);
                    vec3 expected=direction*aim.x+vec3(-direction.y,direction.x,0)*aim.y+vec3(0,0,aim.z);
                    float alignment=dot(normalize((grip*vec4(blade->GetVector("SweepVector"),0)).xyz()),normalize(expected));
                    std::cout<<"FIN_BLADE_FORWARD "<<alignment<<std::endl;
                    check(alignment>.80f,"both rendered weapons follow their own finishing motion");
                }
            }
            if(i>delay+6)break;
        }
        std::cout<<"FIN_RESULT weapon="<<type<<" mode="<<mode<<" before="<<initial<<" after="<<target->Health()<<" delay="<<delay<<" physical="<<physical<<" remote="<<shown<<" sever="<<severed<<std::endl;
        check(lethal==(target->Health()<=0),"lethality");
        if(pinTest){
            tick(78);
            auto* copy=snapshot();
            check(p->finisherHeld && copy && copy->finisherHeld,"held pose still replicated well after attack");
            check(length((copy->finisherAimWorld-copy->Location())-(p->finisherAimWorld-p->Location()))<.25f,
                  "remote and late snapshots carry the exact blade contact");
            UActor* corpse=nullptr;
            for(auto* a:game.Level->Actors)if(a && RunePhysics::Impaled(a))corpse=a;
            check(corpse && RunePhysics::Controls(corpse) && !RuneCorpse::Throwable(corpse),"supported ragdoll cannot be picked up");
            int hp=target->Health();
            if(mode=="pinbot"){
                RuneBot::Brain brain(717,1);MatchStatus status;status.mode=GameMode::Deathmatch;status.phase=MatchPhase::Playing;
                World observed;check(authority.capture(observed),"bot observes active impale");
                auto d=brain.think(game,replica,observed,status,authority.identity(p),now);
                check(d.action && *d.action==ActionKind::Attack,"bot chooses to pull the blade free");
                check(action(1,*d.action),"bot releases through host Attack");
            }else if(mode=="pinattack")check(action(1,ActionKind::Attack),"next deliberate attack withdraws");
            else if(mode=="pinmove"){
                for(auto* a:game.Level->Actors)if(a && a->NetworkReplica)game.Level->Collision.RemoveFromCollision(a);
                check(p->SetLocation(p->Location()+direction*12.f),"lean into the blade");tick(2);
                check(p->finisherHeld,"forward movement keeps the body supported");
                check(p->SetLocation(p->Location()-direction*22.f),"step away from the body");
            }else {
                corpse->Destroy();
            }
            tick(2);copy=snapshot();
            check(!p->finisherHeld && !copy->finisherHeld && !RunePhysics::Impaled(corpse),"authority and client release");
            check(target->Health()==hp,"withdrawal does not deal damage twice");
            if(mode!="pindestroy")check(RunePhysics::Controls(corpse),"released body stays the same articulated corpse");
            tick(14);
            check(!RuneFinisher::Hands(p) && !RuneFinisher::Hands(copy),"both arms leave the pin pose");
        }
        if(ordinaryWeapon && target->Health()>0){
            snapshot();auto* shownTarget=UObject::TryCast<UPawn>(replica.actor(authority.identity(target)));
            check(shownTarget && shownTarget->Health()==target->Health() && !RuneFinisher::Hands(p),"ordinary weapon health replicates without a finishing pose");
        }
        if(mode=="leg"){
            unsigned detached=0;for(auto* a:game.Level->Actors)if(a && !a->NetworkReplica && !a->bDeleteMe() && RuneLeg::Detached(a))++detached;
            check(detached==(RuneFinisher::Style(w)==RuneFinisher::Family::Hammer?0:1),"leg cut for edged hit only");
            if(detached){
                UActor* leg=nullptr;
                for(auto* a:game.Level->Actors)if(a && !a->NetworkReplica && !a->bDeleteMe() && RuneLeg::Detached(a))leg=a;
                int upright=0,turning=0;bool flat=false;
                for(int f=0;f<120;++f){
                    tick(1);snapshot();
                    auto* copy=replica.actor(authority.identity(leg));
                    check(copy && RuneLeg::Detached(copy),"replicated leg mesh");
                    int angle=std::abs(int(int16_t(leg->Rotation().Roll)));
                    check(std::abs(int(int16_t(copy->Rotation().Roll-leg->Rotation().Roll)))<=1,"replicated leg tilt");
                    if(leg->Physics()==PHYS_Rotating && angle==0)++upright;
                    if(angle>0 && angle<16384)++turning;
                    if(leg->Physics()==PHYS_None && angle==16384){flat=true;break;}
                }
                std::cout<<"LEG_FALL upright_ticks="<<upright<<" tipping_ticks="<<turning<<" flat="<<flat<<std::endl;
                check(upright>=4 && turning>=4 && flat,"detached leg balances, tips and rests");
            }
        }
        if(lethal)check(physical && shown && (mode!="sever" || severed),"physical finisher and replica");
        else if(mode=="friendly" || mode=="block")check(target->Health()==initial,"full protection");
        else check(target->Health()<initial,"ordinary damage");
        return;
    }
    reset(150);give(p,"VikingShortSword",true);auto* off=UObject::TryCast<UWeapon>(give(p,commandline->HasArg("","--rune-dual-followup-probe") && !commandline->HasArg("","--follow-axe")?"VikingShortSword":"HandAxe",false));
    if(commandline->HasArg("","--rune-dual-followup-probe")) {
        give(p,"VikingShield",true);
        auto series=[&](bool dual,float distance) {
            RuneDualWield::Enabled=dual;
            int total=0,firstRight=0;
            for(int i=0;i<8;++i) {
                reset(distance);
                tick(1);
                check(RuneDualWield::Equipped(p)==dual,"requested equipment");
                int before=target->Health();
                check(action(1,ActionKind::Attack),"committed swing");
                if(dual && distance==45 && i==2) {
                    int elapsed=0;
                    while(elapsed<80 && !p->Dual.followAvailable){tick(1);++elapsed;}
                    auto* remote=snapshot();
                    check(remote && remote->Dual.followAvailable && !remote->Dual.left,"paired cut reaches remote player");
                    tick(80-elapsed);
                } else tick(80);
                int damage=before-target->Health();
                std::cout<<"FOLLOW_STRIKE dual="<<dual<<" distance="<<distance<<" swing="<<i+1<<" damage="<<damage<<std::endl;
                if(i==0)firstRight=damage;
                if(dual && distance==45 && i==2)check(damage>firstRight,"third attack lands both weapons");
                total+=damage;
            }
            return total;
        };
        int distance=commandline->GetArgInt("","--follow-distance",45);
        int single=series(false,distance);
        int paired=series(true,distance);
        check(single>0 && paired>single && paired<2*single,"offhand damage advantage below double");
        check(series(true,300)==0,"both weapons miss outside reach");
        std::cout<<"FOLLOW_BALANCE_OK shield="<<single<<" dual="<<paired<<std::endl;
        return;
    }
    // Native eligibility and execution gates, not just menu-model checks.
    {
      auto original=RuneBattleLaws::Campaign.load();RuneBattleLaws::Campaign=0;
      check(!RuneKick::Compatible(p)&&!RuneKick::Start(p),"unchecked kick rejects player and AI eligibility/execution");
      check(!RuneBash::Compatible(p)&&!RuneBash::Start(p),"unchecked bash rejects player and AI eligibility/execution");
      check(!RuneDualWield::Equip(p,off),"unchecked dual wield rejects equipment");
      check(!RuneBloodlust::Readiness(p)&&!RuneBloodlust::Leap(p,direction)&&!RuneBloodlust::Charge(p),"unchecked special moves reject activation");
      check(!RuneSlam::Supported(p)&&!RuneSlam::Arm(p),"unchecked slam rejects AI eligibility/activation");
      check(!RuneCorpse::Pickup(p,nullptr)&&!RunePhysics::UseProp(p),"unchecked interactions reject activation");
      check(!RunePhysics::ShieldRushKnockdown(target,p,vec3(100,0,150)),"unchecked knockdown rejects forced rush physics");
      RuneBattleLaws::Campaign=original;
      auto content=MakeRuneMatchContract(game,"DM-Hildir",2);
      {RuneBattleLaws::MatchScope selected(RuneBattleLaws::All&~RuneBattleLaws::Kick);
       check(!RuneKick::Compatible(p),"host law overrides local enabled kick");
       check(MakeRuneMatchContract(game,"DM-Hildir",2).gameplayHash!=content.gameplayHash,"authenticated match contract binds laws");
       RuneBattleLaws::Campaign=0;check(RuneBattleLaws::Allowed(RuneBattleLaws::Bash),"campaign preferences cannot veto host rules");}
      RuneBattleLaws::Campaign=original;
      check(!RuneBattleLaws::InMatch,"leaving match restores campaign context");
      std::cout<<"LAWS_NATIVE_GATES_OK players_and_ai contract_bound isolated_campaign\n";
    }
    check(RuneDualWield::Equip(p,off),"authority equips dual pair");
    {RuneBattleLaws::Campaign=RuneBattleLaws::All&~RuneBattleLaws::Cross;RuneDualWield::Guard(p,true);
     check(RuneDualWield::Equipped(p)&&!p->Dual.guard&&!RuneDualWield::Blocks(p,target),"crossed block can be disabled without disabling dual attacks");RuneBattleLaws::Campaign=RuneBattleLaws::All;}
    int rightDamage=0,leftDamage=0;
    for(int i=0;i<4;++i){
        reset(45);tick(1);int before=target->Health();check(action(1,ActionKind::Attack),"attack intent");
        tick(80);int damage=before-target->Health();if(i%2)leftDamage+=damage;else rightDamage+=damage;
        std::cout<<"NET_NATIVE_STRIKE n="<<i<<" damage="<<damage<<" left="<<p->Dual.leftSwings<<" right="<<p->Dual.rightSwings<<std::endl;
    }
    check(rightDamage>0 && leftDamage>0 && p->Dual.leftSwings>=2 && p->Dual.rightSwings>=2,"both weapons damage through authority");
    reset(150);p->Dual.left=true;p->Dual.guard=true;p->Dual.guardBlend=1;
    auto* remote=snapshot();check(remote && RuneDualWield::Equipped(remote) && remote->Dual.left && remote->Dual.guardBlend==1,"replica pair/pose state");
    if(commandline->HasArg("","--rune-combat-playback-probe")) {
        World fixture;check(authority.capture(fixture),"playback fixture world");
        auto own=authority.identity(p);auto* model=UObject::Cast<USkelModel>(GetRuneDisplayMesh(remote));
        auto* proxy=UObject::Cast<UActor>(remote->GetUObject("AnimProxy"));
        uint16_t sequence=0;
        for(size_t i=0;i<model->Sequences.size();++i)
            if(model->Sequences[i].Info.Name==p->Weapon()->GetName("A_AttackStandA"))sequence=uint16_t(i+1);
        check(sequence!=0,"playback standing attack");
        const double rate=double(int16_t(1.2*256))/256;
        auto installFrame=[&](uint64_t hostTick,double phase,uint32_t life=0) {
            auto world=fixture;
            for(auto& e:world)if(e.ref==own) {
                e.bodyAnimation=e.torsoAnimation=sequence;e.flags&=uint8_t(~(8|16));
                e.animationPhase=e.torsoPhase=int16_t(std::lround(phase*32767));
                e.animationRate=e.torsoRate=int16_t(std::lround(rate*256));
                e.combat.guard=0;e.combat.flags&=uint8_t(~2);
                if(life)e.life=life;
            }
            Snapshot s{++snapshotId,0,hostTick,world},decoded;Bytes wire;
            check(encodeSnapshot(s,nullptr,wire)&&decodeSnapshot(wire,nullptr,decoded),"playback wire frame");
            check(replica.install(decoded),"playback installed frame");
        };
        struct Metrics {double rms=0;int holds=0,frames=0,rewinds=0;};
        auto replay=[&](bool smooth,int hz) {
            Metrics result;const double jitter[]={0,.012,-.009,.018,-.004,.008,-.012,.005};
            struct Packet {double at,phase;uint64_t tick;};std::vector<Packet> packets;
            auto firstTick=replica.installedTick()+2;
            for(int n=0;n<18;++n)if(n!=9)packets.push_back({n/30.+jitter[n%8],.05+n/30.*rate,firstTick+uint64_t(n*2)});
            size_t installed=0;double started=game.TotalTime+1,received=started,spacing=1./30,lastPhase=.05;
            installFrame(packets[0].tick,packets[0].phase);
            for(int frame=0;frame<int(hz*.55);++frame) {
                double clock=started+double(frame)/hz;game.TotalTime=clock;
                while(installed+1<packets.size() && packets[installed+1].at<=clock-started) {
                    ++installed;spacing=std::clamp(clock-received,.016,.150);received=clock;
                    installFrame(packets[installed].tick,packets[installed].phase);
                }
                double alpha=std::clamp((clock-received)/spacing,0.,1.);
                replica.present(alpha,smooth?clock:-1);
                const double phase=proxy->AnimFrame();
                auto bones=model->ActorJointMatrices(remote);
                for(auto& bone:bones)for(int k=0;k<16;++k)check(std::isfinite(bone[k]),"playback finite rendered skeleton");
                if(frame>hz*.10) {
                    double delta=phase-lastPhase;
                    if(delta < -2e-5)++result.rewinds;
                    if(smooth && delta < -2e-5)std::cout<<"NET_PLAYBACK_REWIND frame="<<frame<<" hz="<<hz<<" previous="<<lastPhase<<" current="<<phase<<" now="<<clock<<" alpha="<<alpha<<" packet="<<installed<<" snapshot="<<replica.installedId()<<" rate="<<proxy->AnimRate()<<" physics="<<fixture.front().physical.kind<<'\n';
                    if(smooth)check(delta>=-2e-5,"playback rewound a continuous strike");
                    if(smooth)check(delta<=rate*1.25/hz+1e-4,"playback single-frame jump");
                    result.rms+=std::pow(delta*hz-rate,2);result.holds+=delta<1e-5;++result.frames;
                }
                lastPhase=phase;
            }
            result.rms=std::sqrt(result.rms/result.frames);return result;
        };
        for(int hz:{60,120,144,165,240}) {
            auto old=replay(false,hz),smooth=replay(true,hz);
            std::cout<<"NET_PLAYBACK_CADENCE hz="<<hz<<" old_rms="<<old.rms<<" smooth_rms="<<smooth.rms
                <<" old_holds="<<old.holds<<" smooth_holds="<<smooth.holds<<" old_rewinds="<<old.rewinds<<" smooth_rewinds="<<smooth.rewinds<<'\n';
            check(smooth.rms<old.rms*.65,"playback cadence not meaningfully smoother");
        }
        // A repeated clip's new tween starts from the last displayed pose.
        auto source=model->SampleAnimation(proxy);auto hostTick=replica.installedTick()+2;
        installFrame(hostTick,-.025);
        check(proxy->RuneTweenPose.size()==source.size(),"same-clip restart captured tween");
        for(size_t j=0;j<source.size();++j)
            check(length(source[j].Position-proxy->RuneTweenPose[j].Position)<.001f && source[j].Rotation==proxy->RuneTweenPose[j].Rotation,"restart uses displayed source");
        replica.present(0,game.TotalTime+.001);check(proxy->AnimFrame()<0,"windup advanced into unconfirmed strike");
        RuneAnimationPlayback clock;RuneAnimationPlayback::Sample sample{1,1,1,1,1,.2,rate,.9,false};
        clock.present(sample,1,.2);check(clock.present(sample,2,.2)<=.2+rate*.05+1e-8,"animation extrapolation unbounded");
        sample.snapshot++;sample.sequence++;sample.phase=.01;
        check(clock.present(sample,2.001,.01)==.01,"new interruption delayed");
        ++sample.life;sample.snapshot++;sample.phase=.4;
        check(clock.present(sample,2.002,.4)==.4,"new-life playback not reset");
        std::cout<<"NET_COMBAT_PLAYBACK_OK confirmed_clip cadence restart windup life gap skeleton\n";
        return;
    }
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
        for(auto& observed:observations)if(observed.ref==own)observed.combat.ready=3; // Availability must not mask missing-target bugs.
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
            check(!decision.action || (*decision.action!=ActionKind::Kick && *decision.action!=ActionKind::Bash && *decision.action!=ActionKind::Attack && *decision.action!=ActionKind::BloodLeap && *decision.action!=ActionKind::ShieldCharge),"normal bot needs a live visible opponent for combat");
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
        for(uint64_t time=1000;time<=61000;time+=100) {
            auto decision=brain.think(game,replica,nearby,status,own,time);
            if(decision.action==ActionKind::Kick){kicked=true;break;}
        }
        check(kicked,"eligible normal bot still kicks a close visible enemy");
        for(auto mode:{GameMode::Deathmatch,GameMode::TeamDeathmatch,GameMode::Arena,GameMode::HeadBall}){
            auto saved=RuneBattleLaws::Campaign.load();RuneBattleLaws::Campaign=0;
            RuneBot::Brain restricted(717,1);auto rules=status;rules.mode=mode;rules.arenaPhase=1;rules.headBallStarted=true;
            PlayerScore ownPlayer;ownPlayer.player=1;ownPlayer.team=0;ownPlayer.pawn=own;
            PlayerScore foePlayer;foePlayer.player=2;foePlayer.team=1;foePlayer.pawn=enemy;rules.scores={ownPlayer,foePlayer};
            unsigned ordinary=0;
            for(uint64_t time=1000;time<31000;time+=100){auto d=restricted.think(game,replica,nearby,rules,own,time);ordinary+=d.action==ActionKind::Attack;
                check(!d.action||(*d.action!=ActionKind::Kick&&*d.action!=ActionKind::Bash&&*d.action!=ActionKind::BloodLeap&&*d.action!=ActionKind::ShieldCharge),"bot does not select unchecked moves");}
            if(mode==GameMode::Deathmatch)check(ordinary>0,"restricted bot still uses ordinary weapon attacks");
            RuneBattleLaws::Campaign=saved;
        }
        std::cout<<"LAWS_BOT_PLANNER_OK four_modes unchecked_moves_not_selected\n";
        RuneBot::Brain cadence(717,1);unsigned sampledKicks=0,sampledAttacks=0;
        for(uint64_t time=1000;time<61000;time+=100){auto d=cadence.think(game,replica,nearby,status,own,time);sampledKicks+=d.action==ActionKind::Kick;sampledAttacks+=d.action==ActionKind::Attack;}
        check(sampledKicks>0 && sampledKicks<=12 && sampledAttacks>sampledKicks*3,"bot kicks remain occasional rather than replacing ordinary weapon attacks");
        std::cout<<"BOT_KICK_POLICY_SAMPLE seconds=60 stationary_visible_enemy=1 distance=45 kicks="<<sampledKicks<<" attacks="<<sampledAttacks<<" actual_actions_executed=0"<<std::endl;

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
    replica.present(1,double(now+8)/1000);replica.presentAnticipation(now+8);
    check(remote->RuneCombatPreviewGuard>0 && !p->Dual.guard,"crossed block responds on first render frame");
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
    replica.present(1,double(now+8)/1000);replica.presentAnticipation(now+8);
    auto firstPreview=model->ActorJointMatrices(remote);float firstAttackMotion=0;
    for(size_t j=0;j<firstPreview.size();++j)for(int k=0;k<16;++k)firstAttackMotion=std::max(firstAttackMotion,std::abs(firstPreview[j][k]-previewStart[j][k]));
    check(firstAttackMotion>.001f,"attack responds on first render frame with playback enabled");
    replica.present(1,double(now+50)/1000);replica.presentAnticipation(now+50);auto previewMid=model->ActorJointMatrices(remote);float previewMotion=0;
    for(size_t j=0;j<previewMid.size();++j)for(int k=0;k<16;++k)previewMotion=std::max(previewMotion,std::abs(previewMid[j][k]-previewStart[j][k]));
    check(previewMotion>.01f && previewProxy->AnimSequence()==originalSequence && std::abs(previewProxy->AnimFrame()-originalFrame)<.0001f,"preview moves skeleton without changing replicated animation clock");
    check(target->Health()==unchangedHealth && !remote->bFire() && !p->bFire(),"prediction has no attack/damage side effects");
    replica.resolveAnticipation({9002,Admission::ExecutionRejected},now+60);replica.presentAnticipation(now+100);
    check(previewProxy->RuneCombatPreviewWeight>0 && previewProxy->RuneCombatPreviewWeight<1,"rejected preview blends back");
    replica.presentAnticipation(now+141);check(previewProxy->RuneCombatPreviewWeight==0,"rejected preview fully clears");
    previewInput.held=2;
    check(replica.anticipate(authority.identity(p),9003,ActionKind::RaiseShield,previewInput,now+150),"anticipate shield raise");
    auto shieldStart=model->ActorJointMatrices(remote);
    replica.present(1,double(now+158)/1000);replica.presentAnticipation(now+158);
    auto firstShield=model->ActorJointMatrices(remote);float firstBlockMotion=0;
    for(size_t j=0;j<firstShield.size();++j)for(int k=0;k<16;++k)firstBlockMotion=std::max(firstBlockMotion,std::abs(firstShield[j][k]-shieldStart[j][k]));
    check(firstBlockMotion>.001f,"shield block responds on first render frame with playback enabled");
    std::cout<<"NET_FIRST_FRAME_RESPONSE attack_motion="<<firstAttackMotion<<" block_motion="<<firstBlockMotion<<" milliseconds=8\n";
    replica.present(1,double(now+190)/1000);replica.presentAnticipation(now+190);check(previewProxy->RuneCombatPreviewWeight==1,"shield raise visible before host response");
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
    {
        auto* rt=UObject::Cast<UPawn>(replica.actor(authority.identity(target)));
        auto time=game.LevelInfo->TimeSeconds();
        target->RuneBashImpactAt=-1000;
        auto base=reactionModel->ActorPose(target),kicked=base;
        RuneBash::PresentImpact(target,true);game.LevelInfo->TimeSeconds()=time+.045f;
        RuneBash::ApplyPose(*reactionModel,target,kicked);
        float headChange=0;
        for(size_t j=0;j<base.size();++j) {
            check(base[j].Position==kicked[j].Position && base[j].Scale==kicked[j].Scale,"bash recoil retains bone shape");
            auto a=RunePoseRotation(base[j].Rotation),b=RunePoseRotation(kicked[j].Rotation);
            if(reactionModel->Joints[j].Name=="head")for(int n=0;n<16;++n)headChange=std::max(headChange,std::abs(a[n]-b[n]));
            else check(base[j].Rotation==kicked[j].Rotation,"bash recoil isolated to head");
        }
        check(headChange>.3f,"confirmed bash visibly snaps head back");
        auto recovered=base;game.LevelInfo->TimeSeconds()=time+.35f;RuneBash::ApplyPose(*reactionModel,target,recovered);
        for(size_t j=0;j<base.size();++j)check(base[j].Rotation==recovered[j].Rotation,"head recoil finishes promptly");
        game.LevelInfo->TimeSeconds()=time;RuneBash::PresentImpact(rt,true);
        check(rt->RuneBashImpactAt==time && rt->RuneBashImpactStrength==1,"replica accepts cosmetic confirmed-hit reaction");
        game.LevelInfo->TimeSeconds()=time;
        std::cout<<"NET_NATIVE_BASH_HEAD_OK peak="<<headChange<<" duration=.34"<<std::endl;
    }
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
    reset(60);tick(180);p->RuneKickCooldown=0;
    const auto kickStartedAt=now;check(action(1,ActionKind::Kick),"kick timing sample starts");
    for(int frame=0;frame<120 && p->RuneKickPending;++frame)tick(1);
    check(!p->RuneKickPending,"kick timing sample reaches authored contact");
    const auto kickContactAt=now;
    check(kickContactAt-kickStartedAt>=340 && kickContactAt-kickStartedAt<=410,"shared kick animation reaches contact in 350-400ms with fixed-step tolerance");
    for(int frame=0;frame<180 && p->RuneKickActive;++frame)tick(1);
    std::cout<<"ACTUAL_KICK_TIMING contact_ms="<<kickContactAt-kickStartedAt<<" whole_move_ms="<<now-kickStartedAt<<" authored_notify=1 tick_ms=17"<<std::endl;
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
    for(auto gameMode:{GameMode::Deathmatch,GameMode::TeamDeathmatch,GameMode::Arena,GameMode::HeadBall})
    for(int moveMode:{0,1,2})for(bool leap : {false,true}) {
        if(gameMode!=GameMode::Deathmatch && moveMode!=1)continue;
        const bool outside=moveMode!=0;
        reset(leap?450:250);RuneBloodlust::AllowNormal=outside;
        if(moveMode==1){p->SetInt("Strength",0);p->SetBool("bBloodLust",false);check(RuneBloodlust::Readiness(p)&1,"normal combat readiness without rage");}
        else full();
        const int initialMeter=p->GetInt("Strength");
        check(p->GetUObject("Shield")!=nullptr,"both special moves are tested with shield equipped");
        RuneBot::Brain brain(73,1);
        MatchStatus status;status.mode=gameMode;status.phase=MatchPhase::Playing;status.arenaPhase=1;status.headBallStarted=true;
        PlayerScore ownScore;ownScore.player=1;ownScore.team=0;ownScore.pawn=authority.identity(p);
        PlayerScore foeScore;foeScore.player=2;foeScore.team=1;foeScore.pawn=authority.identity(target);status.scores={ownScore,foeScore};
        bool acceptedSpecial=false,sawJump=false;
        for(int i=0;i<20 && !acceptedSpecial;++i) {
            tick(6);snapshot();World observations;check(authority.capture(observations),"bot decision observations");
            // Exclude optional supplies from this focused two-player duel.
            std::erase_if(observations,[](const Entity& e){return e.kind!=EntityKind::Pawn;});
            auto decision=brain.think(game,replica,observations,status,authority.identity(p),now);
            if(!decision.action)continue;
            auto input=decision.input;input.sequence=++actionId;input.createdMs=now;
            AdmittedAction request{{actionId,actionId,now,input.life,*decision.action},input,now};
            const int beforeMoveMeter=p->GetInt("Strength");
            bool admitted=authority.action(1,request,now);
            if(*decision.action==ActionKind::Jump){check(admitted,"bot first jump admitted");sawJump=true;}
            if(*decision.action==(leap?ActionKind::BloodLeap:ActionKind::ShieldCharge)) {
                check(admitted,"bot special met real gesture/equipment contract");
                if(outside)check(p->GetInt("Strength")==beforeMoveMeter,"normal-combat move consumes no meter, including during active rage");
                check(leap?sawJump:bool(input.held&2),"bot special includes required input");acceptedSpecial=true;
            }
        }
        check(acceptedSpecial,leap?"bot never performed rage leap":"bot never performed shield charge");
        std::cout<<"NET_NORMAL_MOVE game_mode="<<int(gameMode)<<" mode="<<moveMode<<" leap="<<leap<<" meter_initial="<<initialMeter<<" meter_after="<<p->GetInt("Strength")<<" cooldown="<<p->Bloodlust.cooldown<<std::endl;
        if(outside)check(p->Bloodlust.cooldown>3.8f && (moveMode!=1 || p->GetInt("Strength")==0),"normal combat retains cooldown and needs no rage");
        tick(180);
    }
    RuneBloodlust::AllowNormal=true;
    for(bool leap:{false,true}){
        reset(leap?450:250);snapshot();World seen;check(authority.capture(seen),"ally corridor observations");
        std::erase_if(seen,[](const Entity& e){return e.kind!=EntityKind::Pawn;});
        auto self=std::find_if(seen.begin(),seen.end(),[&](const Entity& e){return e.ref==authority.identity(p);});check(self!=seen.end(),"ally corridor self");
        Entity ally=*self;ally.ref={900001,1};auto at=p->Location()+direction*300.f;ally.position={int32_t(at.x*8),int32_t(at.y*8),int32_t(at.z*8)};seen.push_back(ally);
        MatchStatus state;state.mode=GameMode::TeamDeathmatch;state.phase=MatchPhase::Playing;
        PlayerScore own;own.player=1;own.team=0;own.pawn=authority.identity(p);
        PlayerScore foe;foe.player=2;foe.team=1;foe.pawn=authority.identity(target);
        PlayerScore friendScore;friendScore.player=3;friendScore.team=0;friendScore.pawn=ally.ref;state.scores={own,foe,friendScore};
        RuneBot::Brain brain(73,1);
        for(uint64_t time=1000;time<4000;time+=100){auto d=brain.think(game,replica,seen,state,own.pawn,time);check(!d.action || (*d.action!=ActionKind::BloodLeap && *d.action!=ActionKind::ShieldCharge),"bot avoids special-move corridor occupied by ally");}
    }
    std::cout<<"BOT_SPECIAL_ALLY_LANE_OK rush_and_leap"<<std::endl;
    RuneBloodlust::AllowNormal=false;reset(300);p->SetInt("Strength",0);p->SetBool("bBloodLust",false);
    check(RuneBloodlust::Readiness(p)==0,"disabling normal mode restores rage requirement");
    std::cout<<"NET_BOT_SPECIALS_OK rage_only_and_normal_combat_empty_or_full_meter double_tap held_block authority_admitted cooldowns\n";
    // A normal-combat rush must physically topple the remote player in every
    // physics mode; the existing authoritative pose carries it to clients.
    const int savedPhysics=RunePhysics::RequestedMode.load();
    RuneDualWield::Enabled=false;RuneBloodlust::AllowNormal=true;
    give(p,"VikingShield",true);tick(60);
    for(int physicsMode:{0,1,2}) {
        RunePhysics::BeforeSave();RunePhysics::Reset();RunePhysics::RequestedMode=physicsMode;tick(1);reset(100);
        check(RuneBloodlust::Charge(p),"zero-rage shield rush starts");
        bool down=false;float away=0;const vec3 start=target->Location();
        for(int frame=0;frame<90 && !down;++frame){tick(1);down=RunePhysics::Controls(target);}
        check(down,"shield rush must knock down its target in every physics mode");
        World physical;check(authority.capture(physical),"capture rushed opponent");
        auto entity=std::find_if(physical.begin(),physical.end(),[&](const Entity& e){return e.ref==authority.identity(target);});
        check(entity!=physical.end() && entity->physical.kind==1,"rush ragdoll is replicated");
        snapshot();check(RunePhysics::Controls(replica.actor(authority.identity(target))),"remote opponent visibly uses physical presentation");
        for(int frame=0;frame<30;++frame){tick(1);away=std::max(away,dot(target->Location()-start,direction));}
        check(away>20,"rush ragdoll travels away from attacker");
        tick(420);check(!RunePhysics::Controls(target) && target->Health()>0,"rushed opponent recovers alive");
        check(RunePhysics::mode()==physicsMode,"rush does not change saved ambient physics mode");
        std::cout<<"NET_RUSH_KNOCKDOWN_OK mode="<<physicsMode<<" backwards="<<away<<" replicated=1 recovered=1"<<std::endl;
    }
    RunePhysics::BeforeSave();RunePhysics::Reset();RunePhysics::RequestedMode=0;tick(1);
    give(target,"VikingShield",true);tick(60);reset(100);
    check(action(2,ActionKind::RaiseShield),"rush target raises shield");
    check(RuneKick::ShieldBlocks(target,p),"rush target really blocks toward attacker");
    const int guardedHealth=target->Health();
    check(RuneBloodlust::Charge(p),"rush against raised shield starts");
    for(int frame=0;frame<90 && !RunePhysics::Controls(target);++frame)tick(1);
    check(RunePhysics::Controls(target),"blocking reduces damage but still topples rush target");
    check(guardedHealth-target->Health()<openDamage,"shield retains reduced rush damage with original game damage multipliers");
    tick(420);check(!RunePhysics::Controls(target) && target->Health()>0,"blocking rush target recovers");
    std::cout<<"NET_RUSH_BLOCK_KNOCKDOWN_OK reduced_damage=1 recovered=1"<<std::endl;
    RunePhysics::BeforeSave();RunePhysics::Reset();RunePhysics::RequestedMode=savedPhysics;tick(1);
    RuneBloodlust::AllowNormal=false;RuneDualWield::Enabled=true;
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
