#pragma once

#include "Engine.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include "VM/Frame.h"
#include "VM/ScriptCall.h"
#include <cmath>
#include <stdexcept>

// Opt-in traversal fixture. Uses an original placed Rope and original player
// Touch/PlayerMove/Jump scripts. Input fields are injected; this is not UI input.
class RuneRopeProbe
{
public:
    RuneRopeProbe() : active(commandline && commandline->HasArg("", "--probe-rope")) {}
    bool Active() const { return active && !complete; }
    void Tick(Engine& game, int frame)
    {
        if (!Active() || frame < 240) return;
        Require(commandline->HasArg("", "--save-directory"), "requires isolated --save-directory");
        auto* player = UObject::TryCast<UPlayerPawn>(game.viewport->Actor());
        Require(player && player->Health() > 0, "requires living player");
        if (player->GetStateName() != "PlayerWalking" && player->GetStateName() != "PlayerSwimming")
        {
            Require(frame < 3600, "player never reached controllable walking/swimming state");
            return;
        }
        Require(!player->ViewTarget(), "active cinematic/view target must finish before rope fixture");
        auto invoke = [](UActor* actor, const NameString& function, Array<ExpressionValue> args = {})
        {
            auto* method = FindEventFunction(actor, function);
            if (!method) throw std::runtime_error("Rope probe missing original function " + function.ToString());
            return Frame::Call(method, actor, {args.data(), args.size()});
        };
        UActor* rope = nullptr;
        vec3 anchor;
        vec3 originalPosition = player->Location();
        NameString originalState = player->GetStateName();
        uint8_t originalPhysics = player->Physics();
        Rotator originalRotation = player->Rotation(), originalViewRotation = player->ViewRotation();
        vec3 originalVelocity = player->Velocity(), originalAcceleration = player->Acceleration();
        // Choose an actual clear rope interval with enough headroom for a timed
        // climb; a map without one must fail instead of silently spawning a fake.
        for (auto* actor : game.Level->Actors)
        {
            if (!actor || !actor->IsA("Rope") || actor->GetBool("bActorAttached")) continue;
            invoke(actor, "ComputeClimbingEndpoints", {ExpressionValue::FloatValue(actor->Location().z)});
            vec3 bottom = actor->GetVector("RopeClimbBottom"), top = actor->GetVector("RopeClimbTop");
            if (top.z - bottom.z < 320) continue;
            vec3 candidate = actor->Location();
            candidate.z = std::max(bottom.z + player->CollisionHeight() + 32.0f, std::min(candidate.z, top.z - 200.0f));
            auto checked = player->CheckLocation(candidate, player->CollisionRadius(), player->CollisionHeight(), true);
            if (!checked.first || length(checked.second - candidate) > 1.0f) continue;
            rope = actor; anchor = candidate; break;
        }
        Require(rope, "no placed rope with clear 320-unit climb interval");
        LogMessage("RUNE_ROPE_FIXTURE map=" + game.LevelInfo->URL.Map + " actor=" + rope->Name.ToString() + " input=script_fields");
        auto prepare = [&](uint8_t physics)
        {
            player->GotoState("PlayerWalking", "Begin");
            player->SetPhysics(PHYS_Walking);
            player->SetBase(nullptr, true);
            Require(player->SetLocation(anchor), "cannot place player at rope interval");
            player->InitActorZone();
            Require(length(player->Location() - anchor) < 1.0f, "rope anchor was displaced by placement");
            player->Velocity() = vec3(0); player->Acceleration() = vec3(0);
            // Walking across a rope must not attach; only falling/swimming does.
            invoke(player, "Touch", {ExpressionValue::ObjectValue(rope)});
            Require(player->GetStateName() != "PlayerRopeClimbing" && !rope->GetBool("bActorAttached"), "grounded player incorrectly grabbed rope");
            player->SetPhysics(physics);
            invoke(player, "Touch", {ExpressionValue::ObjectValue(rope)});
            Require(player->GetStateName() == "PlayerRopeClimbing" && player->GetUObject("TheRope") == rope &&
                rope->GetUObject("AttachedActor") == player && rope->GetBool("bActorAttached"), "original Touch did not attach player");
        };
        auto climb = [&](int hz, uint8_t physics)
        {
            prepare(physics);
            float startZ = player->Location().z;
            float dt = 1.0f / hz;
            for (int step = 0; step < hz; ++step)
            {
                player->SetFloat("aForward", 6000.0f);
                player->SetFloat("aStrafe", 0); player->SetFloat("aTurn", 0); player->SetFloat("aLookUp", 0); player->SetFloat("aUp", 0);
                invoke(player, "PlayerMove", {ExpressionValue::FloatValue(dt)});
                Require(player->Physics() == PHYS_Flying, "rope PlayerMove did not select flying physics");
                player->TickFlying(dt);
                Require(player->GetStateName() == "PlayerRopeClimbing", "climb unexpectedly left rope state");
                if (step == 0) Require(player->Velocity().z > 0.1f, "flying physics discarded rope upward velocity");
            }
            float moved = player->Location().z - startZ;
            Require(moved > 10.0f, "forward input did not climb a meaningful distance");
            player->SetFloat("aForward", 0);
            invoke(player, "PlayerMove", {ExpressionValue::FloatValue(dt)});
            Require(std::abs(player->Velocity().z) < 0.001f, "released forward input did not stop rope climb");
            invoke(player, "Jump");
            Require(player->GetStateName() != "PlayerRopeClimbing" && !player->GetUObject("TheRope") &&
                !rope->GetBool("bActorAttached") && !rope->GetUObject("AttachedActor"), "original Jump did not detach rope");
            Require(player->Physics() == PHYS_Falling || player->Physics() == PHYS_Swimming, "rope Jump chose wrong physics");
            LogMessage("RUNE_ROPE_CLIMB_OK hz=" + std::to_string(hz) + " distance=" + std::to_string(moved) + " grounded_guard=1 attach=1 idle_stop=1 jump_detach=1");
            return moved;
        };
        float sixty = climb(60, PHYS_Falling);
        float oneTwenty = climb(120, PHYS_Swimming);
        Require(std::abs(sixty - oneTwenty) < std::max(sixty, oneTwenty) * 0.20f, "rope climb is strongly frame-rate dependent");
        Require(player->SetLocation(originalPosition), "could not restore pre-fixture player position");
        player->InitActorZone();
        player->GotoState(originalState, "Begin"); player->SetPhysics(originalPhysics);
        player->SetRotation(originalRotation); player->ViewRotation() = originalViewRotation;
        player->Velocity() = originalVelocity; player->Acceleration() = originalAcceleration;
        complete = true;
        LogMessage("RUNE_ROPE_PROBE_OK map=" + game.LevelInfo->URL.Map + " actor=" + rope->Name.ToString() + " rates=60,120 input=script_fields ui_verified=0");
    }
private:
    bool active = false, complete = false;
    static void Require(bool condition, const std::string& message)
    {
        if (!condition) { LogMessage("RUNE_ROPE_PROBE_FAILED " + message); throw std::runtime_error("Rope probe: " + message); }
    }
};
