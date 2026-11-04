#include "Precomp.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Package/Package.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Brush/MovingBrush.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Native/NActor.h"
#include "VM/ScriptCall.h"
#include <cmath>
#include <iostream>

// Deterministic fixture for the original FarMoveActor contract (Engine.dll
// 0x1038dcc0) as reached from script SetLocation: relocation sets
// bJustTeleported and OldLocation, and CheckEncroachment can refuse it.
// Uses DM-IceCavern's in-level Teleporter0 -> upperteletarget pair.
// Every case is reported; the probe throws after all cases if any failed.
void RunRuneRelocationProbe(Engine& game)
{
    auto url = game.GetDefaultURL("DM-IceCavern.run");
    url.AddOrReplaceOption("game=RuneI.RuneSinglePlayer");
    game.LoadMap(url); game.LoginPlayer();
    auto* player = game.viewport->Actor(); player->bShowMenu() = false;
    auto tick = [&](int frames) {
        for (int i = 0; i < frames; ++i)
        {
            constexpr float dt = 1.f / 120;
            game.LevelInfo->TimeSeconds() += dt; game.TotalTime += dt; game.Level->Tick(dt, false);
        }
    };
    tick(30);
    player->DisableEvent("PlayerTick");
    int failures = 0, cases = 0;
    auto report = [&](const std::string& name, bool ok, const std::string& detail) {
        ++cases; if (!ok) ++failures;
        std::cout << (ok ? "RUNE_RELOCATION_PASS " : "RUNE_RELOCATION_FAIL ") << name << ' ' << detail << '\n';
    };
    auto scriptSetLocation = [](UActor* actor, const vec3& location) {
        uint32_t word = 0; BitfieldBool result{&word, 1};
        NActor::SetLocation(actor, location, result);
        return result.Get();
    };
    auto fmt = [](const vec3& v) {
        return "(" + std::to_string(v.x) + "," + std::to_string(v.y) + "," + std::to_string(v.z) + ")";
    };
    UActor* source = nullptr; UActor* destination = nullptr; UActor* gate = nullptr;
    for (auto* a : game.Level->Actors)
    {
        if (!a) continue;
        if (a->Name == "Teleporter0") source = a;
        if (a->Name == "Teleporter2") destination = a;
        if (a->Name == "Mover0" && GetMovingBrush(a)) gate = a;
    }
    if (!source || !destination || !gate) throw std::runtime_error("relocation fixture: DM-IceCavern teleporters or gate missing");

    // A pawn walking into a teleporter keeps a walking velocity, instead of
    // the teleport distance divided by the tick.
    {
        bool placed = false; vec3 direction;
        for (int angle = 0; angle < 16 && !placed; ++angle)
        {
            float rad = angle * 3.14159265f / 8;
            direction = vec3(std::cos(rad), std::sin(rad), 0);
            vec3 start = source->Location() + direction * (source->CollisionRadius() + player->CollisionRadius() + 3.0f);
            start.z = source->Location().z + player->CollisionHeight() - source->CollisionHeight();
            placed = player->SetLocation(start) && length((player->Location() - start).xy()) < 1.0f;
        }
        if (!placed) throw std::runtime_error("relocation fixture: no approach to Teleporter0");
        player->SetPhysics(PHYS_Falling); player->Velocity() = vec3(0); player->Acceleration() = vec3(0);
        tick(60);
        vec3 before = player->Location();
        player->Velocity() = -direction * 400.0f; player->Acceleration() = -direction * 2000.0f;
        bool teleported = false; vec3 velocity;
        for (int i = 0; i < 30 && !teleported; ++i)
        {
            tick(1);
            if (length(player->Location() - before) > 1000.0f) { teleported = true; velocity = player->Velocity(); }
        }
        float speed = length(velocity);
        report("teleport_keeps_velocity", teleported && speed < 1000.0f,
            "teleported=" + std::to_string(teleported) + " speed=" + std::to_string(speed) + " at=" + fmt(player->Location()));
        player->Acceleration() = vec3(0); player->Velocity() = vec3(0);
    }

    // Script SetLocation marks the actor teleported and moves OldLocation too.
    {
        vec3 target = destination->Location() + vec3(0, 0, player->CollisionHeight());
        bool moved = scriptSetLocation(player, target);
        report("relocation_marks_teleport", moved && player->bJustTeleported() && length(player->OldLocation() - player->Location()) < 0.01f,
            "moved=" + std::to_string(moved) + " just_teleported=" + std::to_string((bool)player->bJustTeleported()) +
            " old_gap=" + std::to_string(length(player->OldLocation() - player->Location())));
        tick(30);
    }

    // Original Pawn.EncroachingOn: a non-player pawn may not relocate onto
    // another pawn; the player may, and the other pawn's EncroachedBy gibs it.
    {
        auto* cls = game.packages->GetPackage("RuneI")->GetClass("DarkViking");
        vec3 spot = player->Location() + vec3(160, 0, 0);
        auto found = player->CheckLocation(spot, 42, 22, true);
        auto* viking = UObject::Cast<UPawn>(player->Spawn(cls, {}, {}, found.second, Rotator(0, 0, 0)));
        if (!viking) throw std::runtime_error("relocation fixture: DarkViking spawn failed");
        tick(10);
        for (const char* event : {"Tick", "Timer", "SeePlayer", "HearNoise", "EnemyNotVisible"}) viking->DisableEvent(event);
        vec3 vikingStart = viking->Location();
        bool vikingMoved = scriptSetLocation(viking, player->Location());
        report("nonplayer_onto_pawn_refused", !vikingMoved && length(viking->Location() - vikingStart) < 0.01f && player->Health() > 0,
            "moved=" + std::to_string(vikingMoved) + " viking_offset=" + std::to_string(length(viking->Location() - vikingStart)) +
            " player_health=" + std::to_string(player->Health()));

        int before = viking->Health();
        bool playerMoved = scriptSetLocation(player, viking->Location());
        tick(2);
        report("player_onto_pawn_telefrags", playerMoved && (viking->bDeleteMe() || viking->Health() <= 0) && player->Health() > 0,
            "moved=" + std::to_string(playerMoved) + " viking_health=" + std::to_string(before) + "->" +
            std::to_string(viking->bDeleteMe() ? -1 : viking->Health()) + " player_health=" + std::to_string(player->Health()));
    }

    // Pawns never relocate into a brush (original Pawn.EncroachingOn).
    {
        vec3 start = player->Location();
        bool moved = scriptSetLocation(player, gate->Location());
        report("pawn_into_mover_refused", !moved && length(player->Location() - start) < 0.01f,
            "moved=" + std::to_string(moved) + " offset=" + std::to_string(length(player->Location() - start)));
    }

    std::cout << "RUNE_RELOCATION_SUMMARY cases=" << cases << " failures=" << failures << std::endl;
    if (failures) throw std::runtime_error("relocation fixture failures: " + std::to_string(failures));
    std::cout << "RUNE_RELOCATION_OK cases=" << cases << std::endl;
}
