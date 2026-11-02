
#include "Precomp.h"
#include "UActor.h"
#include "VM/ScriptCall.h"
#include "Packages/Core/UClass.h"
#include "Packages/Engine/Actors/Decoration/UDecoration.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Brush/UMover.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Keypoint/UInterpolationPoint.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Engine.h"
#include "RuneInterpolation.h"
#include "Utils/Exception.h"

static void TickRunePath(UActor* actor, float elapsed)
{
	actor->OldLocation()=actor->Location();
	float timeLeft=elapsed;
	int transitions=0;
	while(timeLeft>0 && actor->bInterpolating() && actor->PhysRate()!=0 && !actor->bDeleteMe())
	{
		auto* target=UObject::TryCast<UInterpolationPoint>(actor->Target());
		auto* next=target?target->Next():nullptr;
		if(!target || !next) break;
		float oldAlpha=actor->PhysAlpha();
		actor->Level()->TimeDilation()=mix(target->GameSpeedModifier(),next->GameSpeedModifier(),oldAlpha);
		auto* player=UObject::TryCast<UPlayerPawn>(actor);
		if(!player) player=UObject::TryCast<UPlayerPawn>(actor->Owner());
		if(player)
		{
			player->DesiredFlashScale()=mix(target->ScreenFlashScale(),next->ScreenFlashScale(),oldAlpha);
			player->DesiredFlashFog()=mix(target->ScreenFlashFog(),next->ScreenFlashFog(),oldAlpha);
			player->FlashScale()=vec3(player->DesiredFlashScale());
			player->FlashFog()=player->DesiredFlashFog();
			player->FovAngle()=mix(target->FovModifier(),next->FovModifier(),oldAlpha)*player->Class->GetDefaultObject<UPlayerPawn>()->FovAngle();
		}
		float advanced=oldAlpha+actor->PhysRate()*mix(target->RateModifier(),next->RateModifier(),oldAlpha)*timeLeft;
		if(next->GetBool("bInterpInstant")) { oldAlpha=1; advanced=1.01f; }
		float alpha=clamp(advanced,0.0f,1.0f);
		actor->PhysAlpha()=alpha;
		auto* prev=target->Prev();
		auto* after=next->Next();
		vec3 location; Rotator rotation;
		if(prev && after && length(target->Location()-next->Location())>16.0f)
		{
			bool through=target->GetBool("bSplineThruPoints");
			location=RunePathCurve(prev->Location(),target->Location(),next->Location(),after->Location(),alpha,through);
			Rotator a=RunePathRotation(prev->Rotation(),prev->Prev()?prev->Prev()->Rotation():Rotator(0,0,0));
			Rotator b=RunePathRotation(target->Rotation(),a);
			Rotator c=RunePathRotation(next->Rotation(),b);
			Rotator d=RunePathRotation(after->Rotation(),c);
			rotation=RunePathCurve(a,b,c,d,alpha,through);
		}
		else
		{
			location=mix(target->Location(),next->Location(),alpha);
			rotation=mix(target->Rotation(),next->Rotation(),alpha);
		}
		actor->TryMove(location-actor->Location());
		actor->SetRotation(rotation);
		if(auto* pawn=UObject::TryCast<UPawn>(actor)) pawn->ViewRotation()=actor->Rotation();
		bool forward=actor->PhysRate()>0 && advanced>1;
		bool backward=actor->PhysRate()<0 && advanced<0;
		if(!forward && !backward) break;
		timeLeft*=forward?(advanced-1)/(advanced-oldAlpha):(0-advanced)/(oldAlpha-advanced);
		actor->PhysAlpha()=forward?0.0f:1.0f;
		// Original Rune advances Target before callbacks. Events/pause/end flags
		// belong to the node reached, not the node the camera just departed.
		do
		{
			if(++transitions>1024) Exception::Throw("Rune interpolation path contains an instantaneous/skip cycle");
			target=forward?target->Next():target->Prev();
			actor->Target()=target;
		} while(target && target->bSkipNextPath());
		if(target)
		{
			CallEvent(target,EventName::InterpolateEnd,{ExpressionValue::ObjectValue(actor)});
			if(!actor->bDeleteMe()) CallEvent(actor,EventName::InterpolateEnd,{ExpressionValue::ObjectValue(actor->Target())});
		}
		// The original reverse-path branch also notifies the moving actor with None.
		if(backward && !actor->bDeleteMe()) CallEvent(actor,EventName::InterpolateEnd,{ExpressionValue::ObjectValue(nullptr)});
	}
	if(elapsed>0) actor->Velocity()=(actor->Location()-actor->OldLocation())/elapsed;
}

void UActor::TickInterpolating(float elapsed)
{
	if(engine->LaunchInfo.IsRune()) { TickRunePath(this,elapsed); return; }
	OldLocation() = Location();

	float timeLeft = elapsed;
	while (timeLeft > 0.0f)
	{
		if (PhysRate() == 0.0f || !bInterpolating())
			break;

		UInterpolationPoint* target = UObject::Cast<UInterpolationPoint>(Target());
		UInterpolationPoint* next = target ? target->Next() : nullptr;
		if (!target || !next)
			break;

		float physAlpha = PhysAlpha();

		if (auto pawn = UObject::TryCast<UPlayerPawn>(this))
		{
			if (engine->LaunchInfo.ue1Version > 219)
			{
				pawn->DesiredFlashScale() = mix(target->ScreenFlashScale(), next->ScreenFlashScale(), physAlpha);
				pawn->DesiredFlashFog() = mix(target->ScreenFlashFog(), next->ScreenFlashFog(), physAlpha);
				pawn->FovAngle() = mix(target->FovModifier(), next->FovModifier(), physAlpha) * Class->GetDefaultObject<UPlayerPawn>()->FovAngle();
				pawn->FlashScale() = vec3(pawn->DesiredFlashScale());
				pawn->FlashFog() = pawn->DesiredFlashFog();
			}
		}

		if (engine->LaunchInfo.ue1Version > 219)
			Level()->TimeDilation() = mix(target->GameSpeedModifier(), next->GameSpeedModifier(), physAlpha);

		float rateModifier = mix(target->RateModifier(), next->RateModifier(), physAlpha);
		float physRate = PhysRate() * rateModifier;
		if (physRate == 0.0f)
			break;

		bool interpolateStart = false, interpolateEnd = false;
		physAlpha += physRate * timeLeft;
		if (physRate < 0.0f && physAlpha < 0.0f)
		{
			timeLeft = physAlpha / physRate;
			physAlpha = 0.0f;
			interpolateStart = true;
		}
		else if (physRate > 0.0f && physAlpha > 1.0f)
		{
			timeLeft = (physAlpha - 1.0f) / physRate;
			physAlpha = 1.0f;
			interpolateEnd = true;
		}
		else
		{
			timeLeft = 0.0f;
		}

		UInterpolationPoint* prev = target->Prev();
		UInterpolationPoint* nextnext = next->Next();
		vec3 location;
		Rotator rotation;
		if (prev && nextnext)
		{
			location = spline(prev->Location(), target->Location(), next->Location(), nextnext->Location(), physAlpha);
			rotation = spline(prev->Rotation(), target->Rotation(), next->Rotation(), nextnext->Rotation(), physAlpha);
		}
		else
		{
			location = mix(target->Location(), next->Location(), physAlpha);
			rotation = mix(target->Rotation(), next->Rotation(), physAlpha);
		}

		PhysAlpha() = physAlpha;
		TryMove(location - Location());
		SetRotation(rotation);

		if (auto pawn = UObject::TryCast<UPawn>(this))
		{
			pawn->ViewRotation() = Rotation();
		}

		if (interpolateStart)
		{
			CallEvent(target, EventName::InterpolateEnd, { ExpressionValue::ObjectValue(this) });
			CallEvent(this, EventName::InterpolateEnd, { ExpressionValue::ObjectValue(target) });

			target = target->Prev();
			if (engine->LaunchInfo.ue1Version > 219)
			{
				while (target && target->bSkipNextPath())
					target = target->Prev();
			}

			Target() = target;
			PhysAlpha() = 1.0f;
		}
		else if (interpolateEnd)
		{
			CallEvent(target, EventName::InterpolateEnd, { ExpressionValue::ObjectValue(this) });
			CallEvent(this, EventName::InterpolateEnd, { ExpressionValue::ObjectValue(target) });

			target = target->Next();
			if (engine->LaunchInfo.ue1Version > 219)
			{
				while (target && target->bSkipNextPath())
					target = target->Next();
			}

			Target() = target;
			PhysAlpha() = 0.0f;
		}
	}

	if (elapsed > 0.0f)
		Velocity() = (Location() - OldLocation()) / elapsed;
}
