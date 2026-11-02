
#include "Precomp.h"
#include "Render/SpriteTexture.h"
#include "VisibleActor.h"
#include "VisibleFrame.h"
#include "VisibleBrush.h"
#include "VisibleMesh.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "VisibleSprite.h"
#include "VisibleRuneParticles.h"
#include "VisibleBloodlustHalo.h"
#include "VisibleSnow.h"
#include "RuneAttachmentVisibility.h"
#include "Utils/CommandLine.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Brush/MovingBrush.h"

void VisibleActor::Process(VisibleFrame* frame, UActor* actor)
{
	if (engine->LaunchInfo.IsRune())
	{
		// Replace only authored snowfall while the new weather is enabled.
		// Leave emitters alive so switching Off restores their normal behavior.
		if(RuneSnow::Mode.load()>0 && RuneSnow::weather.sky && actor->IsA("Snow"))return;
		if(actor->IsA("BloodlustStart"))return; // The quiet head halo replaces this flash.
		auto* player = engine->viewport->Actor();
		auto attachment = GetRuneAttachmentVisibility(actor, player);
		// Bloodlust eye flames are children of hidden eye gear. They must
		// inherit that visibility rather than shine through the player's head.
		if (attachment.HiddenParent) return;
		// Decorative attached particles cannot retain layered translucency
		// while their carrier uses coverage fading. Hide only those effects
		// in the crowded view; detached projectiles and world effects remain.
		if (attachment.AttachedToPlayer && RuneParticles::IsSystem(actor) &&
			player->GetBool("bClientSideAlpha") && player->GetFloat("ClientSideAlphaScale") < 1.0f &&
			commandline->GetArgInt("", "--rune-camera-coverage", 1) != 0) return;
	}
	if (actor->bCorona())
	{
		if (engine->LaunchInfo.IsDeusEx())
		{
			// There seems to be more to this check than simply the distance. But this will do for now.
			vec3 v = actor->Location() - frame->ViewLocation.xyz();
			float d = 2000.0f;
			if (dot(v, v) < d * d)
				frame->Coronas.push_back(actor);
		}
		else
		{
			frame->Coronas.push_back(actor);
		}
	}

	if (frame->ShadowView && actor->Style() == STY_AlphaBlend && actor->GetFloat("AlphaScale") < 1.0f && !(actor->HasProperty("bClientSideAlpha") && actor->GetBool("bClientSideAlpha"))) return;
	if (actor->bHidden())
		return;

	UActor* viewportActor = engine->viewport->Actor();
	bool isOwnedByViewport = actor->IsOwnedBy(viewportActor);

	bool behindView = frame->PortalDepth > 0;
	if (UPawn* pawn = UObject::TryCast<UPawn>(viewportActor))
	{
		if (pawn->bBehindView())
			behindView = true;
	}

	if ((behindView || !isOwnedByViewport) && actor->bOnlyOwnerSee())
		return;

	if (engine->LaunchInfo.ue1Version > 219)
	{
		if (!behindView && isOwnedByViewport && actor->bOwnerNoSee())
			return;
	}

	if (!behindView && actor == engine->CameraActor) // Hide CameraActor as if bOwnerNoSee was set for it
		return;

	if (!frame->Clipper.IsAABBVisible(actor->BspInfo.BoundingBox))
		return;

	EDrawType dt = (EDrawType)actor->DrawType();
	if ((int)dt == 8 && RuneParticles::IsSystem(actor))
	{
		Type = dt;
		Actor = actor;
		frame->Actors.push_back(*this);
	}
	else if ((dt == DT_Mesh || (engine->LaunchInfo.IsRune() && (int)dt == 6)) && GetRuneDisplayMesh(actor))
	{
		Type = DT_Mesh;
		Actor = actor;
		frame->Actors.push_back(*this);
	}
	else if ((dt == DT_Sprite || dt == DT_SpriteAnimOnce) && GetActorSpriteTexture(actor, engine->LaunchInfo.IsRune()))
	{
		Type = dt;
		Actor = actor;
		frame->Actors.push_back(*this);
	}
	else if (dt == DT_Brush && actor->Brush() && GetMovingBrush(actor))
	{
		Type = dt;
		Actor = actor;
		frame->Actors.push_back(*this);
	}
}

void VisibleActor::DrawOpaque(VisibleFrame* frame)
{
	if (Type == DT_Mesh)
	{
		VisibleMesh vismesh;
		bool translucent=vismesh.DrawMesh(frame, Actor, false, false);
		if (translucent || HasBloodlustHalo(frame,Actor))
		{
			vec3 v = Actor->Location() - frame->ViewLocation.xyz();
			frame->Translucents.emplace_back(*this, dot(v, v));
		}
	}
	else if (Type == DT_Sprite || Type == DT_SpriteAnimOnce || ((int)Type == 8 && engine->LaunchInfo.IsRune()))
	{
		// Assume all sprites are translucent for now
		vec3 v = Actor->Location() - frame->ViewLocation.xyz();
		frame->Translucents.emplace_back(*this, dot(v, v));
	}
	else if (Type == DT_Brush)
	{
		VisibleBrush visbrush;
		if (visbrush.Draw(frame, Actor, false))
		{
			vec3 v = Actor->Location() - frame->ViewLocation.xyz();
			frame->Translucents.emplace_back(*this, dot(v, v));
		}
	}
}

void VisibleActor::DrawTranslucent(VisibleFrame* frame)
{
	if ((int)Type == 8 && engine->LaunchInfo.IsRune())
	{
		DrawRuneParticles(frame, Actor);
	}
	else if (Type == DT_Mesh)
	{
		VisibleMesh vismesh;
		vismesh.DrawMesh(frame, Actor, false, true);
        DrawBloodlustHalo(frame,Actor);
	}
	else if (Type == DT_Sprite || Type == DT_SpriteAnimOnce)
	{
		VisibleSprite vissprite;
		vissprite.Draw(frame, Actor);
	}
	else if (Type == DT_Brush)
	{
		VisibleBrush visbrush;
		visbrush.Draw(frame, Actor, true);
	}
}
