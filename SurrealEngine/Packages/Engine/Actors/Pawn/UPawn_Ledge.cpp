#include "Precomp.h"
#include "UPawn.h"
#include "UPlayerPawn.h"
#include "Packages/Engine/Actors/UProjectile.h"
#include "Engine.h"
#include "Packages/Engine/Actors/Brush/MovingBrush.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "VM/ScriptCall.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"

bool UPawn::TryGrabRuneEdge(float downwardHeightScale)
{
	static const NameString canGrab("bCanGrabEdges"), grabEvent("GrabEdge");
	if (!engine->LaunchInfo.IsRune() || !GetBool(canGrab) || bDeleteMe() || !XLevel()) return false;
	// Original helper 0x103b9640 temporarily sets bWarping while checking and placing the pawn.
	struct RestoreWarping { UPawn* pawn; bool value; ~RestoreWarping() { pawn->bWarping() = value; } } restore{this, bool(bWarping())};
	bWarping() = true;
	const vec3 origin = Location();
	const float radius = CollisionRadius(), height = CollisionHeight();
	const vec3 forward = (Coords::Rotation(Rotation()).ToMatrix() * vec4(1, 0, 0, 0)).xyz();
	auto trace = [&](const vec3& from, const vec3& to, bool hull)
	{
		// Rune's trace mask 6 selects world and moving brushes, not other pawns.
		for (const auto& hit : XLevel()->Collision.Trace(from, to, hull ? height : 0, hull ? radius : 0, true, true, false))
			if (hit.Actor && (hit.Actor == Level() || hit.Actor->IsA("Mover") || IsRunePolyobj(hit.Actor))) return hit;
		return CollisionHit();
	};
	const auto wall = trace(origin, origin + forward * 4.0f, true);
	if (wall.Fraction >= 1.0f || std::abs(wall.Normal.z) >= 0.2f) return false;
	vec3 inward(-wall.Normal.x, -wall.Normal.y, 0);
	if (dot(inward, inward) < 0.0001f) return false;
	inward = normalize(inward);
	const vec3 above = origin + vec3(0, 0, 2.0f * height);
	const vec3 over = above + inward * (1.5f * radius);
	if (trace(above, over, true).Fraction < 1.0f) return false;
	const vec3 below = over - vec3(0, 0, downwardHeightScale * height);
	const auto surface = trace(over, below, false);
	if (surface.Fraction >= 1.0f) return false;
	const auto floor = trace(over, below, true);
	if (floor.Fraction >= 1.0f || floor.Normal.z < 0.7f) return false;
	// Reject actor encroachment at the upper candidate before running script.
	for (const auto& hit : XLevel()->Collision.OverlapTest(over, height, radius, true, false, false))
	{
		auto* other = hit.Actor;
		if (!other || other == this || other->IsOwnedBy(this) || IsOwnedBy(other)) continue;
		const bool playerCollision = UObject::TryCast<UPlayerPawn>(this) ||
			UObject::TryCast<UPlayerPawn>(other) || UObject::TryCast<UProjectile>(other);
		if (playerCollision ? (other->bBlockPlayers() && bBlockPlayers()) :
			(other->bBlockActors() && bBlockActors())) return false;
	}
	const float distance = (over + (below - over) * surface.Fraction).z - origin.z;
	const vec3 destination(origin.x + inward.x * (radius + 4), origin.y + inward.y * (radius + 4), origin.z + distance + 8 + height);
	if (!XLevel()->Collision.OverlapTest(destination, height, radius, false, true, false).empty()) return false;
	// Original RunePlayer/ScriptPawn decide whether their current state permits
	// grabbing, validate the final placement, and own the animation/state change.
	const auto result = CallEvent(this, grabEvent, {ExpressionValue::FloatValue(distance), ExpressionValue::VectorValue(inward)});
	const bool grabbed = result.GetType() != ExpressionValueType::Nothing && result.ToBool();
	if (grabbed && commandline->HasArg("", "--probe-camera"))
		LogMessage("RUNE_LEDGE_GRAB actor=" + Name.ToString() + " distance=" + std::to_string(distance) + " state=" + GetStateName().ToString());
	return grabbed;
}
