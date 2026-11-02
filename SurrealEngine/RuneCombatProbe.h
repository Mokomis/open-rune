#pragma once

#include "Engine.h"
#include "RuneDualWield.h"
#include "Render/VisibleRuneBlood.h"
#include "Packages/Engine/Actors/Inventory/UWeapon.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "VM/ScriptCall.h"
#include "Utils/CommandLine.h"

// Opt-in fixtures invoke the original scripts. They never alter normal combat.
class RuneCombatProbe
{
public:
    void Tick(Engine& game, int frame, UPawn* enemy)
    {
        auto* player=game.viewport->Actor();
        if (!game.LaunchInfo.IsRune() || !player) return;
        if(commandline->HasArg("", "--probe-blood-trail")) {
            if(!commandline->HasArg("", "--save-directory") || !commandline->HasArg("", "--maxframes"))Exception::Throw("Blood fixture requires isolated bounded run");
            if(frame==145) {
                player->Health()=100;
                auto* weapon=player->GetUObject("Weapon");
                if(weapon && weapon->HasProperty("BloodTexture"))static_cast<UObject**>(weapon->GetProperty("SkelGroupSkins"))[1]=weapon->GetUObject("BloodTexture");
            }
            if(frame==150 || (frame>=180 && frame<210 && frame%10==0)) {
                if(frame==150)player->Health()-=35;
                auto* effect=player->Spawn(game.packages->FindClass("RuneI.BloodMist"),{},{},player->Location()+vec3(0,0,15),{});
                if(!effect)Exception::Throw("Blood fixture spawn failed");
                LogMessage("RUNE_BLOOD_SOURCE style="+std::to_string(effect->Style())+" glow="+std::to_string(effect->ScaleGlow()));
            }
            if(frame==151 && commandline->HasArg("", "--probe-blood-walk"))game.OnWindowKeyDown(EInputKey::IK_W);
            if(frame==320 && commandline->HasArg("", "--probe-blood-walk"))game.OnWindowKeyUp(EInputKey::IK_W);
            if(frame==1500 && RuneVisualEffects::Enabled.load() && (RuneBloodPresentation::landed==0 ||
                std::none_of(RuneBloodPresentation::stains.begin(),RuneBloodPresentation::stains.end(),[](const auto& s){return s.live;})))Exception::Throw("Heavy-hit stains expired before the map ended");
            if(frame==240 || frame==1500)LogMessage("RUNE_BLOOD_TRAIL stains="+std::to_string(std::count_if(RuneBloodPresentation::stains.begin(),RuneBloodPresentation::stains.end(),[](const auto& s){return s.live;}))+" bursts="+std::to_string(RuneBloodPresentation::bursts)+" emitted="+std::to_string(RuneBloodPresentation::emitted)+" landed="+std::to_string(RuneBloodPresentation::landed));
        }
        if(commandline->HasArg("", "--probe-effects")) {
            if(!commandline->HasArg("", "--save-directory"))Exception::Throw("Effects fixture requires isolated saves");
            if(frame==120) {
                std::set<std::string> classes;
                for(auto* actor:game.Level->Actors)if(actor&&actor->IsA("ParticleSystem")&&classes.insert(actor->Class->Name.ToString()).second)
                    LogMessage("RUNE_EFFECT_SOURCE class="+actor->Class->Name.ToString()+" type="+std::to_string(actor->GetByte("ParticleType"))+" sprite="+std::to_string(actor->GetByte("ParticleSpriteType")));
            }
            if(frame>=120&&frame%60==0) {
                auto axis=Coords::Rotation(player->Rotation()).ToMatrix();
                auto forward=(axis*vec4(1,0,0,0)).xyz();
                auto side=(axis*vec4(0,1,0,0)).xyz();
                const char* classes[]={"HitMetal","HitWood","HitStone","BloodSpray","GreenBloodSpray"};
                for(int i=0;i<5;i++) {
                    auto* effect=player->Spawn(game.packages->FindClass(std::string("RuneI.")+classes[i]),{},{},player->Location()+forward*110.f+side*((i-2)*24.f)+vec3(0,0,25),{});
                    if(!effect)Exception::Throw("Effects fixture spawn failed");
                    effect->LifeSpan()=1.f;
                }
                if(frame==120) {
                    auto* flame=player->Spawn(game.packages->FindClass("RuneI.TorchFire"),{},{},player->Location()+forward*180.f+side*95.f+vec3(0,0,15),{});
                    if(!flame)Exception::Throw("Flame fixture spawn failed");flame->LifeSpan()=60.f;
                }
                if(frame==120)LogMessage("RUNE_EFFECTS_FIXTURE_READY metal=1 wood=1 stone=1 blood=1 green_blood=1");
            }
        }
        if(commandline->HasArg("", "--probe-bloodlust")) {
            if(!commandline->HasArg("", "--save-directory"))Exception::Throw("Bloodlust fixture requires isolated saves");
            if(frame==120) {
                if(!player->Weapon()) {
                    auto* weapon=player->Spawn(game.packages->FindClass("RuneI.HandAxe"),{},{},player->Location()+vec3(0,0,100),{});
                    if(!weapon)Exception::Throw("Bloodlust weapon grant failed");
                    weapon->SetFloat("RespawnTime",0);
                    auto* copy=CallEvent(weapon,"SpawnCopy",{ExpressionValue::ObjectValue(player)}).ToObject();
                    CallEvent(player,"AcquireInventory",{ExpressionValue::ObjectValue(copy)});copy->GotoState("Active","Begin");
                }
                auto* item=player->Spawn(game.packages->FindClass("RuneI.VikingShield"),{},{},player->Location()+vec3(0,0,100),{});
                if(!item)Exception::Throw("Bloodlust shield grant failed");
                item->SetFloat("RespawnTime",0);
                auto* copy=CallEvent(item,"SpawnCopy",{ExpressionValue::ObjectValue(player)}).ToObject();
                if(copy!=item)Exception::Throw("Bloodlust grant created spare pickup");
                CallEvent(player,"AcquireInventory",{ExpressionValue::ObjectValue(item)});item->GotoState("Idle","Begin");
                CallEvent(player,"BoostStrength",{ExpressionValue::IntValue(player->GetInt("MaxStrength"))});
                for(int i=0;i<16;++i){
                    float angle=i*3.14159265f/8;vec3 direction(std::cos(angle),std::sin(angle),0),end=player->Location()+direction*600.f,hit,normal;
                    if(player->FastTrace(end,player->Location()) && player->CheckLocation(end,22,42,true).first && player->Trace(hit,normal,end-vec3(0,0,140),end,false,vec3(10,10,0)) && normal.z>.7f){
                        player->SetRotation(Rotator::FromVector(direction));player->ViewRotation()=player->Rotation();break;
                    }
                }
                LogMessage("RUNE_BLOOD_FIXTURE_READY");
            }
            if(frame>=120 && player->Health()>0 && commandline->HasArg("", "--probe-bloodlust-infinite")) {
                // Session-only practice aid; normal cost/cooldown and saves are unchanged.
                player->SetInt("Strength",player->GetInt("MaxStrength"));
                if(!player->GetBool("bBloodLust"))CallEvent(player,"BoostStrength",{ExpressionValue::IntValue(1)});
                if(frame==120)LogMessage("RUNE_BLOOD_PRACTICE infinite_meter=1 cooldown=normal");
            }
            if(frame==commandline->GetArgInt("", "--probe-bloodlust-rush-frame",-1) && !RuneBloodlust::Charge(player))Exception::Throw("Rush visual fixture could not charge");
            if(frame%120==0)LogMessage("RUNE_BLOOD_STATE meter="+std::to_string(player->GetInt("Strength"))+" ready="+std::to_string(RuneBloodlust::Readiness(player))+" phase="+std::to_string(player->Bloodlust.phase)+" leaps="+std::to_string(player->Bloodlust.leaps)+" charges="+std::to_string(player->Bloodlust.charges)+" travel="+std::to_string(player->Bloodlust.travel));
        }
        if(commandline->HasArg("", "--probe-dual")) {
            if(!commandline->HasArg("", "--save-directory"))Exception::Throw("Dual fixture requires isolated saves");
            if(frame==120) {
                auto give=[&](UPawn* p,const char* name,bool primary) {
                    auto* cls=game.packages->FindClass(std::string("RuneI.")+name);
                    UWeapon* w=nullptr;int visited=0;
                    // Arena players already receive a handaxe. Reuse it rather
                    // than bypassing normal duplicate-class pickup restrictions.
                    for(auto* item=p->Inventory();item && visited++<128;item=item->Inventory())
                        if(item->Class==cls && item->Owner()==p && !item->bDeleteMe()) {w=UObject::TryCast<UWeapon>(item);break;}
                    bool reused=w!=nullptr;
                    if(!w) {
                        auto* item=p->Spawn(cls,{},{},p->Location()+vec3(0,0,100),{});
                        if(!item)Exception::Throw("Dual fixture item spawn failed");
                        item->SetFloat("RespawnTime",0);
                        w=UObject::TryCast<UWeapon>(CallEvent(item,"SpawnCopy",{ExpressionValue::ObjectValue(p)}).ToObject());
                        if(!w || w!=item)Exception::Throw("Dual fixture grant created a spare world pickup");
                        // SpawnCopy makes weapons visible before attachment. A
                        // disabled offhand must not float at the grant position.
                        if(!primary)w->bHidden()=true;
                    }
                    if(primary)CallEvent(p,"AcquireInventory",{ExpressionValue::ObjectValue(w)});
                    if(primary || !reused)w->GotoState("Active","Begin");
                    LogMessage("RUNE_DUAL_GRANT actor="+p->Name.ToString()+" weapon="+w->Name.ToString()+" reused="+std::to_string(reused));
                };
                give(player,"VikingShortSword",true);give(player,"HandAxe",false);
                if(enemy) {give(enemy,"VikingShortSword",true);give(enemy,"HandAxe",false);}
                LogMessage("RUNE_DUAL_FIXTURE_READY");
            }
            if(frame==180) {
                int visited=0;
                for(auto* item=player->Inventory();item && visited++<128;item=item->Inventory())
                    if(auto* w=UObject::TryCast<UWeapon>(item);w && w->Owner()==player && !w->bDeleteMe() && !w->bHidden() && w->GetUObject("AttachParent")!=player)
                        Exception::Throw("Dual fixture left a visible unattached weapon: "+w->Name.ToString());
                LogMessage("RUNE_DUAL_ATTACHMENTS_OK");
            }
            if(frame>140 && commandline->HasArg("", "--probe-dual-guard"))player->bAltFire()=1;
        }
        if(commandline->HasArg("", "--probe-rune-power")) {
            if(!commandline->HasArg("", "--save-directory"))Exception::Throw("Rune Power fixture requires isolated saves");
            if(frame==120) {
                player->SetInt("RunePower",0);
                int before=player->GetInt("MaxPower");
                auto* rune=player->Spawn(game.packages->FindClass("RuneI.RuneOfPower"),{},{},player->Location()+vec3(0,0,100),{});
                if(!rune)Exception::Throw("Rune Power pickup spawn failed");
                rune->SetFloat("RunePower",100);
                auto* rules=player->GetUObject("Level")->GetUObject("Game");
                bool autoPickup=rules->GetBool("bAutoPickup");
                rules->SetBool("bAutoPickup",true); // Force the ordinary Touch route in this isolated fixture.
                LogMessage("RUNE_POWER_PICKUP_BEGIN state="+rune->GetStateName().ToString()+" valid="+std::to_string(CallEvent(rune,"ValidTouch",{ExpressionValue::ObjectValue(player)}).ToBool())+" maximum="+std::to_string(before)+" value="+std::to_string(rune->GetFloat("RunePower")));
                CallEvent(rune,"Touch",{ExpressionValue::ObjectValue(player)});
                rules->SetBool("bAutoPickup",autoPickup);
                LogMessage("RUNE_POWER_PICKUP_AFTER maximum="+std::to_string(player->GetInt("MaxPower"))+" meter="+std::to_string(player->GetInt("RunePower")));
                if(player->GetInt("MaxPower")<=before || player->GetInt("RunePower")!=player->GetInt("MaxPower"))
                    Exception::Throw("Rune pickup did not raise and fill the meter");
                LogMessage("RUNE_POWER_PICKUP_OK maximum_before="+std::to_string(before)+" maximum_after="+std::to_string(player->GetInt("MaxPower"))+" meter="+std::to_string(player->GetInt("RunePower")));
            }
            if(frame==140) {
                auto* item=player->Spawn(game.packages->FindClass("RuneI.HandAxe"),{},{},player->Location()+vec3(0,0,100),{});
                if(!item)Exception::Throw("Rune Power weapon spawn failed");
                item->SetFloat("RespawnTime",0);
                auto* copy=CallEvent(item,"SpawnCopy",{ExpressionValue::ObjectValue(player)}).ToObject();
                if(!copy)Exception::Throw("Rune Power weapon copy failed");
                CallEvent(player,"AcquireInventory",{ExpressionValue::ObjectValue(copy)});copy->GotoState("Active","Begin");
            }
            if(frame==200) {
                auto* w=player->GetUObject("Weapon");
                if(!w || !w->GetBool("bCanBePoweredUp"))Exception::Throw("Rune Power fixture has no powerable weapon");
                runePowerBefore=player->GetInt("RunePower");
                runePowerCost=w->GetInt("RunePowerRequired");
                game.OnWindowKeyDown(EInputKey::IK_E);
                LogMessage("RUNE_POWER_ACTIVATE key=E meter_before="+std::to_string(runePowerBefore)+" cost="+std::to_string(runePowerCost));
            }
            if(frame==204)game.OnWindowKeyUp(EInputKey::IK_E);
            if(frame==240) {
                auto* w=player->GetUObject("Weapon");
                int after=player->GetInt("RunePower");
                if(!w || runePowerCost<=0 || runePowerBefore<runePowerCost || after!=runePowerBefore-runePowerCost || !w->GetBool("bPoweredUp"))
                    Exception::Throw("Rune Power activation did not spend meter and power the weapon");
                LogMessage("RUNE_POWER_ACTIVATION_OK meter_after="+std::to_string(after)+" powered=1");
            }
            if(frame==260) {
                int before=player->GetInt("RunePower");
                auto* rune=player->Spawn(game.packages->FindClass("RuneI.RuneOfPowerRefill"),{},{},player->Location()+vec3(0,0,100),{});
                if(!rune)Exception::Throw("Rune Power refill spawn failed");
                rune->SetFloat("RunePower",25);
                auto* rules=player->GetUObject("Level")->GetUObject("Game");
                bool autoPickup=rules->GetBool("bAutoPickup");rules->SetBool("bAutoPickup",true);
                CallEvent(rune,"Touch",{ExpressionValue::ObjectValue(player)});
                rules->SetBool("bAutoPickup",autoPickup);
                if(player->GetInt("RunePower")!=std::min(int(player->GetInt("MaxPower")),before+25))
                    Exception::Throw("Rune refill did not restore meter");
                LogMessage("RUNE_POWER_REFILL_OK meter_before="+std::to_string(before)+" meter_after="+std::to_string(player->GetInt("RunePower")));
            }
        }
        if(commandline->HasArg("", "--probe-endurance") && enemy)
        {
            if(!enduranceStarted) {
                playerStartHealth=player->Health(); enemyStartHealth=enemy->Health(); enduranceStarted=true;
                LogMessage("RUNE_ENDURANCE_BEGIN synthetic=1 health_and_durability_replenished=1");
            }
            if(player->Health()<=0 || enemy->Health()<=0) Exception::Throw("Endurance fixture died; measurement is not active combat");
            playerDamage+=std::max(0,playerStartHealth-player->Health());
            enemyDamage+=std::max(0,enemyStartHealth-enemy->Health());
            player->Health()=playerStartHealth; enemy->Health()=enemyStartHealth;
            for(auto* pawn:{static_cast<UPawn*>(player),enemy}) {
                auto* parts=static_cast<int*>(pawn->GetProperty("BodyPartHealth"));
                for(int i=0;i<15;i++) parts[i]=10000;
            }
            if(auto* shield=player->GetUObject("Shield")) shield->SetInt("Health",70);
            if(frame%600==0) LogMessage("RUNE_ENDURANCE_ACTIVE frame="+std::to_string(frame)+" player_damage="+std::to_string(playerDamage)+" enemy_damage="+std::to_string(enemyDamage));
        }
        int joint=commandline->GetArgInt("", "--probe-limb-joint", -1);
        if (joint>=0 && frame==150)
        {
            if (!enemy || joint>=50) Exception::Throw("Limb probe requires a live enemy and valid joint");
            int part=CallEvent(enemy,"BodyPartForJoint",{ExpressionValue::IntValue(joint)}).ToInt();
            auto* health=static_cast<int*>(enemy->GetProperty("BodyPartHealth"));
            if (part<0 || part>=15 || !CallEvent(enemy,"BodyPartSeverable",{ExpressionValue::IntValue(part)}).ToBool())
                Exception::Throw("Limb probe joint is not severable");
            int before=health[part];
            auto* limbClass=CallEvent(enemy,"SeveredLimbClass",{ExpressionValue::IntValue(part)}).ToObject();
            if(!limbClass) Exception::Throw("Enemy has no severed limb class");
            size_t actorsBefore=game.Level->Actors.size();
            CallEvent(enemy,"JointDamaged",{ExpressionValue::IntValue(80),ExpressionValue::ObjectValue(player),
                ExpressionValue::VectorValue(enemy->Location()),ExpressionValue::VectorValue(vec3(100,0,100)),
                ExpressionValue::NameValue("sever"),ExpressionValue::IntValue(joint)});
            int spawned=0;
            for(size_t i=actorsBefore;i<game.Level->Actors.size();++i)
                if(auto* a=game.Level->Actors[i]; a && a->Class==limbClass) {
                    spawned++; LogMessage("RUNE_LIMB_SPAWN class="+a->Class->Name.ToString()+" state="+a->GetStateName().ToString());
                }
            auto* flags=static_cast<uint8_t*>(enemy->GetProperty("JointFlags"));
            LogMessage("RUNE_LIMB_RESULT joint="+std::to_string(joint)+" part="+std::to_string(part)+" before="+std::to_string(before)+
                " after="+std::to_string(health[part])+" flags="+std::to_string(flags[joint])+" spawned="+std::to_string(spawned));
            if(before<=0 || health[part]>0 || spawned!=1 || (flags[joint]&0x10)) Exception::Throw("Limb severing did not remove collision and spawn the detached part");
            auto* groups=static_cast<int*>(enemy->GetProperty("SkelGroupFlags"));
            bool warrior=enemy->IsA("GoblinWarrior");
            int hidden=warrior?(joint==9?1:joint==15?10:11):(joint==9?2:joint==15?12:1);
            int cap=warrior?(joint==9?7:joint==15?8:4):(joint==9?9:joint==15?10:5);
            if(!(groups[hidden]&1) || (groups[cap]&1) || (joint==20 && !(groups[warrior?12:6]&1)))
                Exception::Throw("Severed body geometry or gore cap visibility is wrong");
            if(joint==15 && enemy->GetUObject("Weapon")) Exception::Throw("Severed weapon arm retained equipped weapon");
            LogMessage("RUNE_LIMB_OK joint="+std::to_string(joint)+" geometry_hidden=1 cap_visible=1 collision_removed=1");
        }
        std::string action=commandline->GetArg("", "--probe-weapon", "");
        if(action.empty()) return;
        bool touch=commandline->HasArg("", "--probe-weapon-touch");
        if(frame==50 && commandline->HasArg("", "--probe-weapon-class")) {
            auto* pickup=player->Spawn(game.packages->FindClass("RuneI."+commandline->GetArg("", "--probe-weapon-class", "")),{},{},player->Location(),player->Rotation());
            if(!pickup)Exception::Throw("Cannot spawn weapon fixture");
            pickup->SetFloat("RespawnTime",0); // Campaign-style, non-respawning inventory.
            LogMessage("RUNE_FOLLOW_GRANT name="+pickup->Name.ToString()+" base="+(pickup->ActorBase()?pickup->ActorBase()->Name.ToString():"None"));
            auto* copy=CallEvent(pickup,"SpawnCopy",{ExpressionValue::ObjectValue(player)}).ToObject();
            if(!copy)Exception::Throw("Cannot copy weapon fixture");
            CallEvent(player,"AcquireInventory",{ExpressionValue::ObjectValue(copy)});copy->GotoState("Active","Begin");
        }
        if(frame==100)
        {
            weapon=UObject::TryCast<UActor>(player->GetUObject("Weapon"));
            if(!weapon) Exception::Throw("Weapon probe requires an equipped weapon");
            start=weapon->Location();
            if(action=="throw" || action=="follow") { if(!touch) game.OnWindowKeyDown(EInputKey::IK_Q); }
            else if(action=="drop") CallEvent(player,"DropWeapon");
            else Exception::Throw("Unknown weapon probe action");
            LogMessage("RUNE_WEAPON_BEGIN action="+action+" class="+weapon->Class->Name.ToString());
        }
        if(frame==108 && (action=="throw" || action=="follow") && !touch) game.OnWindowKeyUp(EInputKey::IK_Q);
        if(weapon && frame%60==0)
        {
            LogMessage("RUNE_WEAPON_STATE frame="+std::to_string(frame)+" state="+weapon->GetStateName().ToString()+
                " carried="+std::to_string((bool)weapon->bCarriedItem())+" attached="+std::to_string(weapon->GetUObject("AttachParent")!=nullptr)+
                " physics="+std::to_string(weapon->Physics())+" distance="+std::to_string(length(weapon->Location()-start))+
                " equipped="+std::to_string(player->GetUObject("Weapon")!=nullptr)+
                " base="+(weapon->ActorBase()?weapon->ActorBase()->Name.ToString():"None")+
                " owner="+(weapon->Owner()?weapon->Owner()->Name.ToString():"None")+
                " x="+std::to_string(weapon->Location().x)+" y="+std::to_string(weapon->Location().y)+" z="+std::to_string(weapon->Location().z));
            moved=std::max(moved,length(weapon->Location()-start));
        }
        if(action=="follow") {
            if(frame==600){
                if(!weapon || weapon->GetStateName()!="Pickup" || weapon->Physics()!=PHYS_None)Exception::Throw("Follow fixture weapon must settle first");
                followStart=weapon->Location();playerStart=player->Location();
                game.OnWindowKeyDown(EInputKey::IK_W);
                LogMessage("RUNE_FOLLOW_BEGIN base="+(weapon->ActorBase()?weapon->ActorBase()->Name.ToString():"None"));
            }
            if(frame==720){
                game.OnWindowKeyUp(EInputKey::IK_W);
                LogMessage("RUNE_FOLLOW_RESULT player_moved="+std::to_string(length(player->Location()-playerStart))+" weapon_moved="+std::to_string(length(weapon->Location()-followStart))+
                    " base="+(weapon->ActorBase()?weapon->ActorBase()->Name.ToString():"None"));
            }
            return;
        }
        if(touch ? (pickupPositionFrame<0 && frame>=900 && weapon && weapon->GetStateName()=="Pickup" && weapon->Physics()==PHYS_None) : frame==420)
        {
            if(!weapon || weapon->bDeleteMe() || weapon->bCarriedItem() || weapon->GetUObject("AttachParent") || moved<10)
                Exception::Throw("Weapon did not detach and move through world physics");
            // Position the pickup fixture beside the landed item; Use/animation
            // still performs the actual pickup, respecting the normal rules.
            if(!player->SetLocation(weapon->Location()+vec3(0,0,45)))Exception::Throw("Cannot position pickup fixture");
            player->Velocity()=vec3(0);
            pickupPositionFrame=frame;
        }
        if((!touch || pickupPositionFrame>=0) && frame==(touch?pickupPositionFrame+200:600)) {
            if(touch) LogMessage("RUNE_WEAPON_TOUCH_READY");
            else game.OnWindowKeyDown(EInputKey::IK_LControl);
        }
        if(frame==608 && !touch) game.OnWindowKeyUp(EInputKey::IK_LControl);
        if(frame==(touch?2200:900))
        {
            auto* equipped=UObject::TryCast<UActor>(player->GetUObject("Weapon"));
            if(!equipped || !equipped->bCarriedItem() || equipped->GetUObject("AttachParent")!=player)
                Exception::Throw("Use did not reacquire and attach the dropped/thrown weapon");
            LogMessage("RUNE_WEAPON_OK action="+action+" moved="+std::to_string(moved)+" picked_up=1 attached=1 touch="+std::to_string(touch));
        }
        if(touch && frame==2400)
        {
            player->Health()=50;
            auto* food=player->Spawn(game.packages->FindClass("RuneI.HealthFruit1"),{},{},player->Location()+vec3(20,0,-35),Rotator(0,0,0));
            if(!food) Exception::Throw("Could not spawn food touch fixture");
            LogMessage("RUNE_FOOD_TOUCH_BEGIN health=50 class=HealthFruit1");
        }
        if(touch && frame==2600)
        {
            if(player->Health()!=50) Exception::Throw("Food fixture must wait for touch Use");
            LogMessage("RUNE_FOOD_TOUCH_READY");
        }
        if(touch && frame==3400)
        {
            if(player->Health()<=50) Exception::Throw("Touch Use did not consume food");
            LogMessage("RUNE_FOOD_TOUCH_OK health_before=50 health_after="+std::to_string(player->Health()));
        }
    }
private:
    UActor* weapon=nullptr;
    vec3 start{0.0f},followStart{0.0f},playerStart{0.0f};
    float moved=0;
    int pickupPositionFrame=-1;
    bool enduranceStarted=false;
    int playerStartHealth=0,enemyStartHealth=0,playerDamage=0,enemyDamage=0;
    int runePowerBefore=0,runePowerCost=0;
};
