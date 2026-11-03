
#include "Precomp.h"
#include "RenderSubsystem.h"
#include "RenderDevice/RenderDevice.h"
#include "GameWindow.h"
#include "VM/ScriptCall.h"
#include "Engine.h"
#include "VisibleFrame.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Collision/TopLevel/CollisionHit.h"
#include "Packages/Extension/Windows/UViewportWindow.h"
#include "Packages/Extension/Windows/TabGroup/URootWindow.h"

void RenderSubsystem::DrawScene()
{
	if (!engine->Level)
		return;

	engine->Level->Light.BeginFrame();
	TextureFrameCounter++;

	// Make sure all actors are at the right location in the BSP
	for (UActor* actor : engine->Level->Actors)
	{
		if (actor)
			actor->UpdateBspInfo();
	}


    Device->SetLightingCamera(engine->CameraLocation);
    if (engine->LaunchInfo.IsRune() && (Device->WorldFeatures & 512))
    {
        UActor* player = engine->viewport->Actor();
        if (player)
        {
            engine->Level->Light.UpdateLightList(player);
            std::vector<std::pair<float,UActor*>> candidates;
            const int budget=RuneShadows::Budget(Device->WorldFeatures);
            const int candidateLimit=budget>3?32:8;
            int count=0;
            for(UActor* light:player->TouchingLights.List)
            {
                if(++count>candidateLimit) break;
                float radius=light->WorldLightRadius();
                vec3 delta=light->Location()-player->Location(); float dist2=dot(delta,delta);
                if(radius<8 || dist2<64 || dist2>900.0f*900.0f) continue;
                auto effect=light->LightEffect();
                if(effect==4 || effect==8 || (effect>=12 && effect<=15) || effect==17) continue;
                float score=(light->LightBrightness()/255.0f)*VertexLight::LightDistanceFalloff(dist2/(radius*radius));
                for(auto* previous:ShadowLights) if(light==previous) {score*=1.3f;break;}
                if(score>0.005f) candidates.emplace_back(score,light);
            }
            std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.first>b.first;});
            int limit=std::min(int(candidates.size()),budget);
            if(commandline->HasArg("","--trace-shadows")){static int lastBudget=-1,lastLimit=-1;if(lastBudget!=budget||lastLimit!=limit){LogMessage("RUNE_SHADOW_BUDGET requested="+std::to_string(budget)+" candidates="+std::to_string(candidates.size())+" active="+std::to_string(limit));lastBudget=budget;lastLimit=limit;}}
            for(int slot=0;slot<RuneShadows::Capacity;++slot)
            {
                UActor* light=slot<limit?candidates[slot].second:nullptr;
                bool changed=ShadowLights[slot]!=light; ShadowLights[slot]=light;
                if(!light) continue;
                VisibleFrame shadow; float radius=std::min(light->WorldLightRadius(),1800.0f);
                shadow.ProcessShadow(light->Location(),player->Location()+vec3(0,0,12),radius);
                if(Device->BeginLightShadow(&shadow.Frame,light->Location(),engine->CameraLocation,radius,slot))
                {
                    shadow.DrawShadow(); Device->EndLightShadow(slot+1<limit);
                    if(changed && commandline->HasArg("","--trace-rune-combat"))
                        LogMessage("RUNE_DYNAMIC_SHADOW light="+light->Name.ToString()+" slot="+std::to_string(slot)+" count="+std::to_string(limit)+" surfaces="+std::to_string(shadow.OpaqueNodes.size())+" actors="+std::to_string(shadow.Actors.size())+" size=1024");
                }
            }
        }
    }
	mat4 worldToView = Coords::ViewToRenderDev().ToMatrix() * Coords::Rotation(engine->CameraRotation).Inverse().ToMatrix() * Coords::Location(engine->CameraLocation).ToMatrix();
	MainFrame.Process(engine->CameraLocation, worldToView, Coords::Rotation(engine->CameraRotation));
    // A bounded ground-contact footprint anchors nearby characters even when
    // other lights fill their directional shadow. No extra render pass.
    if(engine->LaunchInfo.IsRune() && (Device->WorldFeatures&512))
    {
        std::vector<std::pair<float,UPawn*>> contacts;
        for(const auto& visible:MainFrame.Actors) {
            auto* pawn=UObject::TryCast<UPawn>(visible.Actor);
            if(!pawn || pawn->bHidden() || pawn->bDeleteMe() || pawn->Health()<=0 ||
                pawn->Physics()==PHYS_Swimming || pawn->Physics()==PHYS_Flying) continue;
            vec3 delta=pawn->Location()-engine->CameraLocation;
            float dist2=dot(delta,delta);
            if(dist2<800.0f*800.0f) contacts.emplace_back(dist2,pawn);
        }
        std::stable_sort(contacts.begin(),contacts.end(),[](const auto& a,const auto& b){return a.first<b.first;});
        int slot=0;
        // At most four ground traces, independent of the size of the crowd.
        for(size_t i=0;i<std::min(contacts.size(),size_t(4));++i) {
            auto* pawn=contacts[i].second;
            vec3 start=pawn->Location();
            float height=pawn->CollisionHeight();
            vec3 end=start-vec3(0,0,height+24.0f);
            TraceFlags flags;flags.world=true;flags.movers=true;
            auto hit=engine->Level->Collision.TraceFirstHit(start,end,pawn,vec3(0),flags);
            if(hit.Fraction>=1.0f || hit.Normal.z<0.6f) continue;
            vec3 ground=start+(end-start)*hit.Fraction;
            float gap=std::max(start.z-height-ground.z,0.0f);
            float strength=0.48f*std::clamp(1.0f-gap/24.0f,0.0f,1.0f);
            float radius=std::clamp(pawn->CollisionRadius()*1.15f,12.0f,48.0f);
            Device->SetContactShadow(slot++,vec4(ground,1.0f/(radius*radius)),vec4(hit.Normal,strength));
        }
    }
    if(engine->LaunchInfo.IsRune() && (Device->WorldFeatures&2048))
    {
        bool found=false;float waterHeight=0,nearest=1e30f;
        auto considerWater=[&](const VisibleNode& node) {
            float height;
            if(node.WaterPlane(height) && engine->CameraLocation.z>height+1) {
                float distance=engine->CameraLocation.z-height;
                if(distance<nearest){found=true;nearest=distance;waterHeight=height;}
            }
        };
        for(const auto& translucent:MainFrame.Translucents)considerWater(translucent.Node);
        for(const auto& node:MainFrame.OpaqueNodes)considerWater(node);
        if(found && !(Device->WorldFeatures&8192)) {
            Device->PrepareApproximateWater(waterHeight,engine->LevelInfo->TimeSeconds());
            static int logged=0;
            if(logged++<3 && commandline->HasArg("","--trace-rune-combat"))
                LogMessage("RUNE_WATER_APPROXIMATE height="+std::to_string(waterHeight)+" scene_passes=0");
        }
        if(found && (Device->WorldFeatures&8192) && !Device->ReuseWaterReflection(waterHeight,engine->LevelInfo->TimeSeconds(),engine->CameraLocation)) {
            mat4 mirror=mat4::translate(0,0,waterHeight)*mat4::mirror(vec3(0,0,1))*mat4::translate(0,0,-waterHeight);
            mat4 reflectedView=worldToView*mirror;
            VisibleFrame reflection;
            vec3 cam=(mirror*vec4(engine->CameraLocation,1)).xyz();
            reflection.Process(cam,reflectedView,Coords::Rotation(engine->CameraRotation)*Coords::FromMatrix(mat4::mirror(vec3(0,0,1))).Inverse(),true,1,{},vec4(0,0,1,-waterHeight));
            // Rigid inverse transpose transforms a world clipping plane to view.
            vec3 n=(reflectedView*vec4(0,0,1,0)).xyz();
            reflection.Frame.NearClip=vec4(n,cam.z-waterHeight-0.1f);
            if(Device->BeginWaterReflection(&reflection.Frame,waterHeight,engine->LevelInfo->TimeSeconds())) {
                reflection.Draw(); Device->EndWaterReflection();
                static int logged=0;
                if(logged++<3 && commandline->HasArg("","--trace-rune-combat"))LogMessage("RUNE_WATER_REFLECTION height="+std::to_string(waterHeight)+" surfaces="+std::to_string(reflection.OpaqueNodes.size())+" actors="+std::to_string(reflection.Actors.size()));
            }
        }
    }
	MainFrame.Draw();
	MainFrame.DrawCoronas();
}

void RenderSubsystem::DrawViewport(UViewportWindow* viewport)
{
	if (!engine->Level)
		return;

	float x = 0.0f, y = 0.0f;
	engine->dxRootWindow->ConvertCoordinates(viewport, 0.0f, 0.0f, engine->dxRootWindow, x, y);
	engine->dxRootWindow->SetRenderViewport(x, y, viewport->Width(), viewport->Height());

	if (viewport->bClearZ())
		Device->ClearZ();

	bool originActorWasHidden = false;
	vec3 location;
	Rotator rotation(0,0,0);
	if (UActor* originActor = viewport->originActor())
	{
		originActorWasHidden = originActor->bHidden();
		originActor->bHidden() = true;
		location = originActor->Location() + viewport->relLocation();
		if (viewport->bUseEyeHeight())
		{
			if (auto pawn = UObject::TryCast<UPawn>(originActor))
				location.z += pawn->BaseEyeHeight();
		}
		rotation = originActor->Rotation();
	}
	else
	{
		location = viewport->Location() + viewport->relLocation();
		rotation = viewport->Rotation();
	}

	if (viewport->bUseViewRotation())
	{
		rotation = engine->CameraRotation;
	}
	else if (UActor* watchActor = viewport->watchActor())
	{
		vec3 lookAt = watchActor->Location();
		if (viewport->bWatchEyeHeight())
		{
			if (auto pawn = UObject::TryCast<UPawn>(watchActor))
				lookAt.z += pawn->BaseEyeHeight();
		}
		rotation = Rotator::FromVector(lookAt - location);
	}

	rotation += viewport->relRotation();

	mat4 worldToView = Coords::ViewToRenderDev().ToMatrix() * Coords::Rotation(rotation).Inverse().ToMatrix() * Coords::Location(location).ToMatrix();
	MainFrame.Process(location, worldToView, Coords::Rotation(rotation));
	MainFrame.Draw();
	MainFrame.DrawCoronas();

	Device->SetSceneNode(&Canvas.Frame);

	engine->dxRootWindow->ResetRenderViewport();

	if (UActor* originActor = viewport->originActor())
		originActor->bHidden() = originActorWasHidden;
}
