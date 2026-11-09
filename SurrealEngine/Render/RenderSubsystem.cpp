#include "RuneDesktopMenu.h"
#include "RuneGore.h"
#include "RuneCorpse.h"

#include "Precomp.h"
#include "VisibleRuneEffects.h"
#include "VisibleSnow.h"
#include "VisibleRuneBlood.h"
#include "RenderSubsystem.h"
#include "RenderDevice/RenderDevice.h"
#include "GameWindow.h"
#include "Packages/Engine/USurrealClient.h"
#include "Packages/Engine/UConsole.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Resources/UPalette.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Resources/Level/UPolys.h"
#include "VM/ScriptCall.h"
#include "Engine.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"
#include <chrono>

RenderSubsystem::RenderSubsystem(RenderDevice* renderdevice) : Device(renderdevice)
{
}

void RenderSubsystem::DrawGame(float levelTimeElapsed)
{
    if(RuneDesktopMenu::Draw(*engine))return;
	auto profileStart=std::chrono::steady_clock::now();
	LevelTimeElapsed = levelTimeElapsed;
	AutoUV += levelTimeElapsed * 64.0f;
	engine->Level->Light.Tick(levelTimeElapsed);
    engine->Level->Light.PrepareEnvironmentProbes();
    RuneGore::Tick(levelTimeElapsed);
    // Both the campaign loop and interactive multiplayer draw through here.
    // Integrate secondary body motion once; shadow/reflection passes read it.
    RuneCorpse::TickMotion(levelTimeElapsed);

	Stats.Frames = 0;
	Stats.Surfaces = 0;
	Stats.Actors = 0;
	Stats.LightmapsUpdated = 0;

	vec3 flashScale = 0.5f;
	vec3 flashFog = vec3(0.0f, 0.0f, 0.0f);

	UPlayerPawn* player = UObject::TryCast<UPlayerPawn>(engine->CameraActor);
	if (player)
	{
		flashScale = player->FlashScale();
		flashFog = player->FlashFog();
	}

	Device->Brightness = engine->client->Brightness;
	Device->Lock(vec4(flashScale, 1.0f), vec4(flashFog, 1.0f), vec4(0.0f), nullptr, nullptr);

	ResetCanvas();
	PreRender();

	if (engine->LaunchInfo.ue1Version <= 219 || engine->console->bNoDrawWorld() == false)
	{
		DrawScene();
		RenderOverlays();
		if (engine->LaunchInfo.IsDeusEx())
			PostRenderFlash();
		Device->EndFlash();
	}

	PostRender();

	auto profileUnlock=std::chrono::steady_clock::now();
    Device->Unlock(true);
    auto profileEnd=std::chrono::steady_clock::now();
    if(commandline->GetArgInt("", "--profile-frames",0)>0) {
        static int frames=0; static double sceneMs=0,unlockMs=0;
        ++frames;
        if(frames>60) {
            sceneMs+=std::chrono::duration<double,std::milli>(profileUnlock-profileStart).count();
            unlockMs+=std::chrono::duration<double,std::milli>(profileEnd-profileUnlock).count();
            int samples=commandline->GetArgInt("", "--profile-frames",0);
            if(frames==60+samples) LogMessage("RUNE_RENDER_PROFILE scene_ms="+std::to_string(sceneMs/samples)+" unlock_ms="+std::to_string(unlockMs/samples));
        }
    }
}

void RenderSubsystem::DrawEditorViewport()
{
	Device->Brightness = engine->client->Brightness;
	DrawScene();
}

void RenderSubsystem::DrawVideoFrame(TextureInfo* frame, TextureInfo* background)
{
	vec3 flashScale = 0.5f;
	vec3 flashFog = vec3(0.0f, 0.0f, 0.0f);
	Device->Brightness = 0.4f;// engine->client->Brightness;
	Device->Lock(vec4(flashScale, 1.0f), vec4(flashFog, 1.0f), vec4(0.0f), nullptr, nullptr);
	ResetCanvas();
	Device->SetSceneNode(&Canvas.Frame);

	float sizeX = (float)(int)(engine->viewport->ViewportWidth() / (float)Canvas.uiscale);
	float sizeY = (float)(int)(engine->viewport->ViewportHeight() / (float)Canvas.uiscale);

	Rectf clipBox = Rectf::xywh(0.0f, 0.0f, sizeX, sizeY);
	Rectf dest = clipBox;

	if (frame)
	{
		Rectf src = Rectf::xywh(0.0f, 0.0f, (float)frame->USize, (float)frame->VSize);
		DrawTile(*frame, dest, src, clipBox, 1.0f, vec4(1.0f), vec4(0.0f), PF_TwoSided);
	}

	if (background)
	{
		Rectf src = Rectf::xywh(0.0f, 0.0f, (float)background->USize, (float)background->VSize);
		DrawTile(*background, dest, src, clipBox, 1.0f, vec4(1.0f), vec4(0.0f), PF_TwoSided | PF_Highlighted);
	}

	Device->EndFlash();
	Device->Unlock(true);
}

void RenderSubsystem::UpdateTexture(UTexture* tex)
{
	if (tex && tex->FrameCounter != TextureFrameCounter)
	{
		tex->Update(LevelTimeElapsed);
		tex->FrameCounter = TextureFrameCounter;
	}
}

void RenderSubsystem::UpdateTextureInfo(TextureInfo& info, BspSurface& surface, UTexture* texture, float ZoneUPanSpeed, float ZoneVPanSpeed)
{
	UpdateTextureInfo(info, texture);

	info.Pan.x = -(float)surface.PanU;
	info.Pan.y = -(float)surface.PanV;
	if (surface.PolyFlags & PF_AutoUPan) info.Pan.x -= AutoUV * ZoneUPanSpeed;
	if (surface.PolyFlags & PF_AutoVPan) info.Pan.y -= AutoUV * ZoneVPanSpeed;
}

void RenderSubsystem::UpdateTextureInfo(TextureInfo& info, const Poly& poly, UTexture* texture, float ZoneUPanSpeed, float ZoneVPanSpeed)
{
	UpdateTextureInfo(info, texture);

	info.Pan.x = -(float)poly.PanU;
	info.Pan.y = -(float)poly.PanV;
	if (poly.PolyFlags & PF_AutoUPan) info.Pan.x -= AutoUV * ZoneUPanSpeed;
	if (poly.PolyFlags & PF_AutoVPan) info.Pan.y -= AutoUV * ZoneVPanSpeed;
}

void RenderSubsystem::UpdateTextureInfo(TextureInfo& info, UTexture* texture)
{
	info.Texture = texture;
	info.CacheID = (uint64_t)(ptrdiff_t)texture;

	if (!info.Texture)
		return;

	info.UScale = texture->DrawScale();
	info.VScale = texture->DrawScale();
	info.Format = texture->UsedFormat;
	info.Mips = texture->UsedMipmaps.data();
	info.NumMips = (int)texture->UsedMipmaps.size();
	info.USize = texture->USize();
	info.VSize = texture->VSize();
	if (texture->Palette())
		info.Palette = (TextureColor*)texture->Palette()->Colors.data();

	info.bRealtimeChanged = texture->TextureModified;
	if (texture->TextureModified)
		texture->TextureModified = false;
    if(engine->LaunchInfo.IsRune())RuneEffects::WorldTexture(info,(float)engine->TotalTime);
}

void RenderSubsystem::OnMapLoaded()
{
    RuneSnow::Reset();
    RuneBloodPresentation::Reset();
    RuneGore::Reset();
    // Build the small shared sprite atlases during loading, not on the first hit.
    if(engine->LaunchInfo.IsRune() && RuneVisualEffects::Enabled.load()) {
        RuneEffects::Texture(0,false);RuneEffects::Texture(0,true);RuneEffects::Texture(0,true,true);
    }
    for (auto& light : ShadowLights) light = nullptr;
    ShadowFades.reset();
    WaterMaterialKinds.clear(); WaterSkyExposure.clear(); WaterSkyReady=false; WaterSkyTexture=nullptr; WaterSkyColor=vec3(0); WaterDepthSamples.clear(); WaterFaceDepth.clear(); Device->InvalidateWaterReflection();
	Device->Flush(true);
	engine->Level->Light.OnMapLoaded();
}
