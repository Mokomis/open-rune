#pragma once

#include "Engine.h"
#include "Package/PackageManager.h"
#include "Package/Package.h"
#include "Packages/Core/UState.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "VM/Bytecode.h"
#include "VM/Frame.h"
#include "VM/NativeFunc.h"
#include "VM/ScriptCall.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Uses a private transient BSP and actors; never spawns into or saves a map.
// Call after the Rune engine and native bindings have been constructed.
inline void RunRuneCampaignNativeProbe(Engine& game)
{
    auto require=[](bool ok,const char* message) { if(!ok) throw std::runtime_error(message); };
    auto* transient=game.packages->GetTransientPackage();
    auto* enginePackage=game.packages->GetPackage("Engine");
    auto make=[&](const char* name,const char* className) { return transient->NewObject(name,enginePackage->GetClass(className),ObjectFlags::Transient); };
    auto* info=UObject::Cast<ULevelInfo>(make("RuneCampaignNativeLevelInfo","LevelInfo"));
    auto* air=UObject::Cast<UZoneInfo>(make("RuneCampaignNativeAir","ZoneInfo"));
    auto* water=UObject::Cast<UZoneInfo>(make("RuneCampaignNativeWater","ZoneInfo"));
    air->bWaterZone()=false; water->bWaterZone()=true;
    info->bBegunPlay()=true;
    ULevel level("RuneCampaignNativeLevel",nullptr,ObjectFlags::Transient);
    UModel model("RuneCampaignNativeModel",nullptr,ObjectFlags::Transient);
    level.Model=&model; level.SetOwner(info);
    model.Nodes.resize(1); model.Zones.resize(3);
    model.Nodes[0]={};
    auto& plane=model.Nodes[0];
    plane.PlaneZ=1; plane.PlaneW=0; plane.Front=-1; plane.Back=-1;
    plane.Zone0=2; plane.Zone1=1;
    model.Zones[0].ZoneActor=info; model.Zones[1].ZoneActor=air; model.Zones[2].ZoneActor=water;

    struct RestoreLevel
    {
        Engine& Game; ULevel* Level; ULevelInfo* Info;
        ~RestoreLevel() { Game.Level=Level; Game.LevelInfo=Info; }
    } restore{game,game.Level,game.LevelInfo};
    game.Level=&level; game.LevelInfo=info;
    auto pawn=[&](const char* name)
    {
        auto* p=UObject::Cast<UPawn>(make(name,"Pawn"));
        p->Level()=info; p->XLevel()=&level; p->Region().Zone=water;
        p->Role()=ROLE_Authority; p->Physics()=PHYS_None;
        return p;
    };
    auto* first=pawn("RuneCampaignWaitFirst");
    auto* second=pawn("RuneCampaignWaitSecond");
    // Clear pointers to stack-owned level/model when leaving, including failures.
    struct ClearActors
    {
        UPawn* First; UPawn* Second;
        ~ClearActors() { for(auto* p:{First,Second}) { p->StateFrame.reset(); p->XLevel()=nullptr; } }
    } clear{first,second};
    auto invoke=[&](UObject* actor,NameString name,Array<ExpressionValue> args=Array<ExpressionValue>())
    {
        auto* function=FindEventFunction(actor,name);
        require(function!=nullptr,"Campaign native probe function missing");
        return Frame::Call(function,actor,{args.data(),args.size()});
    };
    auto* state=UObject::Cast<UState>(transient->NewObject("RuneCampaignWaitState",
        game.packages->GetPackage("Core")->GetClass("State"),ObjectFlags::Transient));
    // Real VM instructions: native660 WaitForRelease(); Stop. Statement 1 is
    // reached only after the latent wait is released or its timeout expires.
    state->Code=std::make_shared<Bytecode>(Array<uint8_t>{0x62,0x94,0x16,0x08},enginePackage);
    state->StructSize=1; state->StructAlignment=1;
    auto wait=[&](UPawn* p)
    {
        p->StateFrame=std::make_shared<Frame>(p,state,0,true);
        p->StateFrame->Tick();
        require(p->StateFrame->GetLatentState()==LatentRunState::Sleep && p->LatentFloat()==9999.0f &&
            p->StateFrame->GetStatementIndex()==1,"WaitForRelease failed to suspend real state bytecode");
    };
    wait(first); wait(second);
    first->UActor::Tick(0.25f);
    require(first->StateFrame->GetStatementIndex()==1 && first->LatentFloat()==9998.75f,
        "WaitForRelease did not remain suspended with countdown");
    invoke(second,"Release");
    second->UActor::Tick(0.01f);
    require(second->StateFrame->GetLatentState()==LatentRunState::Stop && second->StateFrame->GetStatementIndex()==2 &&
        first->StateFrame->GetLatentState()==LatentRunState::Sleep,"Release affected the wrong actor or failed to resume");
    auto* action=make("RuneCampaignReleaseAction","ScriptAction");
    action->SetObject("WaitingScripter",first);
    invoke(action,"Trigger",{ExpressionValue::ObjectValue(nullptr),ExpressionValue::ObjectValue(nullptr)});
    require(first->LatentFloat()==-1.0f && first->StateFrame->GetLatentState()==LatentRunState::Sleep &&
        action->GetUObject("WaitingScripter")==nullptr,"Original ScriptAction.Trigger did not release its waiting pawn");
    first->UActor::Tick(0.01f);
    require(first->StateFrame->GetLatentState()==LatentRunState::Stop && first->StateFrame->GetStatementIndex()==2,
        "Released script failed to execute its next statement");

    // Original Release intentionally wakes ordinary Sleep too, but not other
    // latent actions; it is not remembered if issued before the next wait.
    invoke(first,"Release"); wait(first);
    for(auto latent:{LatentRunState::FinishAnim,LatentRunState::MoveTo,LatentRunState::FinishInterpolation,LatentRunState::Stop})
    {
        first->StateFrame->SetLatentState(latent); first->LatentFloat()=7.0f;
        invoke(first,"Release");
        require(first->StateFrame->GetLatentState()==latent && first->LatentFloat()==7.0f,
            "Release modified an unrelated latent action");
    }
    invoke(first,"Sleep",{ExpressionValue::FloatValue(20.0f)});
    invoke(first,"Release"); first->UActor::Tick(0.01f);
    require(first->StateFrame->GetLatentState()==LatentRunState::Stop,"Release did not wake original shared Sleep state");
    wait(first); first->LatentFloat()=0.011f; first->UActor::Tick(0.01f);
    require(first->StateFrame->GetLatentState()==LatentRunState::Stop,"Rune half-frame sleep completion threshold differs");
    require(NativeFunctions::IndexForLatentAction.at(LatentRunState::Sleep)==384 &&
        NativeFunctions::LatentActionByIndex[384]==LatentRunState::Sleep &&
        NativeFunctions::LatentActionByIndex[257]==LatentRunState::Sleep,"Rune latent save mapping/legacy read alias missing");
    std::cout << "RUNE_CAMPAIGN_WAIT_OK vm_suspend=1 actor_target=1 script_trigger=1 sleep_release=1 guards=4 timeout=1 save_index=384 legacy_read=257\n";

    auto line=[&](const vec3& a,const vec3& b)
    {
        return invoke(first,"FindWaterLine",{ExpressionValue::VectorValue(a),ExpressionValue::VectorValue(b)}).ToVector();
    };
    vec3 wet=line(vec3(0,0,40),vec3(0,0,-40));
    require(wet.z<0 && wet.z>=-1 && wet.x==0 && wet.y==0,"Waterline did not retain water-side endpoint near boundary");
    first->Region().Zone=air;
    vec3 dry=line(vec3(0,0,-40),vec3(0,0,40));
    require(dry.z>=0 && dry.z<1,"Waterline ignored body's air-zone reference");
    first->Region().Zone=water;
    vec3 same=line(vec3(0,0,-10),vec3(0,0,-90));
    require(same.z<=-10 && same.z>-11,"No-crossing same-zone result differs from original bisection");
    require(line(vec3(0,0,90),vec3(0,0,10))==vec3(0,0,10),"No-crossing opposite-zone endpoint should remain unchanged");
    require(line(vec3(1,2,3),vec3(1,2,3))==vec3(1,2,3),"Zero-length water query changed endpoint");
    // Oblique BSP plane avoids a vertical-Z-only approximation.
    plane.PlaneX=1; plane.PlaneZ=0;
    vec3 horizontal=line(vec3(40,5,7),vec3(-40,5,7));
    require(horizontal.x<0 && horizontal.x>=-1 && horizontal.y==5 && horizontal.z==7,
        "Waterline failed horizontal BSP zone crossing");
    std::cout << "RUNE_CAMPAIGN_WATER_OK wet_z=" << wet.z << " dry_z=" << dry.z << " same_zone_z=" << same.z
        << " horizontal_x=" << horizontal.x << " crossing=3 no_crossing=2 zero_length=1\n";
}
