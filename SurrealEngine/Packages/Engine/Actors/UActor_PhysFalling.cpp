
#include "Precomp.h"
#include "RuneLeg.h"
#include "UActor.h"
#include "VM/ScriptCall.h"
#include "Packages/Engine/Actors/Decoration/UDecoration.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Brush/UMover.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Engine.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"

void UActor::TickFalling(float elapsed)
{
	if (HasLeftWorld())
		return;

	UZoneInfo* zone = Region().Zone;
	UDecoration* decor = UObject::TryCast<UDecoration>(this);
	UPawn* pawn = UObject::TryCast<UPawn>(this);

	// UnrealScript property references
	vec3& acceleration = Acceleration();
	vec3& velocity = Velocity();
	float groundSpeed = 0.0f;

	if (pawn)
	{
		groundSpeed = pawn->GroundSpeed()*RuneLeg::SpeedScale(pawn);
		float maxAccel = engine->LaunchInfo.ue1Version > 219 ? pawn->AirControl() * pawn->AccelRate() : 0.0f;
		float accel = length(acceleration);
		if (accel > maxAccel)
			acceleration = normalize(acceleration) * maxAccel;
	}

	float gravityScale = 2.0f;
	float fluidFriction = 0.0f;

	if (decor && decor->bBobbing())
	{
		gravityScale = 1.0f;
	}
	else if (pawn && pawn->FootRegion().Zone->bWaterZone() && velocity.z < 0.0f)
	{
		fluidFriction = pawn->FootRegion().Zone->ZoneFluidFriction();
	}

	OldLocation() = Location();
	bJustTeleported() = false;

	float fluidFactor = 1.0f - fluidFriction * elapsed;
	vec3 accelVector = acceleration * 1.5f;
	vec3 gravityVector = gravityScale * zone->ZoneGravity();

	vec3 oldVelocity = velocity;
	vec3 newVelocity = oldVelocity * fluidFactor + (accelVector + gravityVector) * 0.5f * elapsed;

	// Limit air control to controlling which direction we are moving in the XY plane, but not increase the speed beyond the ground speed
	vec2 velocity2d = velocity.xy();
	vec2 newVelocity2d = newVelocity.xy();
	float curSpeedSquared = dot(velocity2d, velocity2d);
	if (pawn && curSpeedSquared >= (groundSpeed * groundSpeed) && dot(newVelocity2d, newVelocity2d) > curSpeedSquared)
	{
		float xySpeed = length(velocity2d);
		newVelocity = vec3(normalize(newVelocity2d) * xySpeed, newVelocity.z);
	}
	velocity = newVelocity;

	float timeLeft = elapsed;
	for (int iteration = 0; timeLeft > 0.0f && iteration < 5; iteration++)
	{
		if (ShouldAbortMovementTick(PHYS_Falling))
			break;

		float zoneTerminalVelocity = zone->ZoneTerminalVelocity();
		if (dot(velocity, velocity) > zoneTerminalVelocity * zoneTerminalVelocity)
		{
			velocity = normalize(velocity) * zoneTerminalVelocity;
			newVelocity = velocity;
		}

		vec3 moveDelta = (newVelocity + zone->ZoneVelocity() * elapsed * 25.0f) * timeLeft;
		vec3 dirNormal = normalize(newVelocity);
		if (pawn && pawn->TryGrabRuneEdge(2.0f)) return;

		CollisionHit hit = TryMove(moveDelta);
        if(pawn && pawn->Slam.phase && hit.Fraction<1 && commandline->HasArg("","--probe-slam-gesture"))
            LogMessage("RUNE_SLAM_COLLISION actor="+(hit.Actor?hit.Actor->Name.ToString():"None")+" normal="+std::to_string(hit.Normal.x)+","+std::to_string(hit.Normal.y)+","+std::to_string(hit.Normal.z)+" z="+std::to_string(Location().z));
		if (ShouldAbortMovementTick(PHYS_Falling)) break;
		// Static/immovable actors reject TryMove with fraction zero and no hit
		// actor. There is no wall event to dispatch in that case (HelLift).
		if (hit.Fraction < 1.0f && !hit.Actor) break;
		timeLeft -= timeLeft * hit.Fraction;

		if (hit.Fraction < 1.0f)
		{
			if (hit.Actor->IsA("Pawn"))
			{
				// So projectiles don't think they hit a wall.
			}
			else
			{
				FireHitWall(hit);
			}

			// HitWall can finish a Rune weapon's Settling state and stop its
			// physics. Do not dispatch Landed in the new Pickup state: its
			// inherited handler resets pitch and makes the resting weapon stand up.
			if (ShouldAbortMovementTick(PHYS_Falling)) return;

			// Hit the level
			if (bBounce())
			{
				vec3 reflectedDelta = reflect(moveDelta, hit.Normal);
				hit = TryMove(reflectedDelta);
			}
			else
			{
				if (hit.Normal.z < 0.7071f)
				{
					// We hit a slope. Try to follow it.
					vec3 alignedDelta = (moveDelta - hit.Normal * dot(moveDelta, hit.Normal)) * (1.0f - hit.Fraction);
					if (dot(moveDelta, alignedDelta) >= 0.0f) // Don't end up going backwards
					{
						hit = TryMove(alignedDelta);
						if (hit.Fraction < 1.0f && hit.Normal.z > 0.7071f)
						{
							PhysLanded(hit.Actor, hit.Normal);
							return;
						}
					}

					// adjust velocity along the slope
					if (!bBounce())
						RecomputeVelocityFromDisplacement(elapsed);

					timeLeft = 0.0f;
				}
				else
				{
					PhysLanded(hit.Actor, hit.Normal);
					timeLeft = 0.0f;
				}
			}
		}
	}
}
