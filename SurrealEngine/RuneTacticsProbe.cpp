#include "Precomp.h"
#include "RuneTactics.h"
#include "RuneCharacters.h"
#include "RuneCharacterPreview.h"
#include "RuneKick.h"
#include "RuneBash.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Package/Package.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "VM/ScriptCall.h"
#include "VM/Frame.h"
#include "Utils/CommandLine.h"
#include <iostream>
#include <stdexcept>
#include <set>

void RunRuneDualWieldProbe(Engine& game);
void RunRuneBloodlustProbe(Engine& game);
void RunRuneTacticsProbe(Engine& game) {
    if(commandline->GetArg("","--rune-tactics-probe","")=="previews") { RuneCharacterPreview::Probe(game);return; }
    if(commandline->GetArg("","--rune-tactics-probe","")=="characters") { RuneCharacters::Probe(game);return; }
    if(commandline->GetArg("","--rune-tactics-probe","")=="bloodlust") { RunRuneBloodlustProbe(game);return; }
    RuneBloodlust::Enabled=false; // Separate ordinary-combat regression.
    if(commandline->GetArg("","--rune-tactics-probe","")=="dual") { RunRuneDualWieldProbe(game);return; }
    RuneDualWield::Enabled=false; // Baseline original single-weapon AI regression.
    auto check=[](bool value,const char* why) { if(!value)throw std::runtime_error(std::string("tactics fixture: ")+why); };
    auto url=game.GetDefaultURL("DM-Hildir.run");url.AddOrReplaceOption("game=RuneI.RuneSinglePlayer");url.AddOrReplaceOption("Difficulty=1");
    game.LoadMap(url);game.LoginPlayer();
    auto* player=game.viewport->Actor();player->bShowMenu()=false;
    auto tick=[&](int frames) {
        for(int i=0;i<frames;++i) {
            constexpr float dt=1.f/120;game.LevelInfo->TimeSeconds()+=dt;game.TotalTime+=dt;game.Level->Tick(dt,false);
        }
    };
    tick(60);
    auto origin=player->Location();vec3 forward;bool found=false;
    for(int i=0;i<16&&!found;++i) {
        float angle=i*3.14159265f/8;forward={std::cos(angle),std::sin(angle),0};
        auto end=origin+forward*200.f;
        found=player->CheckLocation(end,22,42,true).first && player->FastTrace(end,origin);
    }
    check(found,"no clear fixture lane");
    auto mode=commandline->GetArg("","--rune-tactics-probe","DarkViking");
    auto* enemy=UObject::TryCast<UPawn>(player->Spawn(game.packages->GetPackage("RuneI")->GetClass(mode),{},{},origin+forward*150.f,Rotator::FromVector(-forward)));
    check(enemy,"spawn failed");
    tick(60);
    auto shield=[&](UPawn* p) {
        if(!p->GetUObject("Shield")) {
            auto* item=p->Spawn(game.packages->GetPackage("RuneI")->GetClass("VikingShield"),{},{},p->Location()+vec3(0,0,100),{});
            auto* copy=CallEvent(item,"SpawnCopy",{ExpressionValue::ObjectValue(p)}).ToObject();
            CallEvent(p,"AcquireInventory",{ExpressionValue::ObjectValue(copy)});copy->GotoState("Idle","Begin");
        }
    };
    auto reset=[&](float distance) {
        RuneTactics::Enabled=false;tick(1);
        enemy->Enemy()=player;enemy->SetPhysics(PHYS_Walking);player->SetPhysics(PHYS_Walking);
        player->GotoState("PlayerWalking","Begin");
        if(auto* proxy=player->GetUObject("AnimProxy"))proxy->GotoState("Idle","Begin");
        enemy->SetLocation(origin+forward*distance);player->SetLocation(origin);
        enemy->SetRotation(Rotator::FromVector(-forward));player->SetRotation(Rotator::FromVector(forward));player->ViewRotation()=player->Rotation();
        enemy->Health()=player->Health()=10000;
        for(auto* p:{static_cast<UPawn*>(player),enemy}) {
            auto* parts=static_cast<int*>(p->GetProperty("BodyPartHealth"));for(int i=0;i<15;++i)parts[i]=10000;
            p->Acceleration()=p->Velocity()=vec3(0);p->SetBool("bSwingingHigh",false);p->SetBool("bSwingingLow",false);
            CallEvent(p,"WeaponDeactivate");CallEvent(p,"ActivateShield",{ExpressionValue::BoolValue(false)});
        }
        enemy->Tactics={};enemy->RuneKickActive=enemy->RuneKickPending=enemy->RuneBashPending=false;
        enemy->RuneBashPhase=0;enemy->RuneKickCooldown=enemy->RuneBashCooldown=0;
        enemy->GotoState("Fighting","Begin");CallEvent(enemy,"PlayWaiting",{ExpressionValue::FloatValue(.1f)});
        RuneTactics::Enabled=true;
    };
    reset(130);
    check(RuneTactics::Eligible(enemy),"expected eligible humanoid");
    check(!RuneTactics::Eligible(player),"player eligible");
    std::set<std::string> attackAnimations;
    for(int i=0;i<1200;++i) {
        tick(1);
        if(enemy->Tactics.attacking)attackAnimations.insert(enemy->AnimSequence().ToString());
    }
    std::cout<<"TACTICS_VARIANTS class="<<mode<<" count="<<attackAnimations.size()<<'\n';
    if(mode=="DarkViking" || mode=="Dwarf")check(attackAnimations.size()>1,"fighter repeated only one authored attack");
    std::cout<<"TACTICS_ENCOUNTER class="<<mode<<" decisions="<<enemy->Tactics.decisions<<" attacks="<<enemy->Tactics.attacks
             <<" moves="<<enemy->Tactics.moves<<" distance="<<length(enemy->Location()-player->Location())<<" reach="<<enemy->MeleeRange()<<" player_health="<<player->Health()<<" state="<<enemy->GetStateName().ToString()<<'\n';
    check(enemy->Tactics.decisions>10,"controller did not acquire fighting state");
    check(enemy->Tactics.attacks>0,"no natural weapon attack");
    check(player->Health()<10000,"authored attacks did not damage target");
    if(RuneKick::Compatible(enemy)) {
        reset(55);int before=enemy->RuneKickHits;tick(300);
        std::cout<<"TACTICS_KICK choices="<<enemy->Tactics.kicks<<" hits="<<enemy->RuneKickHits-before<<'\n';
        check(enemy->Tactics.kicks>0 && enemy->RuneKickHits>before,"crowding did not produce natural kick impact");
    }
    if(RuneBash::Compatible(enemy)) {
        reset(75);shield(enemy);shield(player);
        // Original player input would lower the shield on the next tick. Freeze
        // only the fixture victim; animations and the actual shield still tick.
        player->DisableEvent("PlayerTick");
        CallEvent(player,"ActivateShield",{ExpressionValue::BoolValue(true)});
        std::cout<<"BASH_READY active="<<RuneKick::ShieldBlocks(player,enemy)<<" enemy_shield="<<(enemy->GetUObject("Shield")!=nullptr)<<" player_state="<<player->GetStateName().ToString()<<" range="<<enemy->MeleeRange()<<std::endl;
        int before=enemy->RuneBashHits,blocks=enemy->RuneBashBlocks;tick(480);
        std::cout<<"TACTICS_BASH choices="<<enemy->Tactics.bashes<<" hits="<<enemy->RuneBashHits-before<<" blocked="<<enemy->RuneBashBlocks-blocks<<" target_block="<<RuneKick::ShieldBlocks(player,enemy)<<" attacks="<<enemy->Tactics.attacks<<" distance="<<length(enemy->Location()-player->Location())<<" visible="<<enemy->Tactics.visibleFor<<" state="<<enemy->GetStateName().ToString()<<" phase="<<enemy->RuneBashPhase<<" pending="<<enemy->RuneBashPending<<" anim="<<enemy->AnimSequence().ToString()<<" frame="<<enemy->AnimFrame()<<" rate="<<enemy->AnimRate()<<" age="<<enemy->RuneBashAge<<" blocking_for="<<enemy->Tactics.blockingFor<<'\n';
        check(enemy->Tactics.bashes>0 && enemy->RuneBashHits>before,"held shield did not produce natural bash");
        check(enemy->RuneBashBlocks>blocks,"shield did not absorb tactical bash");
        player->EnableEvent("PlayerTick");
    }
    if(mode=="DarkViking") {
        reset(85);shield(enemy);tick(180);reset(85);tick(1);
        // This decision-only check has no physics step; anchor the actor after
        // the placement's first walking tick selected Falling to settle to ground.
        enemy->SetPhysics(PHYS_Walking);
        enemy->Tactics.recovery=.35f;
        // Controlled visible attack cue. The decision uses the same animation
        // flags as natural swings; no player input is supplied to the AI.
        player->DisableEvent("PlayerTick");
        float firstBlock=-1;
        for(int i=0;i<100;++i) {
            player->SetBool("bSwingingHigh",true);
            if(enemy->Tactics.sawThreat)enemy->Tactics.defenseRoll=0; // Exercise success path deterministically.
            RuneTactics::Tick(enemy,1.f/120);
            if(enemy->Tactics.blocks && firstBlock<0)firstBlock=(i+1)/120.f;
        }
        std::cout<<"TACTICS_DEFENSE first_block_seconds="<<firstBlock<<" visible="<<enemy->Tactics.visibleFor<<" threat="<<enemy->Tactics.threatFor<<" active="<<enemy->Tactics.active<<" attacks="<<enemy->Tactics.attacks<<'\n';
        check(firstBlock>=.3f && firstBlock<.8f,"defense ignored reaction delay or never blocked");
        player->SetBool("bSwingingHigh",false);player->EnableEvent("PlayerTick");
    }
    reset(85);tick(2);
    auto role=enemy->Role();enemy->Role()=ROLE_SimulatedProxy;
    check(!RuneTactics::Eligible(enemy),"replica eligible");enemy->Role()=role;
    enemy->NetworkReplica=true;check(!RuneTactics::Eligible(enemy),"network replica eligible");enemy->NetworkReplica=false;
    enemy->SetBool("bIsBoss",true);tick(1);check(!RuneTactics::Controls(enemy),"boss taken over");enemy->SetBool("bIsBoss",false);
    enemy->GotoState("Waiting","Begin");tick(1);check(!RuneTactics::Controls(enemy),"waiting script taken over");
    if(mode=="DarkViking" || mode=="Dwarf") {
        reset(85);tick(120);auto* weapon=enemy->GetUObject("Weapon");
        enemy->SetObject("Weapon",nullptr);tick(360);
        check(!enemy->Tactics.active,"disarmed fighter did not return to original equipment behavior");
        enemy->SetObject("Weapon",weapon);
        std::cout<<"TACTICS_DISARM_OK\n";
    }
    if(mode=="DarkViking") {
        reset(85);
        for(int i=0;i<360 && !enemy->Tactics.active;++i)tick(1);
        check(enemy->Tactics.active,"knockback fixture failed to acquire tactical control");
        RuneKick::CommitImpact(player,enemy,false);
        auto impulse=enemy->Velocity();
        check(length(impulse.xy())>100 && enemy->Physics()==PHYS_Falling,"kick did not apply its launch impulse");
        RuneTactics::Tick(enemy,1.f/120);
        check(!enemy->Tactics.active && length(enemy->Velocity()-impulse)<.01f,"tactical handoff canceled kick momentum");
        std::cout<<"TACTICS_KNOCKBACK_OK horizontal_velocity="<<length(impulse.xy())<<'\n';
    }
    reset(85);tick(90);RuneTactics::Enabled=false;tick(360);check(!enemy->Tactics.active,"toggle did not release original behavior");
    RuneTactics::Enabled=true;
    if(mode=="DarkViking") {
        reset(85);
        for(int i=0;i<600 && !enemy->Tactics.attacking;++i)tick(1);
        check(enemy->Tactics.attacking,"save fixture did not reach committed attack");
        auto savedName=enemy->Name;int savedHealth=player->Health();
        game.SaveGameToSlot(94,"Tactical AI isolated fixture");
        game.LoadFromSaveFile(UnrealURL("?load=94"));game.PossessSavedPlayer();
        player=game.viewport->Actor();enemy=nullptr;
        for(auto* actor:game.Level->Actors)if(actor && actor->Name==savedName)enemy=UObject::TryCast<UPawn>(actor);
        check(enemy && player->Health()==savedHealth,"save restoration lost actor or player health");
        check(!enemy->Tactics.active && !enemy->Tactics.attacking,"native decisions unexpectedly persisted");
        tick(1200);
        std::cout<<"TACTICS_SAVE restored_attacks="<<enemy->Tactics.attacks<<" state="<<enemy->GetStateName().ToString()<<'\n';
        check(enemy->Tactics.attacks>0,"saved combat never resumed tactical decisions");
    }
    if(mode=="DarkViking") {
        std::vector<UPawn*> group{enemy};
        for(int i=0;i<24 && group.size()<3;++i) {
            float angle=i*6.2831853f/24;
            vec3 direction(std::cos(angle),std::sin(angle),0);
            auto pos=player->Location()+direction*145.f;
            bool clear=player->CheckLocation(pos,22,42,true).first && player->FastTrace(pos,player->Location());
            for(auto* member:group)if(length(pos-member->Location())<85)clear=false;
            if(!clear)continue;
            auto* member=UObject::TryCast<UPawn>(player->Spawn(game.packages->GetPackage("RuneI")->GetClass("DarkViking"),{},{},pos,Rotator::FromVector(-direction)));
            if(member)group.push_back(member);
        }
        check(group.size()==3,"group fixture could not place three enemies");
        tick(90);
        for(auto* member:group) { member->Enemy()=player;member->GotoState("Fighting","Begin"); }
        int maximum=0;
        for(int i=0;i<2400;++i) {
            player->Health()=10000;
            auto* parts=static_cast<int*>(player->GetProperty("BodyPartHealth"));for(int j=0;j<15;++j)parts[j]=10000;
            tick(1);int attackers=0;
            for(auto* member:group)attackers+=member->Tactics.attacking || member->RuneKickActive || member->RuneBashPhase==1;
            maximum=std::max(maximum,attackers);
        }
        int participating=0,totalAttacks=0;
        for(auto* member:group) {
            participating+=member->Tactics.attacks>0;totalAttacks+=member->Tactics.attacks;
            std::cout<<"TACTICS_GROUP_MEMBER name="<<member->Name.ToString()<<" attacks="<<member->Tactics.attacks<<" decisions="<<member->Tactics.decisions<<" moves="<<member->Tactics.moves<<" visible="<<member->Tactics.visibleFor<<" can_see="<<member->CanSee(player)<<" reach="<<member->MeleeRange()<<" xyz="<<member->Location().x<<","<<member->Location().y<<","<<member->Location().z<<" state="<<member->GetStateName().ToString()<<" distance="<<length(member->Location()-player->Location())<<'\n';
        }
        std::cout<<"TACTICS_GROUP participants="<<participating<<" attacks="<<totalAttacks<<" max_committed="<<maximum<<'\n';
        check(participating==3,"one group member never attacked");
        check(maximum<=2,"group exceeded two committed attackers");
        group[0]->Tactics.attacking=true;group[1]->RuneKickPending=true;
        group[2]->SetPhysics(PHYS_Walking);
        check(!RuneTactics::MayCommit(group[2]),"bloodlust could bypass occupied group attack slots");
        group[0]->Tactics.attacking=false;group[1]->RuneKickPending=false;
        std::cout<<"TACTICS_SPECIAL_GROUP_OK\n";
    }
    std::cout<<"TACTICS_RUNTIME_OK class="<<mode<<" authority, original toggle, boss and scripted-state guards\n";
}
