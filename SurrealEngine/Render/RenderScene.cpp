
#include "Precomp.h"
#include "RenderSubsystem.h"
#include "RenderDevice/RenderDevice.h"
#include "GameWindow.h"
#include "VM/ScriptCall.h"
#include "Engine.h"
#include "VisibleFrame.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include "Math/hsb.h"
#include "Packages/Engine/Resources/UPalette.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
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
    if(engine->LaunchInfo.IsRune())engine->Level->Model->UpdateRenderActorBounds();
    // Build visibility once, before shadow selection: scenery lights must be
    // selected from their receivers, not only lights reaching the player pawn.
    mat4 worldToView = Coords::ViewToRenderDev().ToMatrix() * Coords::Rotation(engine->CameraRotation).Inverse().ToMatrix() * Coords::Location(engine->CameraLocation).ToMatrix();
    MainFrame.Process(engine->CameraLocation, worldToView, Coords::Rotation(engine->CameraRotation));
    auto* cameraZone=engine->GetZoneActor(MainFrame.ViewZone);
    Device->SetWaterView(cameraZone && cameraZone->bWaterZone(),engine->LevelInfo->TimeSeconds());
    if (engine->LaunchInfo.IsRune() && (Device->WorldFeatures & 512))
    {
        UActor* player = engine->viewport->Actor();
        if (player)
        {
            engine->Level->Light.UpdateLightList(player);
            std::vector<std::pair<float,UActor*>> candidates;
            const int budget=RuneShadows::Budget(Device->WorldFeatures);
            for(UActor* light:player->TouchingLights.List)
            {
                float radius=light->WorldLightRadius();
                vec3 delta=light->Location()-player->Location(); float dist2=dot(delta,delta);
                if(radius<8 || dist2<64 || dist2>900.0f*900.0f) continue;
                auto effect=light->LightEffect();
                if(effect==4 || effect==8 || (effect>=12 && effect<=15) || effect==17) continue;
                float score=(light->LightBrightness()/255.0f)*VertexLight::LightDistanceFalloff(dist2/(radius*radius));
                for(auto* previous:ShadowLights) if(light==previous) {score*=1.3f;break;}
                if(score>0.005f) candidates.emplace_back(score,light);
            }
            std::map<UActor*, float> receivers;
            std::vector<UActor*> retention;
            if(commandline->GetArgInt("","--shadow-receiver-selection",1)!=0)
                for(auto* light:ShadowLights)if(light && std::find(engine->Level->Actors.begin(),engine->Level->Actors.end(),light)!=engine->Level->Actors.end() && !light->bDeleteMe()) {
                    auto found=std::find_if(candidates.begin(),candidates.end(),[&](const auto& c){return c.second==light;});
                    if(found==candidates.end() || length(light->Location()-player->Location())>750.f)
                        retention.push_back(light);
                }
            // The normal lit-pawn path is unchanged. Only examine scenery when
            // an already active light is about to fade/drop out of that list.
            if(!retention.empty()) {
                auto* model=engine->Level->Model;
                for(const auto& visible:MainFrame.OpaqueNodes) {
                    auto* node=visible.Node;
                    const auto& surface=model->Surfaces[node->Surf];
                    if(surface.LightMap<0 || surface.LightMap>=int(model->LightMap.size()) ||
                        (visible.PolyFlags&PF_Unlit) || !node->NumVertices)continue;
                    int first=model->LightMap[surface.LightMap].LightActors;
                    if(first<0 || first>=int(model->Lights.size()))continue;
                    vec3 lo=model->Points[model->Vertices[node->VertPool].Vertex],hi=lo;
                    for(int v=1;v<node->NumVertices;++v) {
                        auto point=model->Points[model->Vertices[node->VertPool+v].Vertex];
                        for(int k=0;k<3;++k){lo[k]=std::min(lo[k],point[k]);hi[k]=std::max(hi[k],point[k]);}
                    }
                    vec3 focus;
                    for(int k=0;k<3;++k)focus[k]=std::clamp(engine->CameraLocation[k],lo[k],hi[k]);
                    float viewDistance=length(focus-engine->CameraLocation);
                    if(viewDistance>=900.f)continue;
                    float fade=std::clamp((900.f-viewDistance)/150.f,0.f,1.f);
                    fade=fade*fade*(3-2*fade);
                    int pixelLights=0;
                    for(int at=first;at<int(model->Lights.size()) && model->Lights[at] && pixelLights<8;++at) {
                        auto* light=model->Lights[at];
                        float radius=light->WorldLightRadius();auto effect=light->LightEffect();
                        if(light->LightType()==0 || light->LightBrightness()<=0 || radius<=0 || effect==4 || effect==8 || (effect>=12 && effect<=15) || effect==17)continue;
                        ++pixelLights;
                        if(std::find(retention.begin(),retention.end(),light)==retention.end())continue;
                        if(light->bDeleteMe() || radius<8 || light->LightBrightness()<=0)continue;
                        // The nearest point is on the receiver's plane/AABB, not
                        // the pawn. Do not trace the light to the pawn here: the
                        // baked surface mask already preserves world occlusion.
                        vec3 point;
                        for(int k=0;k<3;++k)point[k]=std::clamp(light->Location()[k],lo[k],hi[k]);
                        float d=length(point-light->Location());
                        if(d>=std::min(radius,1800.f))continue;
                        float score=(light->LightBrightness()/255.f)*VertexLight::LightDistanceFalloff(d*d/(radius*radius))*fade;
                        score/=1.f+viewDistance/600.f;
                        auto found=receivers.find(light);
                        if(found==receivers.end() || score>found->second)receivers[light]=score;
                    }
                }
                for(auto& [light,receiver]:receivers) {
                    float score=receiver;
                    for(auto* previous:ShadowLights)if(light==previous){score*=1.3f;break;}
                    auto existing=std::find_if(candidates.begin(),candidates.end(),[&](const auto& c){return c.second==light;});
                    if(existing!=candidates.end())existing->first=std::max(existing->first,score);
                    else if(score>.005f)candidates.emplace_back(score,light);
                }
            }
            std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.first>b.first;});
            int limit=std::min(int(candidates.size()),budget);
            const bool fade=commandline->GetArgInt("","--shadow-transitions",1)!=0;
            std::vector<uintptr_t> desired;
            for(int i=0;i<limit;++i)desired.push_back(reinterpret_cast<uintptr_t>(candidates[i].second));
            for(auto& s:ShadowFades.slots)if(s.light) {
                auto* old=reinterpret_cast<UActor*>(s.light);
                // Light actors normally hide their editor sprite in gameplay.
                // That does not turn off their light; clearing them here would
                // restart the fade every frame and keep shadows nearly invisible.
                if(std::find(engine->Level->Actors.begin(),engine->Level->Actors.end(),old)==engine->Level->Actors.end() || old->bDeleteMe())s={};
            }
            ShadowFades.update(desired,LevelTimeElapsed,budget);
            std::array<float,RuneShadows::Capacity> strengths{};
            for(int slot=0;slot<RuneShadows::Capacity;++slot) {
                auto& s=ShadowFades.slots[slot];
                ShadowLights[slot]=fade?reinterpret_cast<UActor*>(s.light):(slot<limit?candidates[slot].second:nullptr);
                if(auto* light=ShadowLights[slot]) {
                    // A visible receiver keeps its light eligible even if the
                    // player steps into shadow or outside that light's radius.
                    float edge=receivers.count(light)?1.f:std::clamp((900.f-length(light->Location()-player->Location()))/150.f,0.f,1.f);
                    strengths[slot]=fade?s.strength*edge*edge*(3-2*edge):1;
                    if(strengths[slot]<=0)ShadowLights[slot]=nullptr;
                }
            }
            if(commandline->HasArg("","--trace-shadows")){static int lastBudget=-1,lastLimit=-1;if(lastBudget!=budget||lastLimit!=limit){LogMessage("RUNE_SHADOW_BUDGET requested="+std::to_string(budget)+" candidates="+std::to_string(candidates.size())+" active="+std::to_string(limit));lastBudget=budget;lastLimit=limit;}}
            for(int slot=0;slot<RuneShadows::Capacity;++slot)
            {
                UActor* light=ShadowLights[slot];
                if(!light) continue;
                VisibleFrame shadow; float radius=std::min(light->WorldLightRadius(),1800.0f);
                vec3 target=player->Location()+vec3(0,0,12);
                const bool pawnLit=std::find(player->TouchingLights.List.begin(),player->TouchingLights.List.end(),light)!=player->TouchingLights.List.end();
                if(ShadowAimLights[slot]!=light) {
                    ShadowAimLights[slot]=light;ShadowAimTargets[slot]=target;
                }
                // Keep the last lit-pawn projection when he steps out of a
                // light. Continuing to aim at him would sweep the shadow cone
                // away from exactly the scenery for which we retained it.
                if(commandline->GetArgInt("","--shadow-receiver-selection",1)==0 || pawnLit)
                    ShadowAimTargets[slot]=target;
                shadow.ProcessShadow(light->Location(),ShadowAimTargets[slot],radius);
                if(Device->BeginLightShadow(&shadow.Frame,light->Location(),engine->CameraLocation,radius,slot))
                {
                    Device->SetLightShadowStrength(slot,strengths[slot]);
                    bool more=false;for(int after=slot+1;after<RuneShadows::Capacity;++after)if(ShadowLights[after])more=true;
                    shadow.DrawShadow(); Device->EndLightShadow(more);
                    if(commandline->HasArg("","--trace-shadow-transitions"))
                        LogMessage("RUNE_DYNAMIC_SHADOW light="+light->Name.ToString()+" slot="+std::to_string(slot)+" count="+std::to_string(limit)+" surfaces="+std::to_string(shadow.OpaqueNodes.size())+" actors="+std::to_string(shadow.Actors.size())+" hidden="+std::to_string(bool(light->bHidden()))+" strength="+std::to_string(strengths[slot])+" pawn_visible="+std::to_string(shadow.IsBoxVisible(player->BspInfo.BoundingBox))+" receiver="+std::to_string(receivers.count(light))+" pawn_lit="+std::to_string(std::find(player->TouchingLights.List.begin(),player->TouchingLights.List.end(),light)!=player->TouchingLights.List.end())+" size=1024");
                }
            }
        }
    }

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
        bool found=false;float waterHeight=0,nearest=1e30f;const VisibleNode* waterReceiver=nullptr;
        auto considerWater=[&](const VisibleNode& node) {
            float height;
            if(node.WaterPlane(height) && (Device->UsesModernWater() || engine->CameraLocation.z>height+1)) {
                float distance=std::abs(engine->CameraLocation.z-height);
                if(distance<nearest){found=true;nearest=distance;waterHeight=height;waterReceiver=&node;}
            }
        };
        for(const auto& translucent:MainFrame.Translucents)considerWater(translucent.Node);
        for(const auto& node:MainFrame.OpaqueNodes)considerWater(node);
        if(found && Device->UsesModernWater()) {
            bool outdoors=false;for(const auto& portal:MainFrame.Portals)if(portal.SkyZone){outdoors=true;break;}
            auto probes=engine->Level->Light.SampleEnvironment(engine->CameraLocation);
            vec3 environment(0);for(const auto& lobe:probes.Lobes)environment+=lobe.xyz()/6.f;
            if(outdoors) {
                if(!WaterSkyReady) {
                    WaterSkyReady=true;int textures=0;std::set<UTexture*> seen;
                    for(const auto& surface:engine->Level->Model->Surfaces) {
                        auto* texture=surface.Material;if(!texture || !seen.insert(texture).second)continue;
                        std::string name=texture->Name.ToString();for(char& ch:name)ch=char(std::tolower((unsigned char)ch));
                        if(name.find("sky")==std::string::npos && name.find("cloud")==std::string::npos)continue;
                        texture->LoadNow();
                        if(!WaterSkyTexture || name.find("cloud")!=std::string::npos)WaterSkyTexture=texture;
                        if(texture->UsedFormat!=TextureFormat::P8 || !texture->Palette() || texture->UsedMipmaps.empty())continue;
                        auto& data=texture->UsedMipmaps.back().Data;auto& palette=texture->Palette()->Colors;
                        vec3 average(0);int count=0;
                        for(size_t i=0;i<data.size();i+=std::max(size_t(1),data.size()/1024)) {
                            unsigned index=data[i];if(index>=palette.size())continue;uint32_t color=palette[index];
                            average+=vec3(float(color&255),float((color>>8)&255),float((color>>16)&255))/255.f;++count;
                        }
                        if(count) {
                            average/=float(count);WaterSkyColor+=average;++textures;
                            if(!WaterSkyTexture || name.find("cloud")!=std::string::npos)WaterSkyTexture=texture;
                        }
                    }
                    if(textures)WaterSkyColor/=float(textures);
                }
                // Actual authored sky average, not the water's often-dark bake.
                if(dot(WaterSkyColor,WaterSkyColor)>.001f)environment=WaterSkyColor;
            }
            vec3 direction=normalize(vec3(.35f,-.45f,1.f)),lightColor(0);float best=0;
            auto considerLight=[&](UActor* light) {
                if(!light || light->LightType()==0 || light->GetBool("bNegativeLight"))return;
                vec3 delta=light->Location()-vec3(engine->CameraLocation.x,engine->CameraLocation.y,waterHeight);
                float distance=length(delta),radius=light->WorldLightRadius();
                if(distance<1 || radius<1 || delta.z<=0)return;
                float score=(light->LightBrightness()/255.f)*std::max(0.f,1.f-distance/radius);
                if(score>best) {best=score;direction=light->Location();lightColor=hsbtorgb(light->LightHue(),light->LightSaturation(),light->LightBrightness());}
            };
            if(auto* pawn=engine->viewport->Actor()) {
                engine->Level->Light.UpdateLightList(pawn);
                for(auto* light:pawn->TouchingLights.List)considerLight(light);
            }
            if(waterReceiver) {
                auto* model=engine->Level->Model;int index=model->Surfaces[waterReceiver->Node->Surf].LightMap;
                if(index>=0 && index<int(model->LightMap.size())) {
                    int first=model->LightMap[index].LightActors;
                    if(first>=0)for(int i=first;i<int(model->Lights.size()) && model->Lights[i];++i)considerLight(model->Lights[i]);
                }
            }
            Device->PrepareModernWater(waterHeight,engine->LevelInfo->TimeSeconds(),vec4(environment,outdoors?1.f:0.f),vec4(direction,std::min(best,1.f)),vec4(lightColor,0));
            if(outdoors && WaterSkyTexture) {
                auto* sky=WaterSkyTexture->GetAnimTexture();UpdateTexture(sky);
                TextureInfo info;info.CacheID=uint64_t(reinterpret_cast<uintptr_t>(sky));info.Texture=sky;
                info.Format=sky->UsedFormat;info.Mips=sky->UsedMipmaps.data();info.NumMips=int(sky->UsedMipmaps.size());
                info.USize=sky->USize();info.VSize=sky->VSize();info.bRealtimeChanged=sky->TextureModified;
                if(sky->Palette())info.Palette=reinterpret_cast<TextureColor*>(sky->Palette()->Colors.data());
                if(info.NumMips) {Device->PrepareWaterSky(&info);sky->TextureModified=false;}
            }
            if(commandline->HasArg("","--trace-rune-water")) {
                static int frames=0;if(++frames%120==1)LogMessage("RUNE_WATER_MODERN scene_passes=0 height="+std::to_string(waterHeight)+" underwater="+std::to_string(cameraZone && cameraZone->bWaterZone())+" outdoors="+std::to_string(outdoors)+" cached_depth_samples="+std::to_string(WaterDepthSamples.size())+" sky="+(WaterSkyTexture?WaterSkyTexture->Name.ToString():"none"));
            }
        }
        else if(found && !(Device->WorldFeatures&8192)) {
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
