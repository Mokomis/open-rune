#pragma once
#include "RuneSlam.h"
#include "Engine.h"
#include "GameWindow.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Inventory/UWeapon.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "Packages/Core/UClass.h"
#include "Package/PackageManager.h"
#include "RenderDevice/RenderDevice.h"
#include "Utils/CommandLine.h"
#include "VM/ScriptCall.h"
#include "Collision/TopLevel/CollisionHit.h"
#include <fstream>
class RuneSlamVisualProbe {
    unsigned stages=0;bool completed=false;UActor* enemy=nullptr;
    float startHeight=0,peakHeight=0;bool sawDive=false,landedStrike=false;
public:
    void Before(Engine& game,int frame) {
        if(!commandline->HasArg("","--probe-slam-gesture"))return;
        if(!commandline->HasArg("","--save-directory") || !commandline->HasArg("","--maxframes"))Exception::Throw("Slam gesture needs bounded isolated saves");
        auto* p=game.viewport->Actor();
        if(frame==90) {
            // A low ceiling legitimately clips the new launch. Pick a clear
            // diagnostic lane so this fixture exercises the complete flight.
            bool lane=false;
            for(auto* point:game.Level->Actors) {
                if(!point || !point->IsA("NavigationPoint"))continue;
                vec3 hit,normal;auto center=point->Location();
                if(!p->Trace(hit,normal,center-vec3(0,0,250),center,false,vec3(0)) || normal.z<.7f)continue;
                center.z=hit.z+p->CollisionHeight()+1;
                if(!p->CheckLocation(center,p->CollisionRadius(),p->CollisionHeight(),true).first)continue;
                if(p->Trace(hit,normal,center+vec3(0,0,300),center,false,vec3(p->CollisionRadius(),p->CollisionRadius(),p->CollisionHeight())))continue;
                auto original=p->Location();
                if(!p->SetLocation(center))continue;
                auto sweep=p->TryMove(vec3(0,0,300));
                bool clear=sweep.Fraction>.999f && p->Location().z-center.z>299;
                p->SetLocation(original);
                if(!clear)continue;
                for(int i=0;i<16 && !lane;++i) {
                    float angle=i*3.14159265f/8;vec3 forward(std::cos(angle),std::sin(angle),0);
                    auto target=center+forward*85.f;
                    if(!p->CheckLocation(target,24,46,true).first || !p->FastTrace(target,center))continue;
                    p->SetLocation(center);p->SetPhysics(PHYS_Walking);p->Velocity()=vec3(0);
                    p->SetRotation(Rotator::FromVector(forward));p->ViewRotation()=p->Rotation();lane=true;
                }
                if(lane)break;
            }
            if(!lane)Exception::Throw("No open-air slam fixture lane");
            startHeight=p->Location().z;LogMessage("RUNE_SLAM_FIXTURE_LANE xyz="+std::to_string(p->Location().x)+","+std::to_string(p->Location().y)+","+std::to_string(p->Location().z)+" height="+std::to_string(p->CollisionHeight()));
            auto* a=p->Spawn(game.packages->FindClass("RuneI.DwarfWorkHammer"),{},{},p->Location()+vec3(0,0,80),{});
            if(!a)Exception::Throw("Cannot prepare heavy weapon");a->SetFloat("RespawnTime",0);
            auto* item=CallEvent(a,"SpawnCopy",{ExpressionValue::ObjectValue(p)}).ToObject();CallEvent(p,"AcquireInventory",{ExpressionValue::ObjectValue(item)});
            p->Health()=1000;
            auto forward=vec3(std::cos(p->Rotation().YawRadians()),std::sin(p->Rotation().YawRadians()),0);
            enemy=p->Spawn(game.packages->FindClass("RuneI.DarkViking"),{},{},p->Location()+forward*85.f,{});
            if(enemy){enemy->GotoState("","");enemy->DisableEvent("Tick");enemy->SetPhysics(PHYS_Walking);UObject::Cast<UPawn>(enemy)->Health()=1000;}
        }
        if(frame>90) {
            peakHeight=std::max(peakHeight,p->Location().z-startHeight);
            if(p->Slam.phase==1 && p->Slam.age>=RuneSlam::Windup && p->Physics()==PHYS_Falling && p->Velocity().z<0)sawDive=true;
            if(p->Slam.phase==2 && p->Slam.hits>0)landedStrike=true;
            if(p->Slam.phase==1 && frame%10==0)LogMessage("RUNE_SLAM_FLIGHT_SAMPLE age="+std::to_string(p->Slam.age)+" z="+std::to_string(p->Location().z)+" vz="+std::to_string(p->Velocity().z)+" physics="+std::to_string(int(p->Physics())));
        }
        const bool touch=commandline->HasArg("","--probe-slam-touch");
        if(!touch) {
            if(frame==180 || frame==200)game.InputEvent(IK_Space,IST_Press);
            if(frame==181 || frame==201)game.InputEvent(IK_Space,IST_Release);
            if(frame==215)game.InputEvent(IK_LeftMouse,IST_Press);
            if(frame==216)game.InputEvent(IK_LeftMouse,IST_Release);
        }
        if(commandline->HasArg("","--probe-slam-side-view")) {
            auto forward=vec3(std::cos(p->Rotation().YawRadians()),std::sin(p->Rotation().YawRadians()),0),side=vec3(-forward.y,forward.x,0);
            bool found=false;vec3 hit,normal;auto center=p->Location()+vec3(0,0,15);
            for(float radius:{200.f,160.f,120.f}) {
                for(int i=0;i<8 && !found;++i) {
                    float angle=(i*45.f+45)*3.14159265f/180;
                    auto candidate=center+(forward*std::cos(angle)+side*std::sin(angle))*radius+vec3(0,0,5);
                    if(!p->Trace(hit,normal,candidate,center,false,vec3(8))) {game.CameraLocation=candidate;found=true;}
                }
                if(found)break;
            }
            game.CameraRotation=Rotator::FromVector(p->Location()+vec3(0,0,15)-game.CameraLocation);
            p->bBehindView()=true;p->SetBool("bClientSideAlpha",false);p->SetFloat("ClientSideAlphaScale",1);
        }
        if(frame==(touch?700:400)) {
            if(stages!=15 || p->Slam.hits<1 || !p->Weapon() || p->Weapon()->Owner()!=p)Exception::Throw("Slam gesture/render/contact fixture failed");
            if(peakHeight<20 || !sawDive || !landedStrike)Exception::Throw("Slam actual lift/dive/landing fixture failed: peak="+std::to_string(peakHeight)+" dive="+std::to_string(sawDive)+" landed="+std::to_string(landedStrike));
            LogMessage("RUNE_SLAM_FLIGHT_RENDER_OK peak="+std::to_string(peakHeight)+" dive=1 landing_contact=1");
            completed=true;LogMessage("RUNE_SLAM_GESTURE_OK stages=4 heavy_hits="+std::to_string(p->Slam.hits));
        }
    }
    void After(Engine& game,int frame) {
        if(!commandline->HasArg("","--probe-slam-gesture"))return;auto& s=game.viewport->Actor()->Slam;
        int stage=s.phase==1?(s.age>=.36f?2:s.age>=.20f?1:s.age>=.08f?0:-1):s.phase==2?3:-1;
        if(stage<0 || (stages&(1u<<stage)))return;stages|=1u<<stage;
        auto prefix=commandline->GetArg("","--probe-slam-captures","");if(prefix.empty())return;
        auto* d=game.window->GetRenderDevice();int w=d->GetRenderWidth(),h=d->GetRenderHeight();Array<TextureColor> pixels;pixels.resize(size_t(w)*h);
        bool gamma=d->GammaCorrectScreenshots;d->GammaCorrectScreenshots=true;d->ReadPixels(pixels.data());d->GammaCorrectScreenshots=gamma;
        std::ofstream f(prefix+"-"+std::to_string(stage)+".ppm",std::ios::binary);f<<"P6\n"<<w<<' '<<h<<"\n255\n";
        for(auto c:pixels){const char rgb[]={char(c.R),char(c.G),char(c.B)};f.write(rgb,3);}
    }
    void Finish(){if(commandline->HasArg("","--probe-slam-gesture") && !completed)Exception::Throw("Slam gesture incomplete");}
};
