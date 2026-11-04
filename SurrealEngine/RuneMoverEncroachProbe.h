#pragma once

#include "Engine.h"
#include "Package/Package.h"
#include "Package/PackageManager.h"
#include "Packages/Engine/Actors/Brush/MovingBrush.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Resources/Level/UPolys.h"
#include "VM/Frame.h"
#include "VM/ScriptCall.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Opt-in headless fixture for original moving-brush encroachment contracts
// (Engine.dll CheckEncroachment 0x1038eca0, IsBlockedBy 0x10329b70,
// moveSmooth 0x103b7b70, FarMoveActor 0x1038dcc0). Borrows a placed Polyobj's
// geometry, but creates and modifies only transient actors. No saves or assets.
// Every case is reported; the probe throws after all cases if any failed.
inline void RunRuneMoverEncroachProbe(Engine& game, ULevel* level)
{
    UBrush* placed = nullptr;
    std::vector<UBrush*> candidates;
    int movers = 0, polyobjs = 0, worldColliding = 0;
    for (auto* actor : level->Actors)
    {
        auto* brush = GetMovingBrush(actor);
        if (!brush) continue;
        if (IsRunePolyobj(brush)) ++polyobjs; else ++movers;
        if (brush->bCollideWorld()) ++worldColliding;
        if (!brush->Brush()) continue;
        auto* model = brush->Brush(); model->LoadNow();
        if (model->Polys) model->Polys->LoadNow();
        if (IsRunePolyobj(brush) && !model->Nodes.empty() && !model->LeafHulls.empty() && model->Polys && !model->Polys->Polys.empty())
            candidates.push_back(brush);
    }
    // Brush-vs-brush encroachment requires the moving brush's bCollideWorld in
    // the original pair rule. Report how often authored brushes enable it.
    std::cout << "RUNE_ENCROACH_INVENTORY movers=" << movers << " polyobjs=" << polyobjs
        << " collide_world=" << worldColliding << '\n';
    if (candidates.empty()) throw std::runtime_error("Encroachment probe needs a placed Polyobj with collision geometry");
    placed = candidates.front();

    auto* info = UObject::Cast<ULevelInfo>(level->Actors[0]);
    struct LevelContext
    {
        Engine& game; ULevel* previousLevel; ULevelInfo* previousInfo; ULevelInfo* testInfo; bool begun;
        ~LevelContext() { testInfo->bBegunPlay() = begun; game.Level = previousLevel; game.LevelInfo = previousInfo; }
    } context{game, game.Level, game.LevelInfo, info, bool(info->bBegunPlay())};
    game.Level = level; game.LevelInfo = info; info->bBegunPlay() = true;
    level->SetOwner(info);
    auto* transient = game.packages->GetTransientPackage();
    auto* engineClasses = game.packages->GetPackage("Engine");

    std::vector<UActor*> created;
    struct Cleanup
    {
        std::vector<UActor*>& actors;
        ~Cleanup()
        {
            for (auto* actor : actors)
            {
                actor->XLevel()->Collision.RemoveFromCollision(actor);
                if (auto* brush = UObject::TryCast<UBrush>(actor)) brush->RemoveFromBspNode();
            }
        }
    } cleanup{created};

    int counter = 0;
    auto rehash = [&](UActor* actor, const vec3& location) {
        level->Collision.RemoveFromCollision(actor); actor->Location() = location; level->Collision.AddToCollision(actor);
    };
    auto makeActor = [&](float radius, float height) {
        auto* a = UObject::Cast<UActor>(transient->NewObject("RuneEncroachActor" + std::to_string(counter++),
            engineClasses->GetClass("Actor"), ObjectFlags::Transient));
        a->XLevel() = level; a->Level() = info; a->bStatic() = false; a->bMovable() = true;
        a->CollisionRadius() = radius; a->CollisionHeight() = height;
        a->bCollideActors() = true; a->bBlockActors() = true; a->bBlockPlayers() = true; a->bCollideWorld() = true;
        a->Physics() = PHYS_None;
        created.push_back(a);
        return a;
    };
    auto makeBrush = [&](bool polyobj) {
        auto* b = UObject::Cast<UBrush>(transient->NewObject("RuneEncroachBrush" + std::to_string(counter++),
            polyobj ? placed->Class : engineClasses->GetClass("Mover"), ObjectFlags::Transient));
        b->XLevel() = level; b->Level() = info;
        b->Brush() = placed->Brush(); b->PrePivot() = placed->PrePivot(); b->MainScale() = placed->MainScale();
        b->Location() = placed->Location(); b->Rotation() = placed->Rotation();
        b->bStatic() = false; b->bMovable() = true; b->bHidden() = false; b->DrawType() = DT_Brush;
        b->bCollideActors() = true; b->bBlockActors() = true; b->bBlockPlayers() = true; b->bCollideWorld() = false;
        b->SetByte("KeyNum", 0); b->SetByte("PrevKeyNum", 0); b->SetByte("NumKeys", 2);
        b->SetByte(polyobj ? "PolyObjGlideType" : "MoverGlideType", 0);
        b->SetObject("Leader", b); b->SetInt("EncroachDamage", 0);
        auto* keyPos = static_cast<vec3*>(b->GetProperty("KeyPos"));
        auto* keyRot = static_cast<Rotator*>(b->GetProperty("KeyRot"));
        for (int i = 0; i < MovingBrushKeyCount(b); ++i) { keyPos[i] = vec3(0); keyRot[i] = Rotator(0,0,0); }
        if (polyobj)
        {
            auto* events = static_cast<NameString*>(b->GetProperty("InterpolateEvent"));
            for (int i = 0; i < 16; ++i) events[i] = "None";
        }
        level->Collision.AddToCollision(b);
        created.push_back(b);
        return b;
    };
    auto setMode = [](UBrush* b, int mode) { b->SetByte(IsRunePolyobj(b) ? "PolyObjEncroachType" : "MoverEncroachType", (uint8_t)mode); };
    // Original enum order for both classes: Stop, Return, Crush, Ignore.
    enum { Stop = 0, Return = 1, Crush = 2, Ignore = 3 };
    auto encroaches = [&](UBrush* b, UActor* a) {
        for (auto* hit : level->Collision.EncroachingActors(b)) if (hit == a) return true;
        return false;
    };
    auto encroachesAt = [&](UBrush* b, UActor* a, const vec3& location, const Rotator& rotation) {
        vec3 oldLocation = b->Location(); Rotator oldRotation = b->Rotation();
        b->Location() = location; b->Rotation() = rotation;
        bool result = encroaches(b, a);
        b->Location() = oldLocation; b->Rotation() = oldRotation;
        return result;
    };
    // Starts the real recovered InterpolateTo script toward key 1, then ticks a
    // quarter of the leg so the step equals keyPos/keyRot divided by four.
    auto step = [&](UBrush* b, const vec3& translation, const Rotator& rotation) {
        auto* keyPos = static_cast<vec3*>(b->GetProperty("KeyPos"));
        auto* keyRot = static_cast<Rotator*>(b->GetProperty("KeyRot"));
        b->SetVector("BasePos", b->Location()); b->SetRotator("BaseRot", b->Rotation());
        b->SetByte("KeyNum", 0); b->SetByte("PrevKeyNum", 0);
        keyPos[0] = vec3(0); keyRot[0] = Rotator(0,0,0);
        keyPos[1] = translation * 4.0f; keyRot[1] = Rotator(rotation.Pitch * 4, rotation.Yaw * 4, rotation.Roll * 4);
        Array<ExpressionValue> args = {ExpressionValue::IntValue(1), ExpressionValue::FloatValue(1.0f)};
        Frame::Call(FindEventFunction(b, "InterpolateTo"), b, {args.data(), args.size()});
        b->TickMovingBrush(0.25f);
    };

    // Find vertical faces of the borrowed geometry (horizontal normal). Pushing
    // across a vertical face keeps cylinder clearance a simple radius test.
    struct Face { vec3 center, normal; };
    std::vector<Face> faces;
    auto computeFaces = [&]() {
        faces.clear();
        mat4 transform = GetMovingBrushTransform(placed);
        mat4 rotation = Coords::Rotation(placed->Rotation()).ToMatrix();
        for (const auto& poly : placed->Brush()->Polys->Polys)
        {
            if (poly.Vertices.size() < 3) continue;
            vec3 center(0); for (auto v : poly.Vertices) center += v;
            center /= (float)poly.Vertices.size();
            vec3 normal = normalize((rotation * vec4(poly.Normal / placed->MainScale().Scale, 0)).xyz());
            if (std::abs(normal.z) > 0.05f) continue;
            faces.push_back({(transform * vec4(center, 1)).xyz(), normal});
        }
    };

    const float radius = 12, height = 12, stepSize = 8;
    int failures = 0, cases = 0, rotationSkips = 0;
    auto report = [&](const std::string& name, bool ok, const std::string& detail) {
        ++cases; if (!ok) ++failures;
        std::cout << (ok ? "RUNE_ENCROACH_PASS " : "RUNE_ENCROACH_FAIL ") << name << ' ' << detail << '\n';
    };
    auto fmt = [](const vec3& v) {
        return "(" + std::to_string(v.x) + "," + std::to_string(v.y) + "," + std::to_string(v.z) + ")";
    };

    // Arrange a blocker just outside a face so that a translation step of the
    // brush along the face normal overlaps it, while the blocker can move freely.
    struct Setup { vec3 face, normal, blockerStart; bool valid = false; };
    auto arrange = [&](UBrush* b, UActor* blocker) {
        Setup setup;
        for (const auto& face : faces)
        {
            vec3 start = face.center + face.normal * (radius + 2.0f);
            rehash(blocker, start);
            if (encroaches(b, blocker)) continue;
            if (!encroachesAt(b, blocker, b->Location() + face.normal * stepSize, b->Rotation())) continue;
            if (blocker->TryMove(face.normal * (stepSize + 4.0f), true).Fraction != 1.0f) continue;
            setup = {face.center, face.normal, start, true};
            break;
        }
        return setup;
    };
    // A large blocking cylinder 1 unit behind the blocker pins it.
    auto pin = [&](UActor* blocker, const Setup& setup) {
        auto* wall = makeActor(40, 40);
        wall->bMovable() = false;
        rehash(wall, setup.blockerStart + setup.normal * (radius + 40 + 1));
        level->Collision.AddToCollision(wall);
        return wall;
    };
    auto remove = [&](UActor* actor) { level->Collision.RemoveFromCollision(actor); actor->Location() = vec3(0, 0, -1e6f); };

    // Use the first placed Polyobj whose vertical faces leave room to push.
    bool selected = false;
    for (auto* candidate : candidates)
    {
        placed = candidate; computeFaces();
        auto* b = makeBrush(true); auto* blocker = makeActor(radius, height);
        level->Collision.AddToCollision(blocker);
        selected = arrange(b, blocker).valid;
        remove(blocker); remove(b);
        if (selected) break;
    }
    if (!selected) throw std::runtime_error("No placed Polyobj leaves room for a push arrangement");
    std::cout << "RUNE_ENCROACH_GEOMETRY source=" << placed->Name.ToString() << " vertical_faces=" << faces.size() << '\n';

    for (bool polyobj : {true, false})
    {
        const std::string kind = polyobj ? "polyobj" : "mover";

        // Free blocker: original moveSmooth pushes it, the recheck clears, and
        // the brush completes its step without asking EncroachingOn.
        {
            auto* b = makeBrush(polyobj); setMode(b, Stop);
            auto* blocker = makeActor(radius, height); level->Collision.AddToCollision(blocker);
            Setup s = arrange(b, blocker);
            if (!s.valid) throw std::runtime_error("No free face arrangement for " + kind);
            vec3 start = b->Location();
            step(b, s.normal * stepSize, Rotator(0,0,0));
            vec3 moved = b->Location() - start, pushed = blocker->Location() - s.blockerStart;
            bool ok = length(moved - s.normal * stepSize) < 0.05f && b->bInterpolating() &&
                dot(pushed, s.normal) > stepSize - 0.5f && !encroaches(b, blocker);
            report(kind + "_free_push", ok, "brush_moved=" + fmt(moved) + " blocker_pushed=" + fmt(pushed) +
                " interpolating=" + std::to_string(b->bInterpolating()));
            remove(blocker); remove(b);
        }

        // Pinned blocker in each authored event mode. The push fails, the
        // reverse push restores the blocker, and the script result decides.
        for (int mode : {Stop, Return, Crush, Ignore})
        {
            static const char* names[] = {"stop", "return", "crush", "ignore"};
            auto* b = makeBrush(polyobj); setMode(b, mode);
            auto* blocker = makeActor(radius, height); level->Collision.AddToCollision(blocker);
            Setup s = arrange(b, blocker);
            if (!s.valid) throw std::runtime_error("No pinned face arrangement for " + kind);
            auto* wall = pin(blocker, s);
            vec3 start = b->Location();
            step(b, s.normal * stepSize, Rotator(0,0,0));
            vec3 moved = b->Location() - start;
            float restored = length(blocker->Location() - s.blockerStart);
            bool rejects = mode == Stop || mode == Return;
            bool ok = restored < 0.1f && (rejects ? length(moved) < 0.01f && !b->bInterpolating()
                                                  : length(moved - s.normal * stepSize) < 0.05f);
            report(kind + "_pinned_" + names[mode], ok, "brush_moved=" + fmt(moved) + " blocker_offset=" +
                std::to_string(restored) + " interpolating=" + std::to_string(b->bInterpolating()));
            remove(wall); remove(blocker); remove(b);
        }

        // Original pair rule for a brush encroacher: the other actor's
        // bCollideWorld and the brush's block flag decide, not the other
        // actor's own block flags.
        {
            auto* b = makeBrush(polyobj); setMode(b, Stop);
            auto* item = makeActor(radius, height);
            item->bBlockActors() = false; item->bBlockPlayers() = false; item->bCollideWorld() = true;
            level->Collision.AddToCollision(item);
            Setup s = arrange(b, item);
            if (!s.valid) throw std::runtime_error("No item arrangement for " + kind);
            step(b, s.normal * stepSize, Rotator(0,0,0));
            vec3 pushed = item->Location() - s.blockerStart;
            report(kind + "_nonblocking_worldcollider_pushed", dot(pushed, s.normal) > stepSize - 0.5f,
                "item_pushed=" + fmt(pushed));
            remove(item); remove(b);
        }
        {
            auto* b = makeBrush(polyobj); setMode(b, Stop);
            auto* ghost = makeActor(radius, height);
            ghost->bCollideWorld() = false;
            level->Collision.AddToCollision(ghost);
            Setup s = arrange(b, ghost);
            if (!s.valid) throw std::runtime_error("No ghost arrangement for " + kind);
            vec3 start = b->Location();
            step(b, s.normal * stepSize, Rotator(0,0,0));
            vec3 moved = b->Location() - start;
            float ghostMoved = length(ghost->Location() - s.blockerStart);
            report(kind + "_no_worldcollision_passes", length(moved - s.normal * stepSize) < 0.05f && b->bInterpolating() && ghostMoved < 0.01f,
                "brush_moved=" + fmt(moved) + " ghost_moved=" + std::to_string(ghostMoved));
            remove(ghost); remove(b);
        }

        // Rotation-only steps must also encroach, pushing or rejecting.
        for (bool pinned : {false, true})
        {
            auto* b = makeBrush(polyobj); setMode(b, Stop);
            auto* blocker = makeActor(radius, height); level->Collision.AddToCollision(blocker);
            Rotator turn(0, 0, 0); Setup chosen;
            for (const auto& face : faces)
            {
                vec3 start = face.center + face.normal * (radius + 2.0f);
                rehash(blocker, start);
                if (encroaches(b, blocker)) continue;
                for (int yaw : {256, -256, 512, -512, 1024, -1024, 2048, -2048, 4096, -4096})
                {
                    Rotator candidate(0, yaw, 0);
                    vec3 offset = face.center - b->Location();
                    vec3 swept = (Coords::Rotation(candidate).ToMatrix() * vec4(offset, 0)).xyz() - offset;
                    if (dot(swept, face.normal) < 4.0f || length(swept) > 40.0f) continue;
                    if (!encroachesAt(b, blocker, b->Location(), b->Rotation() + candidate)) continue;
                    if (blocker->TryMove(swept * 2.0f + face.normal * 2.0f, true).Fraction != 1.0f) continue;
                    turn = candidate; chosen = {face.center, face.normal, start, true};
                    break;
                }
                if (chosen.valid) break;
            }
            const std::string name = kind + (pinned ? "_rotation_pinned_stop" : "_rotation_free_push");
            if (!chosen.valid)
            {
                std::cout << "RUNE_ENCROACH_SKIP " << name << " no_rotation_arrangement=1\n";
                ++rotationSkips; remove(blocker); remove(b); continue;
            }
            UActor* wall = pinned ? pin(blocker, chosen) : nullptr;
            Rotator startRotation = b->Rotation();
            step(b, vec3(0), turn);
            Rotator turned = b->Rotation() - startRotation;
            float offset = length(blocker->Location() - chosen.blockerStart);
            // The original undo runs with the brush at its test location but
            // old rotation, so a rotation-only undo need not be exact.
            bool ok = pinned ? turned == Rotator(0,0,0) && !b->bInterpolating() && offset < 4.0f
                             : turned == turn && b->bInterpolating() && !encroaches(b, blocker);
            report(name, ok, "yaw=" + std::to_string(turned.Yaw) + "/" + std::to_string(turn.Yaw) +
                " blocker_offset=" + std::to_string(offset) + " interpolating=" + std::to_string(b->bInterpolating()));
            if (wall) remove(wall);
            remove(blocker); remove(b);
        }

        // A direct rider travels with its base and is never treated as an
        // obstacle by the brush that carries it.
        {
            auto* b = makeBrush(polyobj); setMode(b, Stop);
            auto* rider = makeActor(radius, height); level->Collision.AddToCollision(rider);
            Setup s = arrange(b, rider);
            if (!s.valid) throw std::runtime_error("No rider arrangement for " + kind);
            rider->SetBase(b, false);
            vec3 start = b->Location();
            step(b, s.normal * stepSize, Rotator(0,0,0));
            vec3 moved = b->Location() - start, carried = rider->Location() - s.blockerStart;
            bool ok = length(moved - s.normal * stepSize) < 0.05f && length(carried - moved) < 0.05f &&
                rider->ActorBase() == b && b->bInterpolating();
            report(kind + "_rider_carried", ok, "brush_moved=" + fmt(moved) + " rider_moved=" + fmt(carried));
            rider->SetBase(nullptr, false);
            remove(rider); remove(b);
        }

        // FarMoveActor contract: successful relocation clears direct riders'
        // bases without moving them; joint attachments and ownership remain.
        {
            auto* b = makeBrush(polyobj);
            auto* rider = makeActor(radius, height); level->Collision.AddToCollision(rider);
            auto* carried = makeActor(4, 4); carried->bCollideActors() = false; carried->bBlockActors() = false;
            carried->bBlockPlayers() = false;
            carried->Owner() = b; carried->SetObject("AttachParent", b);
            rehash(rider, faces.front().center + faces.front().normal * (radius + 2.0f));
            rider->SetBase(b, false);
            vec3 riderStart = rider->Location();
            bool relocated = b->SetLocation(b->Location() + vec3(0, 0, 32));
            bool ok = relocated && rider->ActorBase() == nullptr && b->BasedActors.empty() && b->StandingCount() == 0 &&
                length(rider->Location() - riderStart) < 0.01f && carried->Owner() == b && carried->GetUObject("AttachParent") == b;
            report(kind + "_relocation_clears_riders", ok, "relocated=" + std::to_string(relocated) + " rider_base=" +
                (rider->ActorBase() ? rider->ActorBase()->Name.ToString() : std::string("None")) +
                " based=" + std::to_string(b->BasedActors.size()));
            rider->SetBase(nullptr, false);
            remove(carried); remove(rider); remove(b);
        }
    }

    // Original MoveActor applies IsBlockedBy to traced hits too: an actor that
    // collides with the world is stopped by a blocking brush even when its own
    // block flags are off.
    for (bool polyobj : {true, false})
    {
        auto* b = makeBrush(polyobj);
        auto* item = makeActor(radius, height);
        item->bBlockActors() = false; item->bBlockPlayers() = false;
        level->Collision.AddToCollision(item);
        Setup s = arrange(b, item);
        if (!s.valid) throw std::runtime_error("No moving-item arrangement");
        CollisionHit hit = item->TryMove(-s.normal * 8.0f);
        report(std::string(polyobj ? "polyobj" : "mover") + "_worldcollider_moving_into_brush_blocked", hit.Fraction < 0.5f && hit.Actor == b,
            "fraction=" + std::to_string(hit.Fraction) + " actor=" + (hit.Actor ? hit.Actor->Name.ToString() : std::string("None")));
        remove(item); remove(b);
    }
    for (const char* name : {"Engine.Inventory", "Engine.Weapon", "Engine.Decoration", "Engine.Carcass", "Engine.Projectile", "Engine.Pawn", "RuneI.Ragnar", "RuneI.VikingShortSword", "RuneI.VikingShield"})
    {
        std::string text = name;
        auto* package = game.packages->GetPackage(text.substr(0, text.find('.')));
        auto* cls = package ? package->GetClass(text.substr(text.find('.') + 1)) : nullptr;
        if (!cls) continue;
        auto* d = cls->GetDefaultObject<UActor>();
        std::cout << "RUNE_ENCROACH_DEFAULTS class=" << name << " collide_actors=" << (bool)d->bCollideActors() << " collide_world=" << (bool)d->bCollideWorld()
            << " block_actors=" << (bool)d->bBlockActors() << " block_players=" << (bool)d->bBlockPlayers() << '\n';
    }

    // Failed relocation preserves riders and location. A world-colliding
    // carrier sent into solid space cannot find a spot.
    {
        auto* carrier = makeActor(radius, height); level->Collision.AddToCollision(carrier);
        auto* rider = makeActor(radius, height); level->Collision.AddToCollision(rider);
        rehash(carrier, faces.front().center + faces.front().normal * (radius + 2.0f));
        rehash(rider, carrier->Location() + vec3(0, 0, height * 2 + 1));
        rider->SetBase(carrier, false);
        vec3 start = carrier->Location();
        // Find a point where every CheckLocation grid candidate overlaps world.
        vec3 solid(0); bool foundSolid = false;
        for (int i = 0; i < 4000 && !foundSolid; ++i)
        {
            vec3 p = faces[i % faces.size()].center - faces[i % faces.size()].normal * (64.0f + 16.0f * (i / (int)faces.size()));
            bool all = true;
            for (int z = -1; z <= 1 && all; ++z) for (int y = -1; y <= 1 && all; ++y) for (int x = -1; x <= 1 && all; ++x)
                if (level->Collision.OverlapTest(p + vec3(x, y, z) * std::max(radius, height), height, radius, false, true, false).empty()) all = false;
            if (all) { solid = p; foundSolid = true; }
        }
        bool relocated = foundSolid ? carrier->SetLocation(solid) : true;
        bool ok = !relocated && rider->ActorBase() == carrier && carrier->BasedActors.size() == 1 &&
            length(carrier->Location() - start) < 0.01f;
        report("failed_relocation_keeps_riders", ok, "solid=" + std::to_string(foundSolid) + " relocated=" + std::to_string(relocated) +
            " based=" + std::to_string(carrier->BasedActors.size()));
        rider->SetBase(nullptr, false);
        remove(rider); remove(carrier);
    }

    // Script MoveSmooth returns the first move's progress (MoveActor's result),
    // not whether anything was hit.
    {
        auto* mover = makeActor(radius, height); level->Collision.AddToCollision(mover);
        const Face& f = faces.front();
        vec3 open = f.center + f.normal * (radius + 2.0f);
        rehash(mover, open);
        bool freeResult = mover->MoveSmooth(f.normal * 4.0f);
        rehash(mover, open);
        bool blockedResult = mover->MoveSmooth(-f.normal * 8.0f);
        report("movesmooth_return", freeResult && blockedResult,
            "free=" + std::to_string(freeResult) + " partial_then_blocked=" + std::to_string(blockedResult));
        remove(mover);
    }

    std::cout << "RUNE_ENCROACH_SUMMARY cases=" << cases << " failures=" << failures << " rotation_skips=" << rotationSkips << '\n';
    if (failures) throw std::runtime_error("Moving-brush encroachment contract failures: " + std::to_string(failures));
    std::cout << "RUNE_ENCROACH_OK cases=" << cases << '\n';
}
