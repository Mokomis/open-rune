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
#include "Light/WorldVisibilityCache.h"
#include <iostream>
#include <stdexcept>

// Opt-in headless fixture: borrow a placed brush's original geometry, but modify
// only transient actors. Does not call map BeginPlay, save, or change assets.
inline void RunRunePolyobjProbe(Engine& game, ULevel* level)
{
    UBrush* placed = nullptr;
    int polyobjects = 0, collisionModels = 0;
    for (auto* actor : level->Actors)
    {
        if (!IsRunePolyobj(actor)) continue;
        ++polyobjects;
        auto* brush = GetMovingBrush(actor);
        if (!brush || !brush->Brush()) continue;
        auto* model = brush->Brush(); model->LoadNow();
        if (model->Polys) model->Polys->LoadNow();
        if (!model->Nodes.empty() && !model->LeafHulls.empty() && model->Polys && !model->Polys->Polys.empty())
        {
            ++collisionModels;
            if (!placed) placed = brush;
        }
    }
    std::cout << "RUNE_POLYOBJ_INVENTORY count=" << polyobjects << " collision_models=" << collisionModels << '\n';
    if (!placed) throw std::runtime_error("Polyobj probe needs a placed brush with polygon and collision geometry");
    auto* info = UObject::Cast<ULevelInfo>(level->Actors[0]);
    struct LevelContext
    {
        Engine& game;
        ULevel* previousLevel;
        ULevelInfo* previousInfo;
        ULevelInfo* testInfo; bool begun;
        ~LevelContext() { testInfo->bBegunPlay()=begun; game.Level = previousLevel; game.LevelInfo = previousInfo; }
    } context{game, game.Level, game.LevelInfo, info, bool(info->bBegunPlay())};
    game.Level = level; game.LevelInfo = info; info->bBegunPlay()=true;
    level->SetOwner(info);
    auto* transient = game.packages->GetTransientPackage();
    auto* fixture = UObject::Cast<UBrush>(transient->NewObject("RunePolyobjFixture", placed->Class, ObjectFlags::Transient));
    fixture->XLevel() = level; fixture->Level() = info;
    fixture->Brush() = placed->Brush();
    fixture->Location() = placed->Location(); fixture->Rotation() = placed->Rotation();
    fixture->PrePivot() = placed->PrePivot(); fixture->MainScale() = placed->MainScale();
    fixture->bStatic() = false; fixture->bMovable() = true;
    fixture->bCollideActors() = true; fixture->bBlockActors() = true; fixture->bBlockPlayers() = true;
    fixture->bHidden() = false; fixture->DrawType() = DT_Brush;
    fixture->SetByte("NumKeys", 16); fixture->SetByte("KeyNum", 0); fixture->SetByte("PrevKeyNum", 0);
    fixture->SetVector("BasePos", fixture->Location()); fixture->SetRotator("BaseRot", fixture->Rotation());
    fixture->SetByte("PolyObjGlideType", 0);
    auto* keyPos = static_cast<vec3*>(fixture->GetProperty("KeyPos"));
    auto* keyRot = static_cast<Rotator*>(fixture->GetProperty("KeyRot"));
    for (int i = 0; i < 16; ++i) { keyPos[i] = vec3(0); keyRot[i] = Rotator(0,0,0); }
    auto* events = static_cast<NameString*>(fixture->GetProperty("InterpolateEvent"));
    for (int i = 0; i < 16; ++i) events[i] = "None";
    if (UObject::TryCast<UMover>(fixture) || GetMovingBrush(fixture) != fixture)
        throw std::runtime_error("Polyobj was incorrectly treated as a native Mover");
    level->Collision.AddToCollision(fixture);
    struct Cleanup
    {
        UBrush* brush;
        ~Cleanup() { brush->XLevel()->Collision.RemoveFromCollision(brush); brush->RemoveFromBspNode(); }
    } cleanup{fixture};
    auto contains = [](const auto& hits, UActor* actor)
    { for (const auto& hit : hits) if (hit.Actor == actor) return true; return false; };
    WorldVisibilityCache visibilityCache;
    int visibilityChecks=0, worldChecks=0;
    auto visibility = [&](const vec3& start, const vec3& end) {
        bool reference=level->Collision.TraceAnyHit(start,end,nullptr,false,true,true);
        visibilityCache.Begin();
        bool cached=visibilityCache.Trace(start,end,
            [&] { ++worldChecks;return level->Collision.TraceAnyHit(start,end,nullptr,false,true,true,TraceWorldPart::LevelOnly); },
            [&] { return level->Collision.TraceAnyHit(start,end,nullptr,false,true,true,TraceWorldPart::NonLevel); });
        visibilityCache.End();++visibilityChecks;
        if(reference!=cached)throw std::runtime_error("Cached world/mover visibility differs from complete ray");
    };
    auto ray = [&](const vec3& start, const vec3& end, float height = 0, float radius = 0)
    { visibility(start,end);return level->Collision.Trace(start, end, height, radius, true, true, false); };

    vec3 faceCenter, faceNormal;
    bool foundFace = false;
    mat4 transform = GetMovingBrushTransform(fixture);
    for (const auto& poly : fixture->Brush()->Polys->Polys)
    {
        if (poly.Vertices.size() < 3 || (poly.PolyFlags & PF_Invisible)) continue;
        vec3 center(0); for (auto v : poly.Vertices) center += v;
        center /= (float)poly.Vertices.size();
        vec3 normal = normalize((Coords::Rotation(fixture->Rotation()).ToMatrix() * vec4(poly.Normal / fixture->MainScale().Scale, 0)).xyz());
        center = (transform * vec4(center, 1)).xyz();
        if (contains(ray(center + normal * 64.0f, center - normal * 64.0f), fixture))
        { faceCenter = center; faceNormal = normal; foundFace = true; break; }
    }
    if (!foundFace) throw std::runtime_error("Placed Polyobj geometry never returned a ray collision");
    if (!contains(ray(faceCenter + faceNormal * 64.0f, faceCenter - faceNormal * 64.0f, 8, 8), fixture))
        throw std::runtime_error("Polyobj capsule-box sweep missed its visible surface");
    fixture->UpdateBspInfo();
    BBox expected = fixture->Brush()->BoundingBox.transform(transform);
    if (length(fixture->BspInfo.BoundingBox.center() - expected.center()) > 0.01f ||
        length(fixture->Collision.Location - expected.center()) > 0.01f)
        throw std::runtime_error("Polyobj collision/render bounds disagree");
    std::cout << "RUNE_POLYOBJ_GEOMETRY_OK source=" << placed->Name.ToString()
        << " nodes=" << fixture->Brush()->Nodes.size() << " polys=" << fixture->Brush()->Polys->Polys.size()
        << " ray=1 box_sweep=1 bounds=1\n";

    // Two distinct actors catch the former encroachment loop checking the brush's
    // visit counter instead of each candidate actor's counter.
    UActor* intruders[2];
    for (int i = 0; i < 2; ++i)
    {
        intruders[i] = UObject::Cast<UActor>(transient->NewObject(i == 0 ? "RunePolyobjOverlapA" : "RunePolyobjOverlapB",
            game.packages->GetPackage("Engine")->GetClass("Actor"), ObjectFlags::Transient));
        auto* a = intruders[i]; a->XLevel() = level; a->Level() = info;
        a->Location() = faceCenter - faceNormal; a->CollisionRadius() = 8; a->CollisionHeight() = 8;
        a->bCollideActors() = true; a->bBlockActors() = true;
        level->Collision.AddToCollision(a);
    }
    auto encroached = level->Collision.EncroachingActors(fixture);
    bool found[2] = {false, false};
    for (auto* actor : encroached) for (int i = 0; i < 2; ++i) if (actor == intruders[i]) found[i] = true;
    for (auto* actor : intruders) level->Collision.RemoveFromCollision(actor);
    if (!found[0] || !found[1]) throw std::runtime_error("Polyobj encroachment failed to report both overlapping actors");
    if (!contains(level->Collision.OverlapTest(faceCenter - faceNormal, 8, 8, false, true, false), fixture))
        throw std::runtime_error("World-only overlap missed Polyobj");
    std::cout << "RUNE_POLYOBJ_OVERLAP_OK candidates=2 reported=2 world_overlap=1\n";

    Rotator unrotated = fixture->Rotation();
    fixture->SetRotation(unrotated + Rotator(8192, 16384, 4096));
    fixture->UpdateBspInfo();
    expected = fixture->Brush()->BoundingBox.transform(GetMovingBrushTransform(fixture));
    if (length(fixture->Collision.Location - expected.center()) > 0.02f ||
        length(fixture->Collision.Extents - (expected.extents() + 0.1f)) > 0.02f)
        throw std::runtime_error("Rotation-only Polyobj left stale collision bounds");
    fixture->SetRotation(unrotated);
    std::cout << "RUNE_POLYOBJ_ROTATION_OK rotation_only_rehash=1\n";

    vec3 start = fixture->Location();
    Rotator startRot = fixture->Rotation();
    keyPos[15] = vec3(96, 48, 24); keyRot[15] = Rotator(0,4096,0);
    auto* interpolate = FindEventFunction(fixture, "InterpolateTo");
    if (!interpolate) throw std::runtime_error("Original Polyobj.InterpolateTo missing");
    Array<ExpressionValue> args = {ExpressionValue::IntValue(15), ExpressionValue::FloatValue(1.0f)};
    Frame::Call(interpolate, fixture, {args.data(), args.size()});
    fixture->TickMovingBrush(0.25f);
    if (length(fixture->Location() - (start + keyPos[15] * 0.25f)) > 0.02f || fixture->GetByte("KeyNum") != 15)
        throw std::runtime_error("Polyobj key15 motion failed quarter-step");
    fixture->TickMovingBrush(0.76f);
    if (length(fixture->Location() - (start + keyPos[15])) > 0.02f || fixture->Rotation() != startRot + keyRot[15] || fixture->bInterpolating() || fixture->GetByte("PrevKeyNum")!=15 ||
        length(fixture->GetVector("RealPosition")-fixture->Location())>0.02f)
        throw std::runtime_error("Polyobj key15 motion/completion failed");
    std::cout << "RUNE_POLYOBJ_MOTION_OK key=15 linear_quarter=1 endpoint=1 rotation=1 script_completion=1\n";

    // The second movement uses smooth glide and a different path, not a repeated
    // endpoint check, and must update the broadphase after leaving its old cells.
    start = fixture->Location();
    fixture->SetByte("PolyObjGlideType", 1);
    keyPos[0] = vec3(4096, 2048, 1024);
    args = {ExpressionValue::IntValue(0), ExpressionValue::FloatValue(1.0f)};
    Frame::Call(interpolate, fixture, {args.data(), args.size()});
    fixture->TickMovingBrush(0.25f);
    vec3 destination = fixture->GetVector("BasePos") + keyPos[0];
    if (length(fixture->Location() - (start + (destination - start) * 0.15625f)) > 0.05f)
        throw std::runtime_error("Polyobj smooth glide quarter-step failed");
    fixture->TickMovingBrush(0.76f);
    if (contains(ray(faceCenter + faceNormal * 64.0f, faceCenter - faceNormal * 64.0f), fixture))
        throw std::runtime_error("Moved Polyobj still collides at its old location");
    fixture->UpdateBspInfo();
    expected = fixture->Brush()->BoundingBox.transform(GetMovingBrushTransform(fixture));
    if (length(fixture->Collision.Location - expected.center()) > 0.02f ||
        length(fixture->BspInfo.BoundingBox.center() - expected.center()) > 0.02f)
        throw std::runtime_error("Moving Polyobj left stale render/collision bounds");
    std::cout << "RUNE_POLYOBJ_GLIDE_OK smooth_quarter=1 stale_collision=0 moved_bounds=1\n";
    // Repeat fixed endpoints through both occupied and vacated positions. The
    // immutable BSP memo must not conceal a moving occluder's current state.
    for(int i=0;i<12;++i) {
        level->Collision.RemoveFromCollision(fixture);
        fixture->Location()=i%2 ? start : destination;
        level->Collision.AddToCollision(fixture);
        visibility(faceCenter+faceNormal*64.f,faceCenter-faceNormal*64.f);
    }
    std::cout<<"RUNE_VISIBILITY_SPLIT_OK checks="<<visibilityChecks<<" static_traces="<<worldChecks<<" moving_occluder_updates=1\n";

    auto* legacy = UObject::Cast<UMover>(transient->NewObject("RuneLegacyMoverControl",
        game.packages->GetPackage("Engine")->GetClass("Mover"), ObjectFlags::Transient));
    legacy->XLevel() = level; legacy->Level() = info;
    legacy->Brush() = placed->Brush(); legacy->MainScale() = placed->MainScale();
    legacy->PrePivot() = placed->PrePivot(); legacy->Location() = placed->Location();
    legacy->Rotation() = placed->Rotation(); legacy->BasePos() = legacy->Location();
    legacy->BaseRot() = legacy->Rotation(); legacy->KeyNum() = 0; legacy->PrevKeyNum() = 0; legacy->NumKeys() = 2;
    legacy->KeyPos()[0] = vec3(0); legacy->KeyPos()[1] = vec3(40,20,10);
    legacy->KeyRot()[0] = Rotator(0,0,0); legacy->KeyRot()[1] = Rotator(0,0,0);
    legacy->bStatic() = false; legacy->bMovable() = true; legacy->bCollideActors() = false;
    legacy->MoverGlideType() = 0;
    args = {ExpressionValue::IntValue(1), ExpressionValue::FloatValue(1.0f)};
    Frame::Call(FindEventFunction(legacy, "InterpolateTo"), legacy, {args.data(), args.size()});
    legacy->TickMovingBrush(0.25f);
    if (length(legacy->Location() - (legacy->BasePos() + legacy->KeyPos()[1] * 0.25f)) > 0.02f ||
        MovingBrushKeyCount(legacy) != 8)
        throw std::runtime_error("Legacy Mover integration changed");
    legacy->TickMovingBrush(0.76f);
    if (length(legacy->Location() - (legacy->BasePos() + legacy->KeyPos()[1])) > 0.02f || legacy->bInterpolating() ||
        legacy->PrevKeyNum()!=1 || length(legacy->GetVector("RealPosition")-legacy->Location())>0.02f)
        throw std::runtime_error("Legacy Mover completion changed");
    std::cout << "RUNE_POLYOBJ_LEGACY_MOVER_OK keys=8 quarter=1 endpoint=1\n";

    // Original Engine.dll's physMovingBrush commits alpha only after movement,
    // completes at exactly 1 (not just overshoot), and clears interpolation
    // before calling the script. The original InterpolateEnd may start another
    // leg immediately; that new state must survive and receive the spare time.
    // Exercise the real recovered script on both brush classes, without saves.
    auto completionContract = [&](UBrush* brush) {
        auto* positions = static_cast<vec3*>(brush->GetProperty("KeyPos"));
        auto* rotations = static_cast<Rotator*>(brush->GetProperty("KeyRot"));
        auto* times = static_cast<float*>(brush->GetProperty("MoveTime"));
        const vec3 origin = brush->Location();
        const Rotator rotation = brush->Rotation();
        auto* function = FindEventFunction(brush, "InterpolateTo");
        auto begin = [&](int keys) {
            brush->Location() = origin; brush->Rotation() = rotation;
            brush->SetVector("BasePos", origin); brush->SetRotator("BaseRot", rotation);
            brush->SetByte("KeyNum", 0); brush->SetByte("PrevKeyNum", 0);
            brush->SetByte("NumKeys", keys); brush->SetInt("ClientUpdate", 0);
            brush->SetByte(IsRunePolyobj(brush) ? "PolyObjGlideType" : "MoverGlideType", 0);
            positions[0] = vec3(0); positions[1] = vec3(40,20,10); positions[2] = vec3(80,40,20);
            for (int i=0; i<3; ++i) { rotations[i]=Rotator(0,0,0); times[i]=2.0f; }
            Array<ExpressionValue> parameters = {ExpressionValue::IntValue(1), ExpressionValue::FloatValue(1.0f)};
            Frame::Call(function, brush, {parameters.data(), parameters.size()});
        };
        begin(2);
        brush->TickMovingBrush(0.0f);
        if (brush->PhysAlpha()!=0 || length(brush->Location()-origin)>0.02f || !brush->bInterpolating())
            throw std::runtime_error("Moving brush consumed a zero-length tick");
        brush->TickMovingBrush(1.0f);
        if (brush->bInterpolating() || brush->GetByte("PrevKeyNum")!=1 ||
            brush->GetInt("ClientUpdate")!=0 || brush->PhysAlpha()!=0 ||
            length(brush->Location()-(origin+positions[1]))>0.02f)
            throw std::runtime_error("Exact endpoint failed to dispatch moving-brush completion");
        brush->TickMovingBrush(0.25f);
        if (brush->GetInt("ClientUpdate")!=0 || length(brush->Location()-(origin+positions[1]))>0.02f)
            throw std::runtime_error("Completed moving brush dispatched twice or kept moving");

        begin(3);
        brush->TickMovingBrush(1.0f);
        if (!brush->bInterpolating() || brush->GetByte("KeyNum")!=2 || brush->GetByte("PrevKeyNum")!=1 ||
            brush->PhysAlpha()!=0 || brush->GetInt("ClientUpdate")!=1 ||
            length(brush->Location()-(origin+positions[1]))>0.02f)
            throw std::runtime_error("Completion overwrote the next leg started by the script");

        begin(3);
        brush->TickMovingBrush(1.25f);
        if (!brush->bInterpolating() || brush->GetByte("KeyNum")!=2 || brush->GetByte("PrevKeyNum")!=1 ||
            std::abs(brush->PhysAlpha()-0.125f)>0.00001f || brush->GetInt("ClientUpdate")!=1)
            throw std::runtime_error("Moving brush lost leftover time or reused the previous leg rate");
        brush->TickMovingBrush(1.75f);
        if (brush->bInterpolating() || brush->GetByte("PrevKeyNum")!=2 || brush->GetInt("ClientUpdate")!=0 ||
            length(brush->Location()-(origin+positions[2]))>0.02f)
            throw std::runtime_error("Chained moving brush failed to complete its final leg");
    };
    completionContract(fixture);
    completionContract(legacy);
    std::cout << "RUNE_MOVING_BRUSH_COMPLETION_OK classes=2 zero_tick=1 exact_endpoint=1 callback_reentry=1 leftover_rate=1 terminal_once=1\n";
}
