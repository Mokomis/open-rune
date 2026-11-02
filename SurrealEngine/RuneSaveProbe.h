#pragma once

#include "Engine.h"
#include "RuneCombat.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Core/Properties/UProperty.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Inventory/UInventory.h"
#include "Packages/Engine/Actors/Inventory/UWeapon.h"
#include "Packages/Engine/Actors/NavigationPoint/UTeleporter.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include "VM/Frame.h"
#include "VM/ScriptCall.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <set>
#include <vector>

// Opt-in fixtures only. Tick after the engine has consumed travel requests, so
// snapshots never retain pointers to a map that is about to be unloaded.
class RuneSaveProbe
{
public:
	RuneSaveProbe()
	{
		flow = commandline && commandline->HasArg("", "--probe-save-flow");
		route = commandline && commandline->HasArg("", "--probe-travel-exit");
		resume = commandline && commandline->HasArg("", "--probe-resume-slot");
		if (resume) resumeSlot = commandline->GetArgInt("", "--probe-resume-slot", -1);
	}

	bool Active() const { return (flow || resume || route) && !complete; }

	void Tick(Engine& game, int frame)
	{
		if (!Active()) return;
		if (!initialized)
		{
			Require(game.LaunchInfo.IsRune(), "requires Rune");
			Require(int(flow)+int(resume)+int(route)==1, "select one save fixture mode");
			std::string dir = commandline->GetArg("", "--save-directory", "");
			Require(!dir.empty() && fs::path(dir).is_absolute(), "requires isolated absolute --save-directory");
			Require(game.packages->GetSaveFolderPath() == fs::path(dir).lexically_normal(), "save-directory override not active");
			if (resume)
			{
				Require(resumeSlot == 90 || resumeSlot == 91, "resume slot must be fixture slot 90 or 91");
				expected = ReadManifest(game, resumeSlot);
				Load(game, resumeSlot);
				Verify(game, expected, true, true, "resume");
				LogMessage("RUNE_SAVE_RESUME_RESTORED slot=" + std::to_string(resumeSlot) + " map=" + Map(game));
				dueFrame = frame + 240;
			}
			else if (flow) Require(Map(game) == "SinkingShip2", "save flow must start in SinkingShip2");
			else { routeSource=Map(game); routeURL=commandline->GetArg("", "--probe-travel-url", ""); Require(!routeURL.empty(), "route fixture needs original exit URL"); }
			initialized = true;
		}

		if (resume)
		{
			if (frame >= dueFrame)
			{
				Require(Map(game) == expected.map && Player(game)->Health() > 0, "resume did not remain playable");
				CheckLinks(game);
				LogMessage("RUNE_SAVE_RESUME_OK slot=" + std::to_string(resumeSlot) + " map=" + Map(game) + " continued_frames=240");
				complete = true;
			}
			return;
		}

		if (route) { TickRoute(game, frame); return; }

		switch (phase)
		{
		case 0:
			if (frame < 240) return;
			Grant(game, "Weapon", "HandAxe", "Active");
			Grant(game, "Shield", "VikingShield", "Idle");
			dueFrame = frame + 120;
			phase = 1;
			LogMessage("RUNE_SAVE_FIXTURE equipment_granted=1");
			break;
		case 1:
			if (frame < dueFrame) return;
			Require(Player(game)->Health() > 0 && Player(game)->GetUObject("Shield"), "fixture died or lost its shield during setup");
			Player(game)->Health() = 73;
			Player(game)->GetUObject("Shield")->SetInt("Health", 41);
			BeginRequestCollision(game);
			dueFrame = frame + 1;
			phase = 7;
			break;
		case 7:
			if (frame < dueFrame) return;
			VerifyRequestCollision(game);
			RoundTrip(game, 90);
			RoundTrip(game, 91); // Saving a loaded Save90 package must retain its real map.
			MovingRoundTrip(game); // Restore the stationary fixture before real exits.
			dueFrame = frame + 240;
			phase = 2;
			break;
		case 2:
			if (frame < dueFrame) return;
			expected = TravelSnapshot(game);
			TriggerExit(game, "Teleporter0", "DeepUnder1Odin/Start");
			dueFrame = frame + 300;
			phase = 3;
			break;
		case 3:
			if (Map(game) == "DeepUnder1Odin")
			{
				expected.map = "DeepUnder1Odin";
				Verify(game, expected, false, false, "first transition");
				LogMessage("RUNE_TRAVEL_OK from=SinkingShip2 to=DeepUnder1Odin health=" + std::to_string(Player(game)->Health()));
				RoundTrip(game, 91);
				dueFrame = frame + 240;
				phase = 4;
			}
			else Require(frame < dueFrame, "first exit did not transition within 300 frames");
			break;
		case 4:
			if (frame < dueFrame) return;
			expected = TravelSnapshot(game);
			TriggerExit(game, "Teleporter1", "DeepUnder3/Start");
			dueFrame = frame + 300;
			phase = 5;
			break;
		case 5:
			if (Map(game) == "DeepUnder3")
			{
				expected.map = "DeepUnder3";
				Verify(game, expected, false, false, "second transition");
				LogMessage("RUNE_TRAVEL_OK from=DeepUnder1Odin to=DeepUnder3 health=" + std::to_string(Player(game)->Health()));
				dueFrame = frame + 240;
				phase = 6;
			}
			else Require(frame < dueFrame, "second exit did not transition within 300 frames");
			break;
		case 6:
			if (frame < dueFrame) return;
			RoundTrip(game, 90);
			game.packages->SaveAllIniFiles(); // Slot metadata must survive process exit too.
			LogMessage("RUNE_SAVE_FLOW_OK maps=SinkingShip2,DeepUnder1Odin,DeepUnder3 final_slot=90");
			complete = true;
			break;
		}
	}

private:
	struct Snapshot
	{
		std::string map, playerName, weapon, shield;
		std::string playerState, animation, proxyState, proxyAnimation;
		int health = 0, maxHealth = 0, strength = 0, runePower = 0, shieldHealth = 0;
		int physics = 0;
		float animationFrame = 0.0f, animationRate = 0.0f, proxyFrame = 0.0f, proxyRate = 0.0f;
		vec3 location = vec3(0.0f);
		vec3 velocity = vec3(0.0f), acceleration = vec3(0.0f);
		std::vector<std::string> inventory, stowed;
	};
	bool flow = false, resume = false, route = false, initialized = false, complete = false;
	std::string routeSource, routeURL;
	int resumeSlot = -1, phase = 0, dueFrame = 0;
	Snapshot expected;

    // Bounded exit/save compatibility fixture. The exit's real Touch script is
    // invoked directly; this deliberately does not claim puzzle or route traversal.
    void TickRoute(Engine& game, int frame)
    {
        if (phase==0) {
            if (frame<240) return;
            auto state=Player(game)->GetStateName();
            if (state!="PlayerWalking" && state!="PlayerSwimming") {
                Require(frame<18000,"route source did not release player control");return;
            }
            Grant(game,"Weapon","HandAxe","Active");
            Grant(game,"Shield","VikingShield","Idle");
            dueFrame=frame+120;phase=1;
        } else if (phase==1 && frame>=dueFrame) {
            RoundTrip(game,90);
            expected=TravelSnapshot(game);
            TriggerExit(game,commandline->GetArg("","--probe-travel-exit","").c_str(),routeURL.c_str());
            dueFrame=frame+600;phase=2;
        } else if (phase==2) {
            auto destination=fs::path(routeURL.substr(0,routeURL.find('/'))).stem().string();
            if (NameString(Map(game))!=NameString(destination)) {
                Require(frame<dueFrame,"original route exit did not travel");return;
            }
            auto actual=Capture(game);
            Require(actual.health==expected.health && actual.maxHealth==expected.maxHealth &&
                actual.strength==expected.strength && actual.runePower==expected.runePower,
                "route travel lost player values");
            Require(actual.inventory==expected.inventory && actual.shield==expected.shield && actual.shieldHealth==expected.shieldHealth,
                "route travel lost inventory or shield");
            // Water transitions may legitimately move the same weapon between
            // the hand and back. Compare the complete held/stowed weapon set.
            auto weapons=[](const Snapshot& s) {
                auto result=s.stowed;result.push_back(s.weapon);
                result.erase(std::remove(result.begin(),result.end(),"None"),result.end());
                std::sort(result.begin(),result.end());return result;
            };
            Require(weapons(actual)==weapons(expected),"route travel lost held/stowed weapons");
            CheckLinks(game);RoundTrip(game,91);
            game.packages->SaveAllIniFiles();
            dueFrame=frame+240;phase=3;
            LogMessage("RUNE_ROUTE_TRAVEL_OK from="+routeSource+" to="+Map(game)+" inventory_preserved=1 source_save=90 destination_save=91");
        } else if (phase==3 && frame>=dueFrame) {
            Require(Player(game)->Health()>0,"route destination died during continuation");
            CheckLinks(game);
            LogMessage("RUNE_ROUTE_FLOW_OK from="+routeSource+" to="+Map(game)+" continued_frames=240 coverage=exit_and_save_fixture_not_traversal");
            complete=true;
        }
    }

	static void Require(bool condition, const std::string& reason)
	{
		if (condition) return;
		LogMessage("RUNE_SAVE_PROBE_FAIL reason=" + reason);
		Exception::Throw("Rune save probe: " + reason);
	}
	static UPlayerPawn* Player(Engine& game)
	{
		auto* player = game.viewport ? game.viewport->Actor() : nullptr;
		Require(player != nullptr, "no possessed player");
		return player;
	}
	static std::string Map(Engine& game)
	{
		return game.LevelInfo ? fs::path(game.LevelInfo->URL.Map).stem().string() : std::string();
	}
	static std::string ClassName(UObject* object)
	{
		return object ? UObject::GetUClassFullName(object).ToString() : "None";
	}
	static Snapshot Capture(Engine& game)
	{
		auto* player = Player(game);
		Require(player->Health() > 0, "fixture player died before snapshot");
		Snapshot s;
		s.map = Map(game);
		s.playerName = player->Name.ToString();
		s.health = player->Health();
		s.maxHealth = (int)player->GetInt("MaxHealth");
		s.strength = (int)player->GetInt("Strength");
		s.runePower = (int)player->GetInt("RunePower");
		s.location = player->Location();
		s.playerState = player->GetStateName().ToString();
		s.physics = player->Physics();
		s.velocity = player->Velocity();
		s.acceleration = player->Acceleration();
		s.animation = player->AnimSequence().ToString();
		s.animationFrame = player->AnimFrame();
		s.animationRate = player->AnimRate();
		auto* proxy = UObject::TryCast<UActor>(player->GetUObject("AnimProxy"));
		Require(proxy != nullptr, "fixture has no animation proxy");
		s.proxyState = proxy->GetStateName().ToString();
		s.proxyAnimation = proxy->AnimSequence().ToString();
		s.proxyFrame = proxy->AnimFrame();
		s.proxyRate = proxy->AnimRate();
		s.weapon = ClassName(player->GetUObject("Weapon"));
		auto* shield = player->GetUObject("Shield");
		s.shield = ClassName(shield);
		s.shieldHealth = shield ? (int)shield->GetInt("Health") : 0;
		std::set<UObject*> seen;
		for (auto* item = player->Inventory(); item; item = item->Inventory())
		{
			Require(seen.insert(item).second && seen.size() < 256, "inventory contains cycle or excessive entries");
			s.inventory.push_back(ClassName(item));
		}
		std::sort(s.inventory.begin(), s.inventory.end());
		auto** stowed = static_cast<UObject**>(player->GetProperty("StowSpot"));
		for (int i = 0; i < 3; ++i) s.stowed.push_back(ClassName(stowed[i]));
		return s;
	}
	static void CheckLinks(Engine& game)
	{
		auto* player = Player(game);
		for (auto* actor : game.Level->Actors)
		{
			if (!actor || actor->bDeleteMe()) continue;
			if (actor->Owner())
			{
				const auto& children = actor->Owner()->ChildActors;
				Require(std::count(children.begin(), children.end(), actor) == 1, "owner child list missing/duplicate: " + actor->Name.ToString());
			}
		}
		std::vector<UActor*> carried;
		bool hasWeapon = false;
		for (const char* property : {"Weapon", "Shield"})
		{
			auto* item = UObject::TryCast<UActor>(player->GetUObject(property));
			if (std::string(property) == "Shield") Require(item != nullptr, "fixture shield absent");
			if (item) carried.push_back(item);
			if (std::string(property) == "Weapon" && item) hasWeapon = true;
		}
		auto** stowed = static_cast<UObject**>(player->GetProperty("StowSpot"));
		for (int i = 0; i < 3; ++i)
		{
			if (auto* item = UObject::TryCast<UActor>(stowed[i]))
			{
				carried.push_back(item);
				hasWeapon = true;
				Require(item->GetStateName() == "Stow", "stowed weapon has wrong state");
			}
		}
		Require(hasWeapon, "fixture weapon neither held nor stowed");
		for (auto* item : carried)
		{
			Require(!item->bDeleteMe(), "carried item destroyed: " + item->Name.ToString());
			Require(item->Owner() == player, "carried owner mismatch: " + item->Name.ToString());
			Require(item->GetUObject("AttachParent") == player && item->bCarriedItem(), "carried attachment absent: " + item->Name.ToString());
			int joint = item->GetByte("AttachParentJoint");
			Require(joint < 50 && static_cast<UObject**>(player->GetProperty("JointChild"))[joint] == item, "joint child mismatch: " + item->Name.ToString());
		}
		auto* proxy = UObject::TryCast<UActor>(player->GetUObject("AnimProxy"));
		Require(proxy && proxy->Owner() == player, "animation proxy owner mismatch");
	}
	static Snapshot TravelSnapshot(Engine& game)
	{
		Snapshot s = Capture(game);
		auto* shield = Player(game)->GetUObject("Shield");
		if (shield)
		{
			auto* health = shield->Class->GetProperty("Health");
			Require(health != nullptr, "shield has no Health property");
			if (!AllFlags(health->PropFlags, PropertyFlags::Travel))
			{
				// Rune Shield.Health is not a travel property. Preserve strict save
				// roundtrips, but expect the original class default on level travel.
				s.shieldHealth = (int)shield->Class->GetDefaultObject<UObject>()->GetInt("Health");
				LogMessage("RUNE_TRAVEL_POLICY shield_health=class_default expected=" + std::to_string(s.shieldHealth));
			}
		}
		return s;
	}
	static void Verify(Engine& game, const Snapshot& wanted, bool position, bool identity, const char* stage)
	{
		auto actual = Capture(game);
		LogMessage(std::string("RUNE_SAVE_VERIFY stage=") + stage + " map=" + actual.map + " health=" + std::to_string(actual.health) + " weapon=" + actual.weapon + " shield=" + actual.shield + " shield_health=" + std::to_string(actual.shieldHealth));
		Require(actual.map == wanted.map, std::string(stage) + ": wrong original map metadata");
		Require(actual.health == wanted.health && actual.maxHealth == wanted.maxHealth && actual.strength == wanted.strength && actual.runePower == wanted.runePower, std::string(stage) + ": player values changed");
		Require(actual.weapon == wanted.weapon && actual.shield == wanted.shield && actual.shieldHealth == wanted.shieldHealth, std::string(stage) + ": equipped item/durability changed");
		Require(actual.inventory == wanted.inventory && actual.stowed == wanted.stowed, std::string(stage) + ": inventory/stowed weapons changed");
		if (position) Require(length(actual.location - wanted.location) < 0.01f, std::string(stage) + ": saved position changed");
		if (identity)
		{
			Require(actual.playerName == wanted.playerName, std::string(stage) + ": saved pawn replaced");
			Require(actual.playerState == wanted.playerState && actual.physics == wanted.physics, std::string(stage) + ": player state/physics changed");
			Require(length(actual.velocity - wanted.velocity) < 0.0001f && length(actual.acceleration - wanted.acceleration) < 0.0001f, std::string(stage) + ": velocity/acceleration changed");
			Require(actual.animation == wanted.animation && std::abs(actual.animationFrame - wanted.animationFrame) < 0.0001f && std::abs(actual.animationRate - wanted.animationRate) < 0.0001f, std::string(stage) + ": player animation changed");
			Require(actual.proxyState == wanted.proxyState && actual.proxyAnimation == wanted.proxyAnimation && std::abs(actual.proxyFrame - wanted.proxyFrame) < 0.0001f && std::abs(actual.proxyRate - wanted.proxyRate) < 0.0001f, std::string(stage) + ": proxy state/animation changed");
		}
		CheckLinks(game);
	}
	static void Grant(Engine& game, const char* property, const char* className, const char* state)
	{
		auto* player = Player(game);
		if (player->GetUObject(property)) return;
		auto* pickup = player->Spawn(game.packages->FindClass(std::string("RuneI.") + className), {}, {}, player->Location(), player->Rotation());
		Require(pickup != nullptr, std::string("cannot spawn fixture ") + className);
		auto* copy = CallEvent(pickup, "SpawnCopy", {ExpressionValue::ObjectValue(player)}).ToObject();
		Require(copy != nullptr, std::string("cannot copy fixture ") + className);
		CallEvent(player, "AcquireInventory", {ExpressionValue::ObjectValue(copy)});
		// Original Weapon.Touch leaves a swimming player's weapon in Stow:
		// RunePlayerProxy.AcquireInventory attaches it, then InstantStow moves it
		// onto the back and clears Weapon. Shield.Touch always selects Idle.
		bool swimmingWeapon = std::string(property) == "Weapon" && player->GetStateName() == "PlayerSwimming";
		if (!swimmingWeapon) copy->GotoState(state, "Begin");
		if (swimmingWeapon)
		{
			auto** stowed = static_cast<UObject**>(player->GetProperty("StowSpot"));
			Require((stowed[0] == copy || stowed[1] == copy || stowed[2] == copy) && copy->GetStateName() == "Stow", "swimming pickup was not stowed by original scripts");
			LogMessage("RUNE_SAVE_FIXTURE_PICKUP class=" + std::string(className) + " placement=stowed player_state=PlayerSwimming");
		}
		else
		{
			Require(player->GetUObject(property) == copy, std::string("fixture was not equipped: ") + property);
			LogMessage("RUNE_SAVE_FIXTURE_PICKUP class=" + std::string(className) + " placement=equipped player_state=" + player->GetStateName().ToString());
		}
	}
	static fs::path SavePath(Engine& game, int slot)
	{
		return game.packages->GetSaveFolderPath() / ("Save" + std::to_string(slot) + "." + game.packages->GetSaveExtension());
	}
	static void BeginRequestCollision(Engine& game)
	{
		auto* player = Player(game);
		Require(Map(game) == "SinkingShip2" && player->GetBool("bCheckPointSave"), "checkpoint collision fixture requires enabled campaign checkpoints");
		Require(player->Physics() == PHYS_Walking || player->Physics() == PHYS_Swimming, "checkpoint collision fixture requires a safe movement mode");
		Require(game.SaveGameInfo.SaveGameSlot == DONT_SAVE_GAME, "unexpected pending save before collision fixture");
		// Only isolated fixture paths are touched. Preserve previous test files,
		// and require newly-created slots so repeated runs cannot pass on stale data.
		for (int slot : {9, 10})
		{
			for (const auto& path : {SavePath(game, slot), game.packages->GetSaveFolderPath() / ("Save" + std::to_string(slot) + ".mapname")})
			{
				if (!fs::exists(path)) continue;
				fs::path backup;
				int suffix = 0;
				do { backup = path.string() + ".race-before-" + std::to_string(suffix++); } while (fs::exists(backup) && suffix < 1000);
				Require(!fs::exists(backup), "too many prior request-collision fixture backups");
				fs::rename(path, backup);
			}
		}
		player->SetFloat("CheckPointSaveTime", -1.0f);
		Require(RuneCombat::Request(2), "quick-save collision request was not accepted");
		// Reproduce an end-of-draw readiness publication racing a queued request.
		// Admission must still reject the second action because the slot is full.
		RuneCombat::Ready.store(true);
		Require(!RuneCombat::Request(5) && RuneCombat::SessionRequest.load() == 2, "overlapping fight request replaced pending quick save");
		RuneCombat::Ready.store(false);
		LogMessage("RUNE_SAVE_REQUEST_RACE_BEGIN quick_slot=9 checkpoint_due=1 overlapping_fight_rejected=1");
	}
	static void VerifyRequestCollision(Engine& game)
	{
		Require(RuneCombat::SessionRequest.load() == 0, "quick-save collision request was not consumed");
		for (int slot : {9, 10})
		{
			Require(fs::is_regular_file(SavePath(game, slot)) && fs::file_size(SavePath(game, slot)) > 0, "same-frame quick/checkpoint save lost slot " + std::to_string(slot));
			std::ifstream metadata(game.packages->GetSaveFolderPath() / ("Save" + std::to_string(slot) + ".mapname"));
			std::string map;
			Require((bool)std::getline(metadata, map) && fs::path(map).stem() == "SinkingShip2", "same-frame save has wrong/missing map metadata");
		}
		Require(Player(game)->GetFloat("CheckPointSaveTime") > 0.0f, "original checkpoint countdown did not run");
		Require(game.SaveGameInfo.SaveGameSlot == DONT_SAVE_GAME, "save dispatch left a pending slot");
		LogMessage("RUNE_SAVE_REQUEST_RACE_OK quick_slot=9 checkpoint_slot=10 both_new_files=1 overlapping_fight_rejected=1");
	}
	static void Load(Engine& game, int slot)
	{
		Require(fs::is_regular_file(SavePath(game, slot)), "fixture save file missing");
		game.LoadFromSaveFile(UnrealURL("?load=" + std::to_string(slot)));
		game.PossessSavedPlayer();
		game.CalcTimeElapsed(); // Loading work is not elapsed simulation time.
	}
	static void RoundTrip(Engine& game, int slot)
	{
		Snapshot saved = Capture(game);
		CheckLinks(game);
		std::string sleeperName, sleeperState;
		float sleepLeft = 0.0f;
		for (auto* actor : game.Level->Actors)
		{
			if (actor && !actor->bDeleteMe() && actor != Player(game) && actor->StateFrame && actor->StateFrame->GetLatentState() == LatentRunState::Sleep && actor->LatentFloat() > 0.0f)
			{
				sleeperName = actor->Name.ToString();
				sleeperState = actor->GetStateName().ToString();
				sleepLeft = actor->LatentFloat();
				break;
			}
		}
		LogMessage("RUNE_SAVE_BEGIN slot=" + std::to_string(slot) + " map=" + saved.map);
		game.SaveGameToSlot(slot, "Rune validation fixture");
		WriteManifest(game, slot, saved);
		auto* player = Player(game);
		player->Health() = 19;
		player->GetUObject("Shield")->SetInt("Health", 3);
		player->Velocity() = vec3(0.0f);
		player->Acceleration() = vec3(0.0f);
		player->SetPhysics(PHYS_None);
		player->AnimFrame() = 0.0f;
		Require(player->SetLocation(player->Location() + vec3(0, 0, 8)), "cannot perturb fixture position");
		Load(game, slot);
		Verify(game, saved, true, true, "roundtrip");
		if (!sleeperName.empty())
		{
			UActor* sleeper = nullptr;
			for (auto* actor : game.Level->Actors) if (actor && actor->Name == sleeperName) { sleeper = actor; break; }
			Require(sleeper && sleeper->StateFrame && sleeper->GetStateName() == sleeperState && sleeper->StateFrame->GetLatentState() == LatentRunState::Sleep && std::abs(sleeper->LatentFloat() - sleepLeft) < 0.0001f, "sleeping actor countdown/state not restored");
			LogMessage("RUNE_SAVE_SLEEP_OK actor=" + sleeperName + " remaining=" + std::to_string(sleepLeft));
		}
		else LogMessage("RUNE_SAVE_SLEEP_SKIPPED reason=no_active_sleeping_actor");
		LogMessage("RUNE_SAVE_ROUNDTRIP_OK slot=" + std::to_string(slot) + " map=" + saved.map);
	}
	static void MovingRoundTrip(Engine& game)
	{
		// Slot91 contains the real settled fixture. The synthetic falling snapshot
		// must round-trip without Possess/StartWalk wiping velocity or animation.
		// Restore slot91 afterwards, so this setup cannot affect campaign travel.
		Snapshot original = Capture(game);
		auto* player = Player(game);
		Require(player->SetLocation(player->Location() + vec3(0, 0, 24)), "cannot raise moving-save fixture");
		player->SetPhysics(PHYS_Falling);
		player->Velocity() = vec3(64.0f, -32.0f, 120.0f);
		player->Acceleration() = vec3(8.0f, 4.0f, -2.0f);
		player->AnimFrame() = 0.375f;
		auto* proxy = UObject::Cast<UActor>(player->GetUObject("AnimProxy"));
		proxy->AnimFrame() = 0.625f;
		LogMessage("RUNE_SAVE_MOVING_BEGIN fixture=synthetic_falling physics=2 velocity=64,-32,120");
		RoundTrip(game, 90);
		Require(Player(game)->Physics() == PHYS_Falling, "moving fixture no longer falling");
		LogMessage("RUNE_SAVE_MOVING_OK physics=falling motion_and_animation_preserved=1");
		Load(game, 91);
		Verify(game, original, true, true, "restore moving fixture baseline");
	}
	static void TriggerExit(Engine& game, const char* actorName, const char* url)
	{
		UTeleporter* exit = nullptr;
		for (auto* actor : game.Level->Actors)
			if (actor && actor->Name == actorName) { exit = UObject::TryCast<UTeleporter>(actor); break; }
		Require(exit && exit->URL() == url && exit->bEnabled(), std::string("original exit absent/disabled: ") + actorName);
		Require(Player(game)->bCanTeleport(), "player cannot teleport");
		LogMessage("RUNE_TRAVEL_BEGIN map=" + Map(game) + " exit=" + actorName + " url=" + url + " fixture=original_touch_event");
		CallEvent(exit, EventName::Touch, {ExpressionValue::ObjectValue(Player(game))});
	}
	static fs::path ManifestPath(Engine& game, int slot)
	{
		return game.packages->GetSaveFolderPath() / ("Save" + std::to_string(slot) + ".probe.txt");
	}
	static void WriteManifest(Engine& game, int slot, const Snapshot& s)
	{
		std::ofstream out(ManifestPath(game, slot));
		out << "RUNE_SAVE_PROBE_2\n" << std::quoted(s.map) << ' ' << std::quoted(s.playerName) << ' ' << std::quoted(s.weapon) << ' ' << std::quoted(s.shield) << '\n';
		out << s.health << ' ' << s.maxHealth << ' ' << s.strength << ' ' << s.runePower << ' ' << s.shieldHealth << '\n';
		out << std::setprecision(9) << s.location.x << ' ' << s.location.y << ' ' << s.location.z << '\n';
		out << std::quoted(s.playerState) << ' ' << s.physics << ' ' << std::quoted(s.animation) << ' ' << s.animationFrame << ' ' << s.animationRate << '\n';
		out << s.velocity.x << ' ' << s.velocity.y << ' ' << s.velocity.z << ' ' << s.acceleration.x << ' ' << s.acceleration.y << ' ' << s.acceleration.z << '\n';
		out << std::quoted(s.proxyState) << ' ' << std::quoted(s.proxyAnimation) << ' ' << s.proxyFrame << ' ' << s.proxyRate << '\n';
		out << s.inventory.size() << '\n';
		for (const auto& item : s.inventory) out << std::quoted(item) << '\n';
		for (const auto& item : s.stowed) out << std::quoted(item) << '\n';
		out.flush();
		Require((bool)out, "cannot write fixture manifest");
	}
	static Snapshot ReadManifest(Engine& game, int slot)
	{
		std::ifstream in(ManifestPath(game, slot));
		std::string version;
		in >> version;
		Require(version == "RUNE_SAVE_PROBE_2", "missing/invalid fixture manifest for resume");
		Snapshot s;
		in >> std::quoted(s.map) >> std::quoted(s.playerName) >> std::quoted(s.weapon) >> std::quoted(s.shield);
		in >> s.health >> s.maxHealth >> s.strength >> s.runePower >> s.shieldHealth;
		in >> s.location.x >> s.location.y >> s.location.z;
		in >> std::quoted(s.playerState) >> s.physics >> std::quoted(s.animation) >> s.animationFrame >> s.animationRate;
		in >> s.velocity.x >> s.velocity.y >> s.velocity.z >> s.acceleration.x >> s.acceleration.y >> s.acceleration.z;
		in >> std::quoted(s.proxyState) >> std::quoted(s.proxyAnimation) >> s.proxyFrame >> s.proxyRate;
		size_t count = 0;
		in >> count;
		Require(count < 256, "invalid fixture inventory count");
		s.inventory.resize(count);
		for (auto& item : s.inventory) in >> std::quoted(item);
		s.stowed.resize(3);
		for (auto& item : s.stowed) in >> std::quoted(item);
		Require((bool)in, "truncated fixture manifest");
		return s;
	}
};
