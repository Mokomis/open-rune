
#include "Precomp.h"
#include "Math/MovementAcceleration.h"
#include "RuneFeedback.h"
#include "UActor.h"
#include "Packages/Engine/Actors/Brush/MovingBrush.h"
#include "VM/ScriptCall.h"
#include "Engine.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Actors/UProjectile.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"

void UActor::TickPhysics(float elapsed)
{
	for (float timeLeft = elapsed; timeLeft > 0.0f && !bDeleteMe(); timeLeft -= 0.02f)
	{
		float physTimeElapsed = std::min(timeLeft, 0.02f);
		int mode = Physics();
		if (mode != PHYS_None)
		{
			switch (mode)
			{
			case PHYS_Walking: TickWalking(physTimeElapsed); break;
			case PHYS_Falling: TickFalling(physTimeElapsed); break;
			case PHYS_Swimming: TickSwimming(physTimeElapsed); break;
			case PHYS_Flying: TickFlying(physTimeElapsed); break;
			case PHYS_Rotating: break;
			case PHYS_Projectile: TickProjectile(physTimeElapsed); break;
			case PHYS_Rolling: TickRolling(physTimeElapsed); break;
			case PHYS_Interpolating: TickInterpolating(physTimeElapsed); break;
			case PHYS_MovingBrush: TickMovingBrush(physTimeElapsed); break;
			case PHYS_Spider: TickSpider(physTimeElapsed); break;
			case PHYS_Trailer: TickTrailer(physTimeElapsed); break;
			}
			TickRotating(physTimeElapsed); // Rotation logic applies to multiple physics modes and not just PHYS_Rotating
		}

		CheckPendingTouch();
	}
}

void UActor::SetPhysics(uint8_t newPhysics)
{
	Physics() = newPhysics;
	LadderMode = false;
	// Severed Rune limbs spawn inside the pawn's collision bounds. InitBase
	// can consequently select that pawn before the Drop state starts falling.
	// Release the support link too; otherwise movement carries the free limb.
	// A later landing can still establish a real floor or moving-platform base.
	if (engine->LaunchInfo.IsRune() && newPhysics == PHYS_Falling && IsA("LimbWeapon"))
		SetBase(nullptr, true);
}

void UActor::SetCollision(bool newColActors, bool newBlockActors, bool newBlockPlayers)
{
	XLevel()->Collision.RemoveFromCollision(this);
	bCollideActors() = newColActors;
	bBlockActors() = newBlockActors;
	bBlockPlayers() = newBlockPlayers;
	XLevel()->Collision.AddToCollision(this);
}

bool UActor::SetLocation(const vec3& newLocation)
{
	auto result = CheckLocation(newLocation, CollisionRadius(), CollisionHeight(), bCollideWorld() || bCollideWhenPlacing());
	if (!result.first)
		return false;

	// Original FarMoveActor (Engine.dll 0x1038dcc0) clears every direct rider's
	// base after a successful relocation. Riders stay where they are; joint
	// attachments are not physics bases and are unaffected.
	if (engine->LaunchInfo.IsRune())
	{
		Array<UActor*> riders = BasedActors;
		for (UActor* rider : riders)
		{
			if (rider && rider->ActorBase() == this)
				rider->SetBase(nullptr, true);
		}
	}

	XLevel()->Collision.RemoveFromCollision(this);
	Location() = result.second;
	XLevel()->Collision.AddToCollision(this);

	// FarMoveActor also marks the actor teleported (Actor flag word 0x208, bit
	// 0x2000) and moves OldLocation, so physics that relocates mid-move, such
	// as a Teleporter touch, keeps its velocity instead of distance / tick.
	if (engine->LaunchInfo.IsRune())
	{
		bJustTeleported() = true;
		OldLocation() = Location();
	}

	if (Level()->bBegunPlay())
	{
		// Send touch notifications for anything at the new location
		for (UActor* actor : XLevel()->Collision.CollidingActors(Location(), CollisionHeight(), CollisionRadius()))
		{
			if (actor != this && !actor->IsBasedOn(this) && !IsBasedOn(actor) && bCollideActors() && actor->bCollideActors())
			{
				Touch(actor);
			}
		}

		// Untouch everything we aren't overlapping anymore
		if (engine->LaunchInfo.IsUnrealTournament_469())
		{
			for (const auto actor : Touching_UT469())
			{
				if (actor && !IsOverlapping(actor))
					UnTouch(actor);
			}
		}
		else
		{
			for (const auto actor : Touching())
			{
				if (actor && !IsOverlapping(actor))
					UnTouch(actor);
			}
		}
	}

	return true;
}

// Original FarMoveActor as reached from script SetLocation (bTest and
// bNoCheck zero): find a spot, then CheckEncroachment at it, which lets the
// mover's EncroachingOn refuse the move and blocked actors see EncroachedBy.
bool UActor::RuneFarMove(const vec3& newLocation)
{
	auto spot = CheckLocation(newLocation, CollisionRadius(), CollisionHeight(), bCollideWorld() || bCollideWhenPlacing());
	if (!spot.first)
		return false;
	if (RuneCheckEncroachment(spot.second, Rotation()) || bDeleteMe())
		return false;
	return SetLocation(spot.second);
}

bool UActor::SetRotation(const Rotator& newRotation)
{
	// To do: return false if there isn't room

	Rotator delta = newRotation - Rotation();
	// Polyobj can rotate from its script Tick as well as keyed interpolation.
	// Rehash after changing orientation so collision uses the rendered bounds.
	bool rehash = IsRunePolyobj(this) && Collision.Inserted;
	if (rehash) XLevel()->Collision.RemoveFromCollision(this);
	Rotation() = newRotation;
	if (rehash) XLevel()->Collision.AddToCollision(this);
	TurnBasedActors(delta);
	return true;
}

// carried items and actors on movers should rotate with the actor their based on
void UActor::TurnBasedActors(const Rotator& deltaRotation)
{
	TurnBasedActors(deltaRotation, Location());
}

void UActor::TurnBasedActors(const Rotator& deltaRotation, const vec3& pivot)
{
	if ((deltaRotation.Yaw & 0xffff) == 0)
		return;
	Coords yawRot = Coords::YawRotation(deltaRotation.YawRadians());
	vec3 baseLoc = pivot;
	for (size_t i = 0; i < BasedActors.size(); )
	{
		UActor* basedActor = BasedActors[i];
		if (!basedActor) { i++; continue; }
		vec3 basedLoc = basedActor->Location();
		vec3 rotatedOffset = yawRot * (basedLoc - baseLoc);
		basedActor->TryMove((baseLoc + rotatedOffset) - basedLoc, false, false);
		basedActor->SetRotation(basedActor->Rotation() + deltaRotation);
		if (UPawn* pawn = UObject::TryCast<UPawn>(basedActor))
			pawn->ViewRotation().Yaw += deltaRotation.Yaw;
		// UnrealScript events triggered in TryMove can call methods such as SetBase or Destroy, so need to guard while iterating.
		if (i < BasedActors.size() && BasedActors[i] == basedActor)
			i++;
	}
}

bool UActor::SetCollisionSize(float newRadius, float newHeight)
{
	// To do: return false if there isn't room

	XLevel()->Collision.RemoveFromCollision(this);
	CollisionRadius() = newRadius;
	CollisionHeight() = newHeight;
	XLevel()->Collision.AddToCollision(this);
	return true;
}

void UActor::PhysLanded(UActor* hitActor, const vec3& hitNormal)
{
	RuneFeedback::Landed(this,-Velocity().z);
	if (!hitActor)
		hitActor = Level();

    // Commit the aerial strike at the swept contact before the original
    // BaseChange/JumpOffPawn scripts can bounce the attacker away.
    if(auto* pawn=UObject::TryCast<UPawn>(this))RuneSlam::Landed(pawn,hitActor);

	// landed on the floor
	if (engine->LaunchInfo.IsRune())
		CallEvent(this, EventName::Landed, { ExpressionValue::VectorValue(hitNormal), ExpressionValue::ObjectValue(hitActor) });
	else CallEvent(this, EventName::Landed, { ExpressionValue::VectorValue(hitNormal) });

	if (Physics() == PHYS_Falling) // Landed event might have changed the physics mode
	{
		if (UObject::TryCast<UPawn>(this))
		{
			SetPhysics(PHYS_Walking);
			SetBase(hitActor, true);
		}
		else
		{
			SetPhysics(PHYS_None);
			SetBase(hitActor, true);
			Velocity() = vec3(0.0f);
		}
	}
}

bool UActor::HasLeftWorld()
{
	if (Region().ZoneNumber != 0)
		return false;

	CallEvent(this, EventName::FellOutOfWorld);
	return true;
}

void UActor::ApplyMovementAcceleration(float elapsed, float accelRate, float friction, float maxSpeed)
{
    ApplyMovementVelocity(Velocity(),Acceleration(),elapsed,accelRate,friction,maxSpeed);
}

bool UActor::ShouldAbortMovementTick(uint8_t expectedPhysics)
{
	return bDeleteMe() || Physics() != expectedPhysics;
}

UPawn* UActor::PreparePawnMovementTick()
{
	UPawn* pawn = UObject::TryCast<UPawn>(this);
	if (!pawn)
		return nullptr;

	if (HasLeftWorld())
		return nullptr;

	OldLocation() = Location();
	bJustTeleported() = false;

	return pawn;
}

void UActor::FireHitWall(const CollisionHit& hit)
{
	CallEvent(this, EventName::HitWall, { ExpressionValue::VectorValue(hit.Normal), ExpressionValue::ObjectValue(hit.Actor ? hit.Actor : Level()) });
}

bool UActor::TryStepToGround(vec3 stepDownDelta)
{
	CollisionHit floorHit = TryMove(stepDownDelta, true);

	//check if floo was reached, or if we would be falling now
	if (floorHit.Fraction == 1.0f || floorHit.Normal.z < 0.7071f)
	{
		//if we would be falling, move back up
		return false;
	}

	// We could reach the ground. Step down there.
	floorHit = TryMove(stepDownDelta);
	if (floorHit.Fraction != 1.0f)
		SetBase(floorHit.Actor, true);
	return true;
}

void UActor::RecomputeVelocityFromDisplacement(float elapsed)
{
	if (!bJustTeleported())
		Velocity() = (Location() - OldLocation()) / elapsed;
}

std::pair<bool, vec3> UActor::CheckLocation(vec3 location, float radius, float height, bool check)
{
	// Search for a valid spot near the location

	if (!check)
		return { true, location };

	// What is a reasonable size for this grid? what did UE1 do?
	int offset[] = { 0, 1, -1 };
	bool found = false;
	float scale = std::max(radius, height);
	for (int z = 0; z < 3 && !found; z++)
	{
		for (int y = 0; y < 3 && !found; y++)
		{
			for (int x = 0; x < 3 && !found; x++)
			{
				vec3 testlocation = location + vec3(offset[x] * scale, offset[y] * scale, offset[z] * scale);
				CollisionHitList hits = XLevel()->Collision.OverlapTest(testlocation, height, radius, false, true, false);
				if (hits.empty())
				{
					location = testlocation;
					found = true;
				}
			}
		}
	}
	return { found, location };
}

bool UActor::IsOverlapping(UActor* other)
{
	return XLevel()->Collision.IsOverlapping(this, other);
}

BoundingBox UActor::GetWorldCollisionBox(bool bVisual)
{
	LogUnimplemented("Actor.GetWorldCollisionBox");
	return {};
}

UObject* UActor::Trace(vec3& hitLocation, vec3& hitNormal, const vec3& traceEnd, const vec3& traceStart, bool bTraceActors, const vec3& extent)
{
	TraceFlags flags;
	flags.movers = true;
	flags.world = true;
	if (bTraceActors)
	{
		flags.pawns = true;
		flags.others = true;
		flags.onlyProjectiles = true;
	}

	// hack?
	if (IsA("ChallengeHUD"))
	{
		flags.zoneChanges = true;
	}

	CollisionHit hit = XLevel()->Collision.TraceFirstHit(traceStart, traceEnd, this, extent, flags);
	hitNormal = hit.Normal;
	hitLocation = traceStart + (traceEnd - traceStart) * hit.Fraction;
	return hit.Actor;
}

UObject* UActor::Trace(vec3& hitLocation, vec3& hitNormal, const vec3& traceEnd, const vec3& traceStart, bool bTraceActors, const vec3& extent, bool bTraceBSP, uint8_t BSPTraceFlags)
{
	LogUnimplemented("Actor.Trace() [U227 - BSPTraceFlags parameter isn't implemented");
	TraceFlags flags;
	flags.movers = true;
	flags.world = bTraceBSP;
	if (bTraceActors)
	{
		flags.pawns = true;
		flags.others = true;
		flags.onlyProjectiles = true;
	}

	// hack?
	if (IsA("ChallengeHUD"))
	{
		flags.zoneChanges = true;
	}

	CollisionHit hit = XLevel()->Collision.TraceFirstHit(traceStart, traceEnd, this, extent, flags);
	hitNormal = hit.Normal;
	hitLocation = traceStart + (traceEnd - traceStart) * hit.Fraction;
	return hit.Actor;
}

bool UActor::FastTrace(const vec3& traceEnd, const vec3& traceStart)
{
	return !XLevel()->Collision.TraceAnyHit(traceStart, traceEnd, this, false, true, false);
}

bool UActor::TraceSurfHitInfo(vec3& Start, vec3& End, vec3* HitLocation, vec3* HitNormal, UTexture* HitTex, int* HitFlags)
{
	const TraceFlags flags = {
		.movers = true,
		.world = true
	};

	const auto hit = XLevel()->Collision.TraceFirstHit(Start, End, this, vec3(), flags);

	if (!hit.Node)
		return false;

	if (HitLocation)
		*HitLocation = Start + (End - Start) * hit.Fraction;

	if (HitNormal)
		*HitNormal = hit.Normal;

	if (HitTex)
		HitTex = XLevel()->Model->Surfaces[hit.Node->Surf].Material;

	if (HitFlags)
		*HitFlags = hit.Node->NodeFlags;

	return true;
}

bool UActor::TraceThisActor(vec3& TraceEnd, vec3 TraceStart, vec3* HitLocation, vec3* HitNormal, std::optional<vec3> Extent)
{
	TraceFlags flags{
		.pawns = true,
		.movers = true,
		.others = true,
		.world = true
	};

	const auto hit = XLevel()->Collision.TraceFirstHit(TraceStart, TraceEnd, this, Extent ? *Extent : vec3(0.0f), flags);

	if (hit.Fraction == 1.0f)
		return false;

	if (HitLocation)
		*HitLocation = TraceStart + (TraceEnd - TraceStart) * hit.Fraction;

	if (HitNormal)
		*HitNormal = hit.Normal;

	return true;
}

CollisionHit UActor::TryMove(const vec3& delta, bool dryRun, bool isOwnBaseBlocking)
{
	// Static and non-movable objects can't move
	if (bStatic() || !bMovable())
	{
		CollisionHit hit;
		hit.Fraction = 0.0f;
		return hit;
	}

	if (engine->LaunchInfo.IsRune() && Brush())
		return dryRun ? CollisionHit() : TryMoveRuneBrush(delta, Rotation());

	// Avoid moving if movement is too small as the physics code doesn't like very small numbers
	if (dot(delta, delta) < 0.00000001f)
		return {};

	// Analyze what we will hit if we move as requested and stop if it is the level or a blocking actor
	bool useBlockPlayers = UObject::TryCast<UPlayerPawn>(this) || UObject::TryCast<UProjectile>(this);
	CollisionHit blockingHit;
	CollisionHitList hits;
	if (!Brush())
	{
		hits = XLevel()->Collision.Trace(Location(), Location() + delta, CollisionHeight(), CollisionRadius(), bCollideActors(), bCollideWorld(), false);
		if (bCollideWorld() || bBlockActors() || bBlockPlayers())
		{
			for (auto& hit : hits)
			{
				if (hit.Actor != Level())
				{
                    // A point-blank slam can start with overlapping pawn
                    // cylinders. An upward-facing exit contact is separating,
                    // not a ceiling: let the launch leave that overlap. Real
                    // incoming pawn contacts and all world/brush hits still block.
                    if(engine->LaunchInfo.IsRune() && delta.z>0 && dot(delta,hit.Normal)>0 && hit.Actor->IsA("Pawn"))
                        if(auto* pawn=UObject::TryCast<UPawn>(this);pawn && pawn->Slam.phase==1)continue;
					bool isBlocking;
					// Original MoveActor applies IsBlockedBy: a brush stops any
					// world-colliding actor, whatever that actor's own block flags.
					if (engine->LaunchInfo.IsRune() && hit.Actor->Brush())
						isBlocking = IsBlockedBy(hit.Actor);
					else if (useBlockPlayers || UObject::TryCast<UPlayerPawn>(hit.Actor) || UObject::TryCast<UProjectile>(hit.Actor))
						isBlocking = hit.Actor->bBlockPlayers() && bBlockPlayers();
					else
						isBlocking = hit.Actor->bBlockActors() && bBlockActors();

					// We never hit ourselves or anything moving along with us
					if (isBlocking && (isOwnBaseBlocking || !hit.Actor->IsBasedOn(this)) && !IsBasedOn(hit.Actor))
					{
						blockingHit = hit;
						break;
					}
				}
				else
				{
					blockingHit = hit;
					break;
				}
			}
		}
	}

	if (dryRun)
		return blockingHit;

	vec3 actuallyMoved = delta * blockingHit.Fraction;
	vec3 OldLocation = Location();

	XLevel()->Collision.RemoveFromCollision(this);
	Location() += actuallyMoved;
	XLevel()->Collision.AddToCollision(this);

	for (size_t i = 0; i < BasedActors.size(); )
	{
		UActor* basedActor = BasedActors[i];
		basedActor->TryMove(actuallyMoved, false, false);
		// UnrealScript events triggered in TryMove can call methods such as SetBase or Destroy, so need to guard while iterating.
		if (i < BasedActors.size() && BasedActors[i] == basedActor)
			i++;
	}

	// Notify actor of encroachment
	if (Brush() && (bBlockPlayers() || bBlockActors() || bCollideActors()))
	{
		Array<UActor*> encroachingActors = XLevel()->Collision.EncroachingActors(this);
		for (UActor* actor : encroachingActors)
		{
			if (actor == this || actor->Brush())
				continue;

			bool isBlocking;
			if (useBlockPlayers || UObject::TryCast<UPlayerPawn>(actor) || UObject::TryCast<UProjectile>(actor))
				isBlocking = actor->bBlockPlayers() && bBlockPlayers();
			else
				isBlocking = actor->bBlockActors() && bBlockActors();

			if (isBlocking)
			{
				bool stopMovement = CallEvent(this, EventName::EncroachingOn, { ExpressionValue::ObjectValue(actor) }).ToBool();
				if (stopMovement)
				{
					XLevel()->Collision.RemoveFromCollision(this);
					Location() = OldLocation;
					XLevel()->Collision.AddToCollision(this);

					CollisionHit hit;
					hit.Fraction = 0.0f;
					return hit;
				}
			}
		}

		for (UActor* actor : encroachingActors)
		{
			if (actor == this)
				continue;

			bool isBlocking;
			if (useBlockPlayers || UObject::TryCast<UPlayerPawn>(actor) || UObject::TryCast<UProjectile>(actor))
				isBlocking = actor->bBlockPlayers() && bBlockPlayers();
			else
				isBlocking = actor->bBlockActors() && bBlockActors();

			if (isBlocking)
				CallEvent(actor, EventName::EncroachedBy, { ExpressionValue::ObjectValue(this) }).ToBool();
		}
	}

	// Send bump notification if we hit an actor
	if (blockingHit.Actor && blockingHit.Actor != Level())
	{
		if (!blockingHit.Actor->IsBasedOn(this))
		{
			CallEvent(blockingHit.Actor, EventName::Bump, { ExpressionValue::ObjectValue(this) });
			CallEvent(this, EventName::Bump, { ExpressionValue::ObjectValue(blockingHit.Actor) });
		}
		// Rune: a move stopped by a collision joint of an actor with bJointsTouch
		// notifies that actor (e.g. a Tarp flexes and queues PostTouch to toss).
		if (engine->LaunchInfo.IsRune() && blockingHit.Joint >= 0 && blockingHit.Actor->GetBool("bJointsTouch") && !blockingHit.Actor->bDeleteMe())
			CallEvent(blockingHit.Actor, "JointTouchedBy", { ExpressionValue::ObjectValue(this), ExpressionValue::IntValue(blockingHit.Joint) });
	}

	// Send touch notifications for anything we crossed while moving
	for (auto& hit : hits)
	{
		if (hit.Fraction >= blockingHit.Fraction)
			break;

		if (hit.Actor != Level() && !hit.Actor->IsBasedOn(this) && !IsBasedOn(hit.Actor) && bCollideActors() && hit.Actor->bCollideActors())
		{
			// We can't touch stuff we are blocked by
			bool isBlocking;
			if (useBlockPlayers || UObject::TryCast<UPlayerPawn>(hit.Actor) || UObject::TryCast<UProjectile>(hit.Actor))
				isBlocking = hit.Actor->bBlockPlayers() && bBlockPlayers();
			else
				isBlocking = hit.Actor->bBlockActors() && bBlockActors();
			if (!isBlocking)
				Touch(hit.Actor);
		}
	}

	// Untouch everything we aren't overlapping anymore
	if (engine->LaunchInfo.IsUnrealTournament_469())
	{
		for (const auto actor : Touching_UT469())
			if (actor && !IsOverlapping(actor))
				UnTouch(actor);
	}
	else
	{
		for (const auto actor : Touching())
			if (actor && !IsOverlapping(actor))
				UnTouch(actor);
	}

	UpdateActorZone();

	return blockingHit;
}

// Original AActor::IsBlockedBy (Engine.dll 0x10329b70). Bit 1 of the flag
// word is bCollideWorld, bit 2 bBlockActors, bit 3 bBlockPlayers. The original
// player test is GetPlayerPawn; the port keeps its existing PlayerPawn cast.
bool UActor::IsBlockedBy(UActor* other)
{
	auto isBrush = [](UActor* actor) { return actor->Brush() && UObject::TryCast<UBrush>(actor); };
	if (other == Level())
		return bCollideWorld();
	if (isBrush(other))
		return bCollideWorld() && (UObject::TryCast<UPlayerPawn>(this) ? other->bBlockPlayers() : other->bBlockActors());
	if (isBrush(this))
		return other->bCollideWorld() && (UObject::TryCast<UPlayerPawn>(other) ? bBlockPlayers() : bBlockActors());
	bool selfUsesPlayers = UObject::TryCast<UPlayerPawn>(this) || UObject::TryCast<UProjectile>(this);
	bool otherUsesPlayers = UObject::TryCast<UPlayerPawn>(other) || UObject::TryCast<UProjectile>(other);
	return (selfUsesPlayers ? other->bBlockPlayers() : other->bBlockActors()) && (otherUsesPlayers ? bBlockPlayers() : bBlockActors());
}

// Original ULevel::CheckEncroachment (Engine.dll 0x1038eca0) as called by
// MoveActor: bTouchNotify is zero. Returns true when the move is rejected.
bool UActor::RuneCheckEncroachment(const vec3& testLocation, const Rotator& testRotation)
{
	UBrush* movingBrush = GetMovingBrush(this);
	if (!bCollideActors() && !bBlockActors() && !bBlockPlayers() && !movingBrush)
		return false;

	auto encroachedAt = [&]() {
		vec3 location = Location();
		Rotator rotation = Rotation();
		Location() = testLocation;
		Rotation() = testRotation;
		Array<UActor*> actors = XLevel()->Collision.EncroachingActors(this);
		Location() = location;
		Rotation() = rotation;
		return actors;
	};
	auto contains = [](const Array<UActor*>& actors, UActor* actor) {
		for (UActor* candidate : actors)
		{
			if (candidate == actor)
				return true;
		}
		return false;
	};

	// The rotation part of the push carries the actor rigidly about this
	// brush's origin. The original masks each component to 0xfffc first.
	vec3 rotationPivot = Location();
	Rotator deltaRotation(
		(testRotation.Pitch & 0xfffc) - (Rotation().Pitch & 0xfffc),
		(testRotation.Yaw & 0xfffc) - (Rotation().Yaw & 0xfffc),
		(testRotation.Roll & 0xfffc) - (Rotation().Roll & 0xfffc));
	bool rotates = testRotation != Rotation();
	mat4 rotationPush = Coords::Rotation(deltaRotation).ToMatrix();

	Array<UActor*> candidates = encroachedAt();
	for (UActor* other : candidates)
	{
		if (!other || other == this || other == Level() || other->bDeleteMe() || !IsBlockedBy(other))
			continue;

		// A moving brush first tries to push a non-brush actor out of the way
		// and only asks EncroachingOn if the actor is still in the test pose.
		if (movingBrush && !GetMovingBrush(other))
		{
			vec3 offset = other->Location() - rotationPivot;
			vec3 push = testLocation - Location();
			if (rotates)
				push += (rotationPush * vec4(offset, 0.0f)).xyz() - offset;

			other->RuneMoveSmooth(push);
			if (other->bDeleteMe())
				continue;
			if (!contains(encroachedAt(), other))
				continue;

			// Undo the failed push with this brush at its test location,
			// as the original does, then restore the real location.
			vec3 location = Location();
			XLevel()->Collision.RemoveFromCollision(this);
			Location() = testLocation;
			XLevel()->Collision.AddToCollision(this);
			other->RuneMoveSmooth(-push);
			XLevel()->Collision.RemoveFromCollision(this);
			Location() = location;
			XLevel()->Collision.AddToCollision(this);
			if (other->bDeleteMe())
				continue;
		}

		if (CallEvent(this, EventName::EncroachingOn, { ExpressionValue::ObjectValue(other) }).ToBool())
		{
			if (commandline->HasArg("", "--trace-encroachment"))
				LogMessage("RUNE_ENCROACH_REJECT brush=" + Name.ToString() + " other=" + other->Name.ToString() + " class=" + other->Class->Name.ToString() +
					" static=" + std::to_string((bool)other->bStatic()) + " movable=" + std::to_string((bool)other->bMovable()) +
					" base=" + (other->ActorBase() ? other->ActorBase()->Name.ToString() : std::string("None")));
			return true;
		}
		if (bDeleteMe())
			return true;
	}

	// The original notifies every blocked candidate from the first query,
	// including actors that were successfully pushed away.
	for (UActor* other : candidates)
	{
		if (other && other != this && other != Level() && !other->bDeleteMe() && IsBlockedBy(other))
			CallEvent(other, EventName::EncroachedBy, { ExpressionValue::ObjectValue(this) });
	}
	return false;
}

CollisionHit UActor::TryMoveRuneBrush(const vec3& delta, const Rotator& newRotation)
{
	if (bStatic() || !bMovable())
	{
		CollisionHit hit;
		hit.Fraction = 0.0f;
		return hit;
	}

	bool translates = dot(delta, delta) >= 0.00000001f;
	bool rotates = newRotation != Rotation();
	if (!translates && !rotates)
		return {};

	// Original MoveActor carries direct riders before it checks encroachment,
	// so a rider is only encroached if something stopped it from following.
	vec3 newLocation = Location() + (translates ? delta : vec3(0.0f));
	if (translates)
	{
		for (size_t i = 0; i < BasedActors.size(); )
		{
			UActor* basedActor = BasedActors[i];
			basedActor->TryMove(delta, false, false);
			// UnrealScript events triggered in TryMove can call methods such as SetBase or Destroy, so need to guard while iterating.
			if (i < BasedActors.size() && BasedActors[i] == basedActor)
				i++;
		}
	}
	if (rotates)
		TurnBasedActors(newRotation - Rotation(), newLocation);

	if (RuneCheckEncroachment(newLocation, newRotation) || bDeleteMe())
	{
		CollisionHit hit;
		hit.Fraction = 0.0f;
		return hit;
	}

	XLevel()->Collision.RemoveFromCollision(this);
	Location() = newLocation;
	Rotation() = newRotation;
	XLevel()->Collision.AddToCollision(this);

	for (const auto actor : Touching())
	{
		if (actor && !IsOverlapping(actor))
			UnTouch(actor);
	}

	UpdateActorZone();
	return {};
}

// Original AActor::TwoWallAdjust (Engine.dll 0x1030cef0).
static void TwoWallAdjust(const vec3& desiredDir, vec3& delta, const vec3& hitNormal, const vec3& oldHitNormal, float hitTime)
{
	if (dot(oldHitNormal, hitNormal) <= 0.0f)
	{
		// 90 degree or sharper corner: follow the crease between the walls.
		vec3 newDir = cross(hitNormal, oldHitNormal);
		float lengthSquared = dot(newDir, newDir);
		newDir = lengthSquared < 1e-8f ? vec3(0.0f) : newDir / std::sqrt(lengthSquared);
		delta = newDir * (dot(delta, newDir) * (1.0f - hitTime));
		if (dot(desiredDir, delta) < 0.0f)
			delta = delta * -1.0f;
	}
	else
	{
		delta = (delta - hitNormal * dot(delta, hitNormal)) * (1.0f - hitTime);
		if (dot(delta, desiredDir) <= 0.0f)
			delta = vec3(0.0f);
	}
}

// Original AActor::moveSmooth (Engine.dll 0x103b7b70): returns the first
// move's MoveActor result (any progress), not whether something was hit.
bool UActor::RuneMoveSmooth(const vec3& delta)
{
	CollisionHit hit = TryMove(delta);
	bool moved = hit.Fraction > 0.0f;
	if (hit.Fraction < 1.0f && !bDeleteMe())
	{
		vec3 adjusted = (delta - hit.Normal * dot(delta, hit.Normal)) * (1.0f - hit.Fraction);
		if (dot(delta, adjusted) >= 0.0f)
		{
			vec3 oldHitNormal = hit.Normal;
			float lengthSquared = dot(delta, delta);
			vec3 desiredDir = lengthSquared < 1e-8f ? vec3(0.0f) : delta / std::sqrt(lengthSquared);
			hit = TryMove(adjusted);
			if (hit.Fraction < 1.0f && !bDeleteMe())
			{
				FireHitWall(hit);
				if (!bDeleteMe())
				{
					TwoWallAdjust(desiredDir, adjusted, hit.Normal, oldHitNormal, hit.Fraction);
					TryMove(adjusted);
				}
			}
		}
	}
	return moved;
}

CollisionHit UActor::TryMoveSmooth(const vec3& delta)
{
	CollisionHit hit = TryMove(delta);
	if (hit.Fraction != 1.0f)
	{
		// We hit a slope. Try to follow it.
		vec3 alignedDelta = (delta - hit.Normal * dot(delta, hit.Normal)) * (1.0f - hit.Fraction);
		if (dot(delta, alignedDelta) >= 0.0f) // Don't end up going backwards
		{
			CollisionHit hit2 = TryMove(alignedDelta);
			return hit2; // XXX: does this break anything?
		}
	}

	return hit;
}
