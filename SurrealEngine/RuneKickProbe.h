#pragma once
#include "RuneKick.h"
#include "Engine.h"
#include "Package/Package.h"
#include "Package/PackageManager.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include "VM/ScriptCall.h"
#include "VM/Frame.h"
#include <sstream>

// Isolated, opt-in real-time fixture. Player kicks/blocks come from real UI.
class RuneKickProbe {
public:
 bool Active()const{return !mode.empty()&&!complete;}
 RuneKickProbe():mode(commandline->GetArg("","--probe-kick","")){}
 void Tick(Engine& game,int frame){
  if(!Active()||frame<180)return;
  auto* player=game.viewport->Actor();
  bool mob=mode.find("mob-")==0,blocked=mode.find("blocked")!=std::string::npos && mode.find("unblocked")==std::string::npos;
  if(!enemy){
   if(!commandline->HasArg("","--save-directory"))Exception::Throw("Kick probe requires isolated saves");
   vec3 start=player->Location(),forward;bool found=false;
   for(int angle=0;angle<16&&!found;angle++){
    float rad=angle*3.14159265f/8;forward={std::cos(rad),std::sin(rad),0};
    auto end=start+forward*200.0f;auto nearValue=start+forward*75.0f;
    found=player->CheckLocation(nearValue,22,42,true).first && player->CheckLocation(end,22,42,true).first && player->FastTrace(end,start);
   }
   if(!found)Exception::Throw("No clear kick fixture lane");
   auto rotation=Rotator::FromVector(forward);player->SetRotation(rotation);player->ViewRotation()=rotation;
   enemy=UObject::Cast<UPawn>(player->Spawn(game.packages->GetPackage("RuneI")->GetClass("DarkViking"),{},{},start+forward*75.0f,Rotator(0,rotation.Yaw+32768,0)));
   if(!enemy)Exception::Throw("Kick enemy spawn failed");
   enemy->Enemy()=player;enemy->GotoState("Fighting","Begin");enemy->SetPhysics(PHYS_Walking);
   for(const char* event:{"Tick","Timer","SeePlayer","HearNoise","EnemyNotVisible"})enemy->DisableEvent(event);
   if(enemy->StateFrame)enemy->StateFrame->SetLatentState(LatentRunState::Stop);
   enemy->RuneKickThink=10000;enemy->RuneBashThink=10000;enemy->Acceleration()=vec3(0);enemy->Velocity()=vec3(0);
   CallEvent(enemy,"WeaponDeactivate");enemy->SetBool("bSwingingHigh",false);enemy->SetBool("bSwingingLow",false);
   if(auto* w=enemy->GetUObject("Weapon")){CallEvent(w,"FinishAttack");CallEvent(w,"DisableSwipeTrail");}
   enemy->LoopAnim("neutral_idle",1,0,0);
   CallEvent(enemy,"ActivateShield",{ExpressionValue::BoolValue(false)});
   attacker=mob?static_cast<UPawn*>(enemy):static_cast<UPawn*>(player);victim=mob?static_cast<UPawn*>(player):enemy;
   if(blocked){
    auto* shield=victim->Spawn(game.packages->GetPackage("RuneI")->GetClass("VikingShield"),{},{},victim->Location()+vec3(0,0,100),Rotator(0,0,0));
    auto* copy=CallEvent(shield,"SpawnCopy",{ExpressionValue::ObjectValue(victim)}).ToObject();
    CallEvent(victim,"AcquireInventory",{ExpressionValue::ObjectValue(copy)});copy->GotoState("Idle","Begin");
    if(!victim->GetUObject("Shield"))Exception::Throw("Kick test shield missing");
    CallEvent(victim,"ActivateShield",{ExpressionValue::BoolValue(true)});
    if(!RuneKick::ShieldBlocks(victim,attacker))Exception::Throw("Facing raised shield did not block");
    auto facing=victim->Rotation();victim->Rotation().Yaw+=32768;
    if(RuneKick::ShieldBlocks(victim,attacker))Exception::Throw("Shield blocked a rear kick");
    victim->Rotation()=facing;CallEvent(victim,"ActivateShield",{ExpressionValue::BoolValue(false)});
    if(RuneKick::ShieldBlocks(victim,attacker))Exception::Throw("Lowered shield blocked a kick");
    if(!mob){CallEvent(victim,"ActivateShield",{ExpressionValue::BoolValue(true)});CallEvent(victim,"PlayBlockLow",{ExpressionValue::FloatValue(.1f)});}
   }
   bool enabled=RuneKick::Enabled.exchange(false);
   if(RuneKick::Start(attacker))Exception::Throw("Disabled kicks still start");
   RuneKick::Enabled.store(enabled);
   origin=victim->Location();health=victim->Health();ready=frame;
   LogMessage("RUNE_KICK_PROBE_READY mode="+mode+" compatible="+std::to_string(RuneKick::Compatible(attacker)));
  }
  if(!enemy->RuneKickActive && enemy->StateFrame)enemy->StateFrame->SetLatentState(LatentRunState::Stop);
  if(mob && !started && frame>ready+60 && (!blocked||RuneKick::ShieldBlocks(victim,attacker))){started=RuneKick::Start(attacker);if(started)LogMessage("RUNE_KICK_PROBE_MOB_STARTED");}
  if(attacker->RuneKickActive && !seen){seen=true;startTime=game.LevelInfo->TimeSeconds();
   if(RuneKick::Start(attacker))Exception::Throw("Kick cooldown allowed duplicate start");
   if(!mob && !CallEvent(enemy,"InDangerFromAttack").ToBool())Exception::Throw("Enemy did not recognize kick threat");}
  if(seen){maxDistance=std::max(maxDistance,length((victim->Location()-origin).xy()));
   if(game.LevelInfo->TimeSeconds()-startTime>1.6f){
    if(attacker->RuneKickHits!=1 || attacker->RuneKickBlocks!=(blocked?1:0))Exception::Throw("Kick expected one correctly blocked/unblocked impact");
    if(blocked && (victim->Health()!=health || maxDistance>12 || maxDistance<.5f))Exception::Throw("Shield kick damage/recoil mismatch");
    if(!blocked && (victim->Health()>=health || maxDistance<15))Exception::Throw("Unblocked kick did not damage/push");
    if(attacker->GetStateName()=="Uninterrupted")Exception::Throw("Kick did not release animation state");
    std::ostringstream s;s<<"RUNE_KICK_PROBE_OK mode="<<mode<<" hits="<<attacker->RuneKickHits<<" blocks="<<attacker->RuneKickBlocks<<" health_before="<<health<<" health_after="<<victim->Health()<<" max_distance="<<maxDistance<<" cooldown="<<attacker->RuneKickCooldown<<" state="<<attacker->GetStateName().ToString();LogMessage(s.str());complete=true;
   }
  }
 }
private:
 std::string mode;UPawn *enemy=nullptr,*attacker=nullptr,*victim=nullptr;vec3 origin;int health=0,ready=0;float startTime=0,maxDistance=0;bool started=false,seen=false,complete=false;
};
