#pragma once

#include "UMover.h"
#include "Engine.h"

// Rune Polyobj inherits Brush in script, not Mover. Keep the original Mover
// implementation and use Rune's separate property layout for its 16-key brushes.
inline bool IsRunePolyobj(UActor* actor)
{
    return actor && engine->LaunchInfo.IsRune() && actor->IsA("Polyobj");
}

inline UBrush* GetMovingBrush(UActor* actor)
{
    if (auto* mover = UObject::TryCast<UMover>(actor)) return mover;
    return IsRunePolyobj(actor) ? UObject::TryCast<UBrush>(actor) : nullptr;
}

inline mat4 GetMovingBrushTransform(UBrush* brush)
{
    return mat4::translate(brush->Location()) * Coords::Rotation(brush->Rotation()).ToMatrix()
        * mat4::scale(brush->MainScale().Scale) * mat4::translate(-brush->PrePivot());
}

inline int MovingBrushKeyCount(UBrush* brush)
{
    return UObject::TryCast<UMover>(brush) ? 8 : 16;
}

inline vec3 MovingBrushKeyPos(UBrush* brush, int key)
{
    key = clamp(key, 0, MovingBrushKeyCount(brush) - 1);
    if (auto* mover = UObject::TryCast<UMover>(brush)) return mover->KeyPos()[key];
    return static_cast<vec3*>(brush->GetProperty("KeyPos"))[key];
}

inline Rotator MovingBrushKeyRot(UBrush* brush, int key)
{
    key = clamp(key, 0, MovingBrushKeyCount(brush) - 1);
    if (auto* mover = UObject::TryCast<UMover>(brush)) return mover->KeyRot()[key];
    return static_cast<Rotator*>(brush->GetProperty("KeyRot"))[key];
}

inline bool MovingBrushDynamicLight(UBrush* brush)
{
    if (auto* mover = UObject::TryCast<UMover>(brush)) return mover->bDynamicLightMover();
    return brush->GetBool("bDynamicLightMover") || brush->GetBool("bDynamicLightPolyobj");
}

// Conservative local AABB of a world-axis cylinder box. Rune brushes may rotate
// around every axis; scaling alone misses collisions on tilted platforms.
inline vec3 MovingBrushLocalExtents(UBrush* brush, const vec3& worldExtents)
{
    mat4 inverseRotation = mat4::transpose(Coords::Rotation(brush->Rotation()).ToMatrix());
    vec3 x = (inverseRotation * vec4(worldExtents.x, 0, 0, 0)).xyz();
    vec3 y = (inverseRotation * vec4(0, worldExtents.y, 0, 0)).xyz();
    vec3 z = (inverseRotation * vec4(0, 0, worldExtents.z, 0)).xyz();
    vec3 scale = brush->MainScale().Scale;
    return vec3(std::abs(x.x) + std::abs(y.x) + std::abs(z.x),
        std::abs(x.y) + std::abs(y.y) + std::abs(z.y),
        std::abs(x.z) + std::abs(y.z) + std::abs(z.z))
        / vec3(std::abs(scale.x), std::abs(scale.y), std::abs(scale.z));
}
