#pragma once
#include "RuneKick.h"
#include "RuneBash.h"
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
#include <fstream>
#include "RenderDevice/RenderDevice.h"
#include "GameWindow.h"

// Isolated fixture: actual double-tap Block starts player bashes.
class RuneBashProbe {
public:
 bool Active()const{return !mode.empty()&&!complete;}
 RuneBashProbe():mode(commandline->GetArg("","--probe-bash","")){}
 void Tick(Engine& game,int frame){
  if(!Active()||frame<180)return;
  auto* player=game.viewport->Actor();
  bool mob=mode=="ai" || mode.find("mob-")==0,blocked=mode.find("blocked")!=std::string::npos && mode.find("unblocked")==std::string::npos;
  if(!enemy){
   if(!commandline->HasArg("","--save-directory"))Exception::Throw("Bash probe requires isolated saves");
   vec3 start=player->Location(),forward;bool found=false;
   for(int angle=0;angle<16&&!found;angle++){
    float rad=angle*3.14159265f/8;forward={std::cos(rad),std::sin(rad),0};
    auto end=start+forward*200.0f;auto nearValue=start+forward*75.0f;
    found=player->CheckLocation(nearValue,22,42,true).first && player->CheckLocation(end,22,42,true).first && player->FastTrace(end,start);
   }
   if(!found)Exception::Throw("No clear bash fixture lane");
   auto rotation=Rotator::FromVector(forward);player->SetRotation(rotation);player->ViewRotation()=rotation;
   enemy=UObject::Cast<UPawn>(player->Spawn(game.packages->GetPackage("RuneI")->GetClass("DarkViking"),{},{},start+forward*75.0f,Rotator(0,rotation.Yaw+32768,0)));
   if(!enemy)Exception::Throw("Bash enemy spawn failed");
   enemy->Enemy()=player;enemy->GotoState("Fighting","Begin");enemy->SetPhysics(PHYS_Walking);
   for(const char* event:{"Tick","Timer","SeePlayer","HearNoise","EnemyNotVisible"})enemy->DisableEvent(event);
   if(enemy->StateFrame)enemy->StateFrame->SetLatentState(LatentRunState::Stop);
   enemy->RuneKickThink=10000;enemy->RuneBashThink=10000;enemy->Acceleration()=vec3(0);enemy->Velocity()=vec3(0);
   CallEvent(enemy,"WeaponDeactivate");enemy->SetBool("bSwingingHigh",false);enemy->SetBool("bSwingingLow",false);
   if(auto* w=enemy->GetUObject("Weapon")){CallEvent(w,"FinishAttack");CallEvent(w,"DisableSwipeTrail");}
   enemy->LoopAnim("neutral_idle",1,0,0);
   CallEvent(enemy,"ActivateShield",{ExpressionValue::BoolValue(false)});
   attacker=mob?static_cast<UPawn*>(enemy):static_cast<UPawn*>(player);victim=mob?static_cast<UPawn*>(player):enemy;
   auto grantShield=[&](UPawn* pawn) {
    auto* shield=pawn->Spawn(game.packages->GetPackage("RuneI")->GetClass("VikingShield"),{},{},pawn->Location()+vec3(0,0,100),Rotator(0,0,0));
    auto* copy=CallEvent(shield,"SpawnCopy",{ExpressionValue::ObjectValue(pawn)}).ToObject();
    CallEvent(pawn,"AcquireInventory",{ExpressionValue::ObjectValue(copy)});copy->GotoState("Idle","Begin");
   };
   auto* held=attacker->GetUObject("Shield");attacker->SetObject("Shield",nullptr);
   if(RuneBash::Start(attacker))Exception::Throw("Bash started without shield");
   attacker->SetObject("Shield",held);if(!held)grantShield(attacker);
   if(mode=="player-resistant")victim->SetBool("bIsBoss",true);
   if(mode=="player-heavy")victim->Mass()=350;
   if(mode=="player-immune")victim->RuneBashImmunity=100;
   if(blocked){
    auto* shield=victim->Spawn(game.packages->GetPackage("RuneI")->GetClass("VikingShield"),{},{},victim->Location()+vec3(0,0,100),Rotator(0,0,0));
    auto* copy=CallEvent(shield,"SpawnCopy",{ExpressionValue::ObjectValue(victim)}).ToObject();
    CallEvent(victim,"AcquireInventory",{ExpressionValue::ObjectValue(copy)});copy->GotoState("Idle","Begin");
    if(!victim->GetUObject("Shield"))Exception::Throw("Bash test shield missing");
    CallEvent(victim,"ActivateShield",{ExpressionValue::BoolValue(true)});
    if(!RuneKick::ShieldBlocks(victim,attacker))Exception::Throw("Facing raised shield did not block");
    auto facing=victim->Rotation();victim->Rotation().Yaw+=32768;
    if(RuneKick::ShieldBlocks(victim,attacker))Exception::Throw("Shield blocked a rear kick");
    victim->Rotation()=facing;CallEvent(victim,"ActivateShield",{ExpressionValue::BoolValue(false)});
    if(RuneKick::ShieldBlocks(victim,attacker))Exception::Throw("Lowered shield blocked a kick");
    if(!mob){CallEvent(victim,"ActivateShield",{ExpressionValue::BoolValue(true)});CallEvent(victim,"PlayBlockLow",{ExpressionValue::FloatValue(.1f)});}
   }
   bool enabled=RuneBash::Enabled.exchange(false);
   if(RuneBash::Start(attacker))Exception::Throw("Disabled bashes still start");
   RuneBash::Enabled.store(enabled);
   if(mode=="ai") {
    for(const char* event:{"Tick","Timer","SeePlayer","HearNoise","EnemyNotVisible"})enemy->EnableEvent(event);
    enemy->RuneKickThink=1;enemy->RuneBashThink=.7f;enemy->GotoState("Fighting","Begin");
   }
   origin=victim->Location();health=victim->Health();ready=frame;
   LogMessage("RUNE_BASH_PROBE_READY mode="+mode+" compatible="+std::to_string(RuneBash::Compatible(attacker)));
  }
  if(mode=="ai") {
   player->Health()=1000;enemy->Health()=1000;
   if(frame>2400) { if(enemy->RuneBashHits<1)Exception::Throw("AI did not land a bash");LogMessage("RUNE_BASH_AI_OK hits="+std::to_string(enemy->RuneBashHits));complete=true; }
   return;
  }
  if(!enemy->RuneBashPhase && enemy->StateFrame)enemy->StateFrame->SetLatentState(LatentRunState::Stop);
  if(mob && !started && frame>ready+60 && (!blocked||RuneKick::ShieldBlocks(victim,attacker))){started=RuneBash::Start(attacker);if(started)LogMessage("RUNE_BASH_PROBE_MOB_STARTED");}
  if(!mob && !seen && player->RuneBashTapTime>=0 && RuneKick::ShieldBlocks(player,enemy) && !firstBlock) { firstBlock=true;LogMessage("RUNE_BASH_FIRST_BLOCK_OK"); }
  if(attacker->RuneBashPhase==1 && !seen){seen=true;startTime=game.LevelInfo->TimeSeconds();
   if(RuneBash::Start(attacker))Exception::Throw("Bash cooldown allowed duplicate start");
   if(!mob && !CallEvent(enemy,"InDangerFromAttack").ToBool())Exception::Throw("Enemy did not recognize bash threat");}
  if(victim->RuneBashPhase==2)stunTime=std::max(stunTime,victim->RuneBashAge);
  auto prefix=commandline->GetArg("","--probe-bash-captures","");
  if(!prefix.empty()) {
   int stage=attacker->RuneBashPhase==1 && attacker->AnimFrame()>.10f && attacker->AnimFrame()<.25f?0:
     victim->RuneBashImpactStrength>0 && game.LevelInfo->TimeSeconds()-victim->RuneBashImpactAt>.045f && game.LevelInfo->TimeSeconds()-victim->RuneBashImpactAt<.1f?1:-1;
   if(stage>=0 && !(captures&(1<<stage))) {
    auto* d=game.window->GetRenderDevice();int w=d->GetRenderWidth(),h=d->GetRenderHeight();Array<TextureColor> pixels;pixels.resize(size_t(w)*h);
    bool gamma=d->GammaCorrectScreenshots;d->GammaCorrectScreenshots=true;d->ReadPixels(pixels.data());d->GammaCorrectScreenshots=gamma;
    std::ofstream f(prefix+"-"+std::to_string(stage)+".ppm",std::ios::binary);f<<"P6\n"<<w<<' '<<h<<"\n255\n";
    for(auto c:pixels){const char rgb[]={char(c.R),char(c.G),char(c.B)};f.write(rgb,3);}captures|=1<<stage;
   }
  }

  if(seen){maxDistance=std::max(maxDistance,length((victim->Location()-origin).xy()));
   if(game.LevelInfo->TimeSeconds()-startTime>1.6f){
    if(attacker->RuneBashHits!=1 || attacker->RuneBashBlocks!=(blocked?1:0))Exception::Throw("Bash expected one correctly blocked/unblocked impact");
    if(blocked && (victim->Health()!=health || maxDistance>12 || maxDistance<.5f))Exception::Throw("Shield bash damage/recoil mismatch");
    if(!blocked && (victim->Health()>=health || maxDistance<.5f || maxDistance>40))Exception::Throw("Unblocked bash damage/recoil mismatch");
    int expectedStuns=blocked || mode=="player-resistant" || mode=="player-immune" || mode=="player-heavy"?0:1;
    if(victim->RuneBashStuns!=expectedStuns || victim->RuneBashPhase || victim->GetStateName()=="Pain" || victim->GetStateName()=="Uninterrupted")Exception::Throw("Stagger count/recovery mismatch");
    if(expectedStuns && (stunTime<.30f || stunTime>.43f))Exception::Throw("Stagger duration mismatch");
    if(RuneBash::Start(attacker))Exception::Throw("Bash cooldown not enforced after recovery");
    if(attacker->GetStateName()=="Uninterrupted")Exception::Throw("Bash did not release animation state");
    std::ostringstream s;s<<"RUNE_BASH_PROBE_OK mode="<<mode<<" hits="<<attacker->RuneBashHits<<" blocks="<<attacker->RuneBashBlocks<<" health_before="<<health<<" health_after="<<victim->Health()<<" max_distance="<<maxDistance<<" stuns="<<victim->RuneBashStuns<<" stun_seconds="<<stunTime<<" cooldown="<<attacker->RuneBashCooldown<<" state="<<attacker->GetStateName().ToString();LogMessage(s.str());complete=true;
   }
  }
 }
private:
 int captures=0;std::string mode;UPawn *enemy=nullptr,*attacker=nullptr,*victim=nullptr;vec3 origin;int health=0,ready=0;float startTime=0,maxDistance=0,stunTime=0;bool started=false,seen=false,complete=false,firstBlock=false;
};
