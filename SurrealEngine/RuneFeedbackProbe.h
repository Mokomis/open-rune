#pragma once
#include "RuneFeedback.h"
#include "Engine.h"
#include "Package/Package.h"
#include "Package/PackageManager.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include "VM/ScriptCall.h"
#include "VM/Frame.h"

// Opt-in script/physics integration fixture, never a campaign save.
class RuneFeedbackProbe {
    bool enabled=commandline->HasArg("","--probe-feedback");
    UPawn* enemy=nullptr;
    UActor* shield=nullptr;
    int stage=0;
public:
    void Tick(Engine& game,int frame) {
        if(!enabled||frame<240||frame%120)return;
        if(!commandline->HasArg("","--save-directory"))Exception::Throw("Feedback probe requires isolated saves");
        auto* player=game.viewport->Actor();
        auto damage=[&](UActor* victim,UPawn* attacker,int amount) {
            CallEvent(victim,"JointDamaged",{ExpressionValue::IntValue(amount),ExpressionValue::ObjectValue(attacker),ExpressionValue::VectorValue(victim->Location()),ExpressionValue::VectorValue(vec3(0)),ExpressionValue::NameValue("blunt"),ExpressionValue::IntValue(0)});
        };
        switch(stage++) {
        case 0: {
            vec3 spawn;bool found=false;
            for(int angle=0;angle<16&&!found;angle++) {
                float rad=angle*3.14159265f/8;spawn=player->Location()+vec3(std::cos(rad),std::sin(rad),0)*100.0f;
                found=player->CheckLocation(spawn,22,42,true).first&&player->FastTrace(spawn,player->Location());
            }
            if(!found)Exception::Throw("No feedback fixture spawn lane");
            enemy=UObject::TryCast<UPawn>(player->Spawn(game.packages->GetPackage("RuneI")->GetClass("DarkViking"),{},{},spawn,Rotator(0,0,0)));
            if(!enemy)Exception::Throw("Feedback probe enemy spawn failed");
            for(const char* event:{"Tick","Timer","SeePlayer","HearNoise","EnemyNotVisible"})enemy->DisableEvent(event);
            if(enemy->StateFrame)enemy->StateFrame->SetLatentState(LatentRunState::Stop);
            enemy->RuneKickThink=enemy->RuneBashThink=10000;enemy->Health()=1000;player->Health()=1000;
            auto* item=player->Spawn(game.packages->GetPackage("RuneI")->GetClass("VikingShield"),{},{},player->Location()+vec3(0,0,100),Rotator(0,0,0));
            if(!item)Exception::Throw("Feedback probe shield spawn failed");
            auto* copy=CallEvent(item,"SpawnCopy",{ExpressionValue::ObjectValue(player)}).ToObject();
            CallEvent(player,"AcquireInventory",{ExpressionValue::ObjectValue(copy)});
            shield=UObject::TryCast<UActor>(player->GetUObject("Shield"));
            if(!shield)Exception::Throw("Feedback probe shield acquisition failed");
            LogMessage("RUNE_FEEDBACK_PROBE pickup");break;
        }
        case 1: {
            int before=player->Health();damage(player,enemy,20);
            if(player->Health()>=before)Exception::Throw("Feedback damage fixture did not damage player");
            LogMessage("RUNE_FEEDBACK_PROBE damage");break;
        }
        case 2: damage(enemy,player,30);LogMessage("RUNE_FEEDBACK_PROBE weapon");break;
        case 3:
            CallEvent(player,"ActivateShield",{ExpressionValue::BoolValue(true)});
            damage(shield,enemy,15);LogMessage("RUNE_FEEDBACK_PROBE block");break;
        case 4:
            // No local participant, then zero incoming damage: neither rumbles.
            damage(enemy,enemy,10);damage(player,enemy,0);
            LogMessage("RUNE_FEEDBACK_PROBE silent");break;
        case 5:
            if(!player->SetLocation(player->Location()+vec3(0,0,96)))Exception::Throw("No feedback landing clearance");
            player->SetPhysics(PHYS_Falling);player->Velocity()=vec3(0,0,-950);
            LogMessage("RUNE_FEEDBACK_PROBE falling");break;
        case 7: LogMessage("RUNE_FEEDBACK_PROBE_OK");enabled=false;break;
        }
    }
};
