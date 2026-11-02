
#include "Precomp.h"
#include "UActor.h"
#include "VM/ScriptCall.h"
#include "Packages/Engine/Actors/Decoration/UDecoration.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Brush/MovingBrush.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Engine.h"

void UActor::TickMovingBrush(float elapsed)
{
	OldLocation() = Location();

	UBrush* mover = GetMovingBrush(this);
	if (mover)
	{
		float timeLeft = elapsed;
		while (timeLeft > 0.0f)
		{
			if (!bInterpolating())
				break;

			if (PhysRate() <= 0.0f)
				break;

			float physAlpha = PhysAlpha();
			float physRate = PhysRate();

			physAlpha += physRate * timeLeft;
			if (physAlpha > 1.0f)
			{
				timeLeft = (physAlpha - 1.0f) / physRate;
				physAlpha = 1.0f;
			}
			else
			{
				timeLeft = 0.0f;
			}

			float t = physAlpha;
			// Zero-delay, continuously triggered Rune doors should visibly move
			// as soon as they open. Preserve closing easing, authored waits and
			// the movement of timed hatches, lifts and other puzzle mechanisms.
			const bool immediateDoor = engine->LaunchInfo.IsRune() && !IsRunePolyobj(mover) &&
				mover->GetStateName() == "TriggerControl" && mover->GetBool("bOpening") && mover->GetFloat("DelayTime") <= 0;
			if (!immediateDoor && mover->GetByte(IsRunePolyobj(mover) ? "PolyObjGlideType" : "MoverGlideType") == 1/*MV_GlideByTime*/)
				t = smoothstep(0.0f, 1.0f, t);

			int keyIndex = clamp((int)mover->GetByte("KeyNum"), 0, MovingBrushKeyCount(mover) - 1);
			vec3 oldpos = mover->GetVector("OldPos");
			vec3 basepos = mover->GetVector("BasePos");
			vec3 keypos = MovingBrushKeyPos(mover, keyIndex);
			Rotator oldrot = mover->GetRotator("OldRot");
			Rotator baserot = mover->GetRotator("BaseRot");
			Rotator keyrot = MovingBrushKeyRot(mover, keyIndex);

			vec3 deltapos = basepos + keypos - oldpos;
			vec3 targetPos = oldpos + deltapos * t;

			Rotator targetRotation = oldrot + (baserot + keyrot - oldrot) * t;

			// LogMessage("Moving brush: " + std::to_string(t) + " key=" + std::to_string(keyIndex) +" keypos=(" + std::to_string(keypos.x) + "," + std::to_string(keypos.y) + "," + std::to_string(keypos.z) + ")");

			if (TryMove(targetPos - Location()).Fraction == 1.0f)
			{
				SetRotation(targetRotation);
				PhysAlpha() = physAlpha;

				if (physAlpha == 1.0f)
				{
					bInterpolating() = false;
					CallEvent(this, EventName::InterpolateEnd, { ExpressionValue::ObjectValue(nullptr) });
				}
			}
		}
	}

	if (elapsed > 0.0f)
		Velocity() = (Location() - OldLocation()) / elapsed;
}
