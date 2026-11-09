#pragma once
#include "Engine.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "Utils/CommandLine.h"
#include "RuneStairPresentation.h"
#include "GameWindow.h"
#include "RenderDevice/RenderDevice.h"
#include <fstream>
class RuneLocalStairProbe {
    std::ofstream trace;
public:
    void Before(Engine& game,int frame) {
        if(!commandline->HasArg("","--probe-local-stairs"))return;
        if(!commandline->HasArg("","--save-directory") || !commandline->HasArg("","--maxframes"))Exception::Throw("Local stairs need bounded isolated fixture");
        auto* p=game.viewport->Actor();
        if(frame==60) {
            auto path=commandline->GetArg("","--probe-local-stairs","");trace.open(path);if(!trace)Exception::Throw("Cannot open stair trace");
            trace<<"frame,x,y,z,camera_z,anchor_z,visual_z,grounded,offset,camera_height\n";
            if(!p->SetLocation(vec3(220.203f,-773.538f,69)))Exception::Throw("Stair start unavailable");
            p->SetRotation(Rotator(0,0,0));p->ViewRotation()={0,0,0};p->Velocity()=vec3(0);p->SetPhysics(PHYS_Walking);
        }
        int hz=commandline->GetArgInt("","--probe-tick-hz",120);if(!hz)hz=120;
        if(frame==60+hz)game.InputEvent(IK_W,IST_Press);
        if(frame==60+hz*3)game.InputEvent(IK_W,IST_Release);
        if(frame==60+hz*5)game.InputEvent(IK_S,IST_Press);
        if(frame==60+hz*7)game.InputEvent(IK_S,IST_Release);
    }
    void After(Engine& game,int frame) {
        if(!trace)return;auto* p=game.viewport->Actor();
        auto mesh=GetRuneMeshToWorld(p);float offset=RuneStairPresentation::Offset(p),visualZ=mesh[14]+offset;
        trace<<frame<<','<<p->Location().x<<','<<p->Location().y<<','<<p->Location().z<<','<<game.CameraLocation.z<<','<<p->GetVector("OldCameraStart").z<<','<<visualZ<<','<<(p->Physics()==PHYS_Walking)<<','<<offset<<','<<p->GetFloat("CameraHeight")<<'\n';
        if((frame==270 || frame==760) && commandline->HasArg("","--probe-local-stair-captures")) {
            auto prefix=commandline->GetArg("","--probe-local-stair-captures","");
            auto* device=game.window->GetRenderDevice();int w=device->GetRenderWidth(),h=device->GetRenderHeight();
            Array<TextureColor> pixels;pixels.resize(size_t(w)*h);bool gamma=device->GammaCorrectScreenshots;
            device->GammaCorrectScreenshots=true;device->ReadPixels(pixels.data());device->GammaCorrectScreenshots=gamma;
            std::ofstream out(prefix+"-"+std::to_string(frame)+".ppm",std::ios::binary);out<<"P6\n"<<w<<' '<<h<<"\n255\n";
            for(auto c:pixels){const char rgb[]={char(c.R),char(c.G),char(c.B)};out.write(rgb,3);}
        }
    }
};
