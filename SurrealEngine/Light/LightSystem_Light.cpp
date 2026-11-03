
#include "Precomp.h"
#include "LightSystem.h"
#include "LightingVisibilityBvh.h"
#include "BuildLightingVisibilityBvh.h"
#include "RenderDevice/RenderDevice.h"
#include "Engine.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include "Render/RenderSubsystem.h"
#include "Math/hsb.h"
#include "Packages/Engine/Actors/Brush/MovingBrush.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Resources/Level/UPolys.h"
#include "Packages/Engine/Resources/Level/UModel.h"

bool LightSystem::TraceLevelVisibility(const vec3& from,const vec3& to,UActor* ignored)
{
    if(commandline->GetArgInt("","--render-fast-world-visibility",1)==0)
        return Level->Collision.TraceAnyHit(from,to,ignored,false,true,true,TraceWorldPart::LevelOnly);
    if(!visibilityBvh)visibilityBvh=BuildLightingVisibilityBvh(Level->Model);
    return visibilityBvh->Trace(from,to);
}

TextureInfo LightSystem::GetMoverLightmap(UBrush* mover, const Poly& poly, UZoneInfo* zoneActor, UModel* model)
{
	Coords localCoords;
	localCoords.Origin = -poly.Base;
	localCoords.XAxis = poly.TextureU;
	localCoords.YAxis = poly.TextureV;
	localCoords.ZAxis = poly.Normal;

	vec3 scale = mover->MainScale().Scale;

    // Remastered moving surfaces receive light at their current position.
    // Original mode keeps the authored BrushRaytraceKey bake unchanged.
    const bool liveLighting=MovingBrushDynamicLight(mover) ||
        (engine->LaunchInfo.IsRune() && engine->render->Device->UsesPixelMeshLighting() &&
         commandline->GetArgInt("","--render-live-movers",1)!=0);
	mat4 brushToWorld;
	if (liveLighting)
	{
		vec3 moverLocation = mover->Location();
		Rotator moverRotation = mover->Rotation();
		brushToWorld = mat4::translate(moverLocation) * Coords::Rotation(moverRotation).ToMatrix() * mat4::scale(scale) * mat4::translate(-mover->PrePivot());
	}
	else
	{
		vec3 moverLocation = mover->GetVector("BasePos") + MovingBrushKeyPos(mover, mover->GetByte("BrushRaytraceKey"));
		Rotator moverRotation = mover->GetRotator("BaseRot") + MovingBrushKeyRot(mover, mover->GetByte("BrushRaytraceKey"));
		brushToWorld = mat4::translate(moverLocation) * Coords::Rotation(moverRotation).ToMatrix() * mat4::scale(scale) * mat4::translate(-mover->PrePivot());
	}
	Coords worldCoords = Coords::FromMatrix(brushToWorld * localCoords.ToMatrix());
    if(liveLighting && scale.x!=0 && scale.y!=0 && scale.z!=0) {
        // Texture gradients and normals are covectors. Forward scaling would
        // move lighting across a scaled brush instead of following its faces.
        auto covectors=mat3::transpose(mat3::inverse(mat3(brushToWorld)));
        worldCoords.XAxis=covectors*poly.TextureU;
        worldCoords.YAxis=covectors*poly.TextureV;
        worldCoords.ZAxis=covectors*poly.Normal;
    }

    // Bounds belong to this polygon, in world space, not the first polygon
    // cached for the entire brush or transformed through its texture basis.
    vec3 center(0);for(const auto& v:poly.Vertices)center+=(brushToWorld*vec4(v,1)).xyz();
    if(!poly.Vertices.empty())center/=float(poly.Vertices.size());
    float radius=0;for(const auto& v:poly.Vertices)radius=std::max(radius,length((brushToWorld*vec4(v,1)).xyz()-center));
    Array<vec3> samples;
    if(liveLighting)for(const auto& v:poly.Vertices)
        samples.push_back(mix((brushToWorld*vec4(v,1)).xyz(),center,.02f));
    return GetLightmap(model,poly.BrushPolyIndex,worldCoords,zoneActor,center,radius,
        liveLighting?mover:nullptr,mover->bSpecialLit(),&samples);
}

TextureInfo LightSystem::GetLevelLightmap(BspSurface& surface, UZoneInfo* zoneActor, UModel* model)
{
	Coords mapCoords;
	mapCoords.Origin = model->Points[surface.pBase];
	mapCoords.XAxis = model->Vectors[surface.vTextureU];
	mapCoords.YAxis = model->Vectors[surface.vTextureV];
	mapCoords.ZAxis = model->Vectors[surface.vNormal];
	return GetLightmap(model, surface.LightMap, mapCoords, zoneActor, surface.Center, surface.Radius, nullptr, surface.PolyFlags & PF_SpecialLit);
}

TextureInfo LightSystem::GetLightmap(UModel* model, int lightmapIndex, const Coords& coords, UZoneInfo* zoneActor, const vec3& worldLocation, float radius, UBrush* dynamicMover, bool specialLit, const Array<vec3>* moverSamples)
{
	if (lightmapIndex < 0 || (size_t)lightmapIndex >= model->LightMap.size())
		return {};

	LightMapIndex& lmindex = model->LightMap[lightmapIndex];

	uint32_t ambientID = (((uint32_t)zoneActor->AmbientHue()) << 16) | (((uint32_t)zoneActor->AmbientSaturation()) << 8) | (uint32_t)zoneActor->AmbientBrightness();
	bool pixelLighting = engine->LaunchInfo.IsRune() && engine->render->Device->UsesPixelMeshLighting();
    // Separate caches make an Original round-trip exact and avoid stale GPU uploads.
    uint64_t cacheID = (((uint64_t)lmindex.LMCacheID) << 32) | (((uint64_t)ambientID) << 8) | (pixelLighting ? 5 : 1);

	if (dynamicMover)
	{
		// Place mover ID in upper 12 bits (4096 movers). This leaves 20 bits (1 million) for the lightmaps.
		if (dynamicMover->Light.MoverID == 0)
			dynamicMover->Light.MoverID = NextMoverID++;
		cacheID |= ((uint64_t)dynamicMover->Light.MoverID) << 52;
	}

	int checkCounter = LightmapCheckCounter++;

	// Collect lights for the lightmap and check if they changed

	int lastStaticUpdate = -100;
	int lastDynamicUpdate = -100;
	TempDynLightList.clear();

	if (!dynamicMover)
	{
		// Examine all the lights that are statically baked into the map:

		if (lmindex.LightActors >= 0)
		{
			UActor** lightlist = &model->Lights[lmindex.LightActors];
			for (int lightindex = 0; lightlist[lightindex] != nullptr; lightindex++)
			{
				UActor* light = lightlist[lightindex];
				CheckLight(light);
				lastStaticUpdate = std::max(lastStaticUpdate, light->Light.LastUpdate);

				// Mark the light as visited so the second pass doesn't pick it up
				light->Light.LightmapCheckCounter = checkCounter;
			}
		}

		// Look at all lights potentially touching the surface. They go into our dynamic light list:

		LightTree.CollectLights(worldLocation, radius);
		for (UActor* light : LightTree.CollectedLights)
		{
			if (light->Light.LightmapCheckCounter != checkCounter)
			{
				light->Light.LightmapCheckCounter = checkCounter;
				if (!light->bStatic() && !light->bNoDelete() && light->bSpecialLit() == specialLit)
				{
					CheckLight(light);
					lastDynamicUpdate = std::max(lastDynamicUpdate, light->Light.LastUpdate);
					TempDynLightList.push_back(light);
				}
			}
		}
	}
	else
	{
        // Original dynamic lightmaps retain their existing update cadence.
        // Pixel lighting also tracks geometry and contributing lights below.
        lastDynamicUpdate = pixelLighting ? -100 : FrameCounter;

        if(pixelLighting) {
            WorldVisibilityCache* visibility = commandline->GetArgInt("", "--render-mover-visibility-cache", 1) != 0
                ? &moverVisibility[cacheID] : nullptr;
            if (visibility) visibility->Begin();
            auto blocked = [&](const vec3& from, const vec3& to) {
                if (!visibility) return Level->Collision.TraceAnyHit(from,to,dynamicMover,false,true,true);
                return visibility->Trace(from,to,
                    [&] { return TraceLevelVisibility(from,to,dynamicMover); },
                    [&] { return Level->Collision.TraceAnyHit(from,to,dynamicMover,false,true,true,TraceWorldPart::NonLevel); });
            };
            LightTree.CollectLights(worldLocation,radius);
            const vec3 normal=normalize(coords.ZAxis);
            for(auto* light:LightTree.CollectedLights) {
                if(light->bCorona() || light->bSpecialLit()!=specialLit)continue;
                const vec3 delta=light->Location()-worldLocation;
                const float reach=light->WorldLightRadius()+radius;
                if(dot(delta,delta)>reach*reach || dot(delta,normal)<=0)continue;
                // Test the actual face, not a pivot buried inside the wall.
                bool visible=!blocked(worldLocation+normal,light->Location());
                // A retracting hatch can have its center inside a wall while
                // its exposed edge still receives light. Keep that light if
                // any inset corner sees it; GPU shadows resolve the coverage.
                if(!visible && moverSamples)for(const auto& sample:*moverSamples)
                    if(!blocked(sample+normal,light->Location())) {visible=true;break;}
                if(!visible)continue;
                CheckLight(light);lastDynamicUpdate=std::max(lastDynamicUpdate,light->Light.LastUpdate);
                TempDynLightList.push_back(light);
            }
            if (visibility) visibility->End();
        } else for (UActor* light : dynamicMover->TouchingLights.List) {
            CheckLight(light);lastDynamicUpdate=std::max(lastDynamicUpdate,light->Light.LastUpdate);
            TempDynLightList.push_back(light);
        }
	}

	// If anything changed update the lightmap:

	bool bRealtimeChanged = false;
	auto& lmtexture = lmtextures[cacheID];
    const bool moverChanged=dynamicMover && pixelLighting && (!lmtexture || !lmtexture->HasMoverCoords ||
        !(lmtexture->MoverCoords==coords) || lmtexture->MoverLights.size()!=TempDynLightList.size() ||
        !std::equal(lmtexture->MoverLights.begin(),lmtexture->MoverLights.end(),TempDynLightList.begin()));
	if (!lmtexture || moverChanged || lmtexture->LastStaticUpdate != lastStaticUpdate || lmtexture->LastDynamicUpdate != lastDynamicUpdate)
	{
		engine->render->Stats.LightmapsUpdated++;

		Builder.Setup(model, coords, lightmapIndex);

		if (!lmtexture || lmtexture->Mip.Width != Builder.Width() || lmtexture->Mip.Height != Builder.Height())
		{
			lmtexture = std::make_unique<LightmapTexture>();
			lmtexture->Format = TextureFormat::RGBA32_F;
			lmtexture->Mip.Width = Builder.Width();
			lmtexture->Mip.Height = Builder.Height();
			lmtexture->Mip.Data.resize((size_t)lmtexture->Mip.Width * lmtexture->Mip.Height * sizeof(vec4));
		}

        if (pixelLighting)
        {
            Builder.SetupPixelLighting(&lmtexture->PixelLighting);
            Builder.SetAmbientLight(zoneActor);
            if (!dynamicMover) Builder.AddStaticLights(model, lightmapIndex);
        }
        else if (dynamicMover)
        {
            Builder.SetAmbientLight(zoneActor);
        }
		else if (lmtexture->LastStaticUpdate != lastStaticUpdate)
		{
			Builder.SetAmbientLight(zoneActor);
			Builder.AddStaticLights(model, lightmapIndex);
			Builder.SaveStaticLight(lmtexture->StaticLightColors);
		}
		else if (lmtexture->StaticLightColors.size() == Builder.Width() * Builder.Height())
		{
			Builder.LoadStaticLight(lmtexture->StaticLightColors);
		}

		Builder.AddDynamicLights(model, lightmapIndex, TempDynLightList);

		UnrealMipmap& lmmip = lmtexture->Mip;
		vec4* dest = (vec4*)lmmip.Data.data();
		const vec3* src = Builder.Pixels();
		int count = lmmip.Width * lmmip.Height;
		for (int i = 0; i < count; i++)
		{
			dest[i].r = std::min(src[i].r, 1.0f);
			dest[i].g = std::min(src[i].g, 1.0f);
			dest[i].b = std::min(src[i].b, 1.0f);
			dest[i].a = 1.0f;
		}

        if(dynamicMover && pixelLighting) {
            lmtexture->HasMoverCoords=true;lmtexture->MoverCoords=coords;lmtexture->MoverLights=TempDynLightList;
        }
		lmtexture->LastStaticUpdate = lastStaticUpdate;
		lmtexture->LastDynamicUpdate = lastDynamicUpdate;
		bRealtimeChanged = true;
	}

	TextureInfo texinfo;
	texinfo.bRealtimeChanged = bRealtimeChanged;
	texinfo.CacheID = cacheID;
    texinfo.PixelLighting = pixelLighting ? &lmtexture->PixelLighting : nullptr;
	texinfo.Format = lmtexture->Format;
	texinfo.Mips = &lmtexture->Mip;
	texinfo.NumMips = 1;
	texinfo.USize = texinfo.Mips[0].Width;
	texinfo.VSize = texinfo.Mips[0].Height;
	texinfo.Pan = { lmindex.PanX, lmindex.PanY };
	texinfo.UScale = lmindex.UScale;
	texinfo.VScale = lmindex.VScale;
    if (pixelLighting && commandline->HasArg("", "--trace-rune-combat"))
    {
        static bool traced[2] = {};
        int kind = dynamicMover ? 1 : 0;
        if (!traced[kind] && lmtexture->PixelLighting.Count)
        {
            traced[kind] = true;
            LogMessage("RUNE_PIXEL_SURFACE kind=" + std::string(kind ? "dynamic_mover" : "baked") +
                " lights=" + std::to_string(lmtexture->PixelLighting.Count));
        }
    }
	return texinfo;
}

void LightSystem::CheckLight(UActor* light)
{
	if (light->Light.LastCheck == FrameCounter)
		return;

	light->Light.LastCheck = FrameCounter;

	vec3 location = light->Location();
	float radius = light->WorldLightRadius();
	uint8_t type = light->LightType();
	uint8_t effect = light->LightEffect();
	uint8_t hue = light->LightHue();
	uint8_t saturation = light->LightSaturation();
	uint8_t brightness = light->LightBrightness();
	if (
		location != light->Light.Location ||
		radius != light->Light.Radius ||
		type != light->Light.Type ||
		effect != light->Light.Effect ||
		hue != light->Light.Hue ||
		saturation != light->Light.Saturation ||
		brightness != light->Light.Brightness)
	{
		light->Light.Location = location;
		light->Light.Radius = radius;
		light->Light.Type = type;
		light->Light.Effect = effect;
		light->Light.Hue = hue;
		light->Light.Saturation = saturation;
		light->Light.Brightness = brightness;
		light->Light.LastUpdate = FrameCounter;
	}
}

static float LightDistanceFalloff(float distsqr)
{
	float v = std::sqrt(distsqr + 0.0001f);
	float v2 = v * v;
	float v3 = v2 * v;
	return std::min((1.0f + 2.0f * v3 - 3.0f * v2) / v, 1.0f);
}

void LightSystem::InitVertexLight(VertexLight& out, UActor* actor, UZoneInfo* zoneActor)
{
	// AmbientGlow value 255 is a special pulsating effect used for powerups
	float ambientGlow = actor->AmbientGlow() == 255 ? AmbientGlowAmount : actor->AmbientGlow() * (1.0f / 255.0f);
	out.AmbientColor = ambientGlow + hsbtorgb(zoneActor->AmbientHue(), zoneActor->AmbientSaturation(), zoneActor->AmbientBrightness());
	out.ScaleGlow = actor->ScaleGlow() * 1.5f;

	int lightIndex = 0;
	for (UActor* light : actor->TouchingLights.List)
	{
		out.Lights[lightIndex].Location = light->Location();
		out.Lights[lightIndex].Color = LightmapBuilder::GetLightColor(light);
		float invRadius = 1.0f / light->WorldLightRadius();
		out.Lights[lightIndex].InvRadiusSquared = invRadius * invRadius;
		lightIndex++;
		if (lightIndex == VertexLight::MaxLights)
			break;
	}
	out.NumLights = lightIndex;

	out.CameraLocation = engine->CameraLocation;

	int fogIndex = 0;
	for (UActor* light : FogBalls)
	{
		if (light->FogInfo.brightness < 0.0f)
		{
			light->FogInfo.fogcolor = hsbtorgb(light->LightHue(), light->LightSaturation(), light->LightBrightness());
			light->FogInfo.brightness = light->LightBrightness() * (1.0f / 255.0f) * light->VolumeBrightness() * (1.0f / 64.0f);
			light->FogInfo.fog = light->VolumeFog() * (1.0f / 255.0f);
			light->FogInfo.radius = light->WorldVolumetricRadius();
			light->FogInfo.location = light->Location();
		}

		out.FogBalls[fogIndex].fogcolor = light->FogInfo.fogcolor;
		out.FogBalls[fogIndex].brightness = light->FogInfo.brightness * 5.0f;
		out.FogBalls[fogIndex].fog = light->FogInfo.fog;
		out.FogBalls[fogIndex].radius = light->FogInfo.radius;
		out.FogBalls[fogIndex].lightpos = light->FogInfo.location;

		fogIndex++;
		if (fogIndex == VertexLight::MaxFogBalls)
			break;
	}
	out.NumFogBalls = fogIndex;
}
