#pragma once

#include "Engine.h"
#include "Package/Package.h"
#include "Package/PackageManager.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/NavigationPoint/UNavigationPoint.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/UCanvas.h"
#include "Render/RenderSubsystem.h"
#include "VM/Frame.h"
#include "VM/ScriptCall.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

inline void RunRuneNavigationCanvasProbe(Engine& game)
{
    auto require=[](bool ok,const char* why) { if(!ok) throw std::runtime_error(why); };
    auto* transient=game.packages->GetTransientPackage();
    auto* package=game.packages->GetPackage("Engine");
    auto make=[&](const char* name,const char* cls) { return transient->NewObject(name,package->GetClass(cls),ObjectFlags::Transient); };
    auto* info=UObject::Cast<ULevelInfo>(make("RuneNavProbeInfo","LevelInfo"));
    auto* pawn=UObject::Cast<UPawn>(make("RuneNavProbePawn","Pawn"));
    auto* a=UObject::Cast<UNavigationPoint>(make("RuneNavProbeA","NavigationPoint"));
    auto* b=UObject::Cast<UNavigationPoint>(make("RuneNavProbeB","NavigationPoint"));
    auto* c=UObject::Cast<UNavigationPoint>(make("RuneNavProbeC","NavigationPoint"));
    ULevel level("RuneNavProbeLevel",nullptr,ObjectFlags::Transient);
    struct Clear {
        UPawn* Pawn; UNavigationPoint* A; UNavigationPoint* B; UNavigationPoint* C;
        ~Clear() { Pawn->XLevel()=nullptr; for(auto* n:{A,B,C}) n->XLevel()=nullptr; }
    } clear{pawn,a,b,c};
    pawn->Level()=info; pawn->XLevel()=&level;
    for(auto* n:{a,b,c}) { n->Level()=info; n->XLevel()=&level; for(auto& path:n->Paths()) path=-1; }
    auto invoke=[&](UObject* object,const char* name,Array<ExpressionValue> args=Array<ExpressionValue>()) {
        auto* function=FindEventFunction(object,name); require(function!=nullptr,"Navigation/canvas probe function missing");
        return Frame::Call(function,object,{args.data(),args.size()});
    };
    auto nearest=[&]() { return invoke(pawn,"NearestNavPoint").ToObject(); };
    info->NavigationPointList()=nullptr;
    require(nearest()==nullptr,"NearestNavPoint empty list must return None");
    info->NavigationPointList()=a; a->nextNavigationPoint()=b; b->nextNavigationPoint()=nullptr;
    pawn->Location()=vec3(100,200,300); a->Location()=vec3(110,200,300); b->Location()=vec3(100,200,311);
    a->bPlayerOnly()=true; a->bEndPointOnly()=true;
    require(nearest()==a,"NearestNavPoint must compare 3D distances without navigation eligibility checks");
    b->Location().z=305;
    require(nearest()==b,"NearestNavPoint ignored vertical distance");
    b->Location().z=310;
    require(nearest()==a,"NearestNavPoint tie must retain first list entry");
    a->nextNavigationPoint()=nullptr; a->Location()=pawn->Location()+vec3(3162,0,0);
    require(nearest()==a,"NearestNavPoint rejected point inside original cutoff");
    a->Location()=pawn->Location()+vec3(3163,0,0);
    require(nearest()==nullptr,"NearestNavPoint ignored original distance cutoff");

    // Execute the original Pawn.FindPathAwayFrom bytecode used by Fleeing,
    // including NumPaths/PathEndPoint/describeSpec rather than a C++ imitation.
    pawn->Location()=vec3(0); a->Location()=vec3(1,0,0);
    b->Location()=vec3(100,0,0); c->Location()=vec3(300,0,0);
    auto* threat=UObject::Cast<UPawn>(make("RuneNavProbeThreat","Pawn")); threat->Location()=vec3(-1000,0,0);
    level.ReachSpecs.resize(2);
    level.ReachSpecs[0].startActor=a; level.ReachSpecs[0].endActor=b;
    level.ReachSpecs[1].startActor=a; level.ReachSpecs[1].endActor=c;
    a->Paths()[0]=0; a->Paths()[1]=1;
    require(invoke(pawn,"FindPathAwayFrom",{ExpressionValue::ObjectValue(threat),ExpressionValue::ObjectValue(nullptr)}).ToObject()==c,
        "Original flee helper did not select furthest outgoing point");
    require(invoke(pawn,"FindPathAwayFrom",{ExpressionValue::ObjectValue(threat),ExpressionValue::ObjectValue(c)}).ToObject()==b,
        "Original flee helper did not exclude justvisited");
    std::cout << "RUNE_NEAREST_NAV_OK empty=1 distance3d=1 eligibility_ignored=1 tie_first=1 cutoff=1 original_flee=1 excluded_previous=1\n";

    auto* canvas=UObject::Cast<UCanvas>(make("RuneProjectionProbe","Canvas"));
    canvas->SizeX()=800; canvas->SizeY()=600;
    // Use actual property references as VM out parameters, avoiding a test
    // that accidentally inspects copies of the argument values.
    auto project=[&](const vec3& point) {
        auto x=ExpressionValue::Variable(canvas->PropertyData.Ptr(size_t(0)),canvas->GetMemberProperty("FramePtr"));
        auto y=ExpressionValue::Variable(canvas->PropertyData.Ptr(size_t(0)),canvas->GetMemberProperty("RenderPtr"));
        invoke(canvas,"TransformPoint",{ExpressionValue::VectorValue(point),x,y});
        return std::pair<int,int>(canvas->FramePtr(),canvas->RenderPtr());
    };
    struct RestoreRender {
        Engine& Game; std::unique_ptr<RenderSubsystem> Render;
        ~RestoreRender() { Game.render=std::move(Render); }
    } restore{game,std::move(game.render)};
    canvas->FramePtr()=17; canvas->RenderPtr()=29;
    require(project(vec3(0,0,100))==std::pair<int,int>(17,29),"TransformPoint without frame changed output parameters");
    game.render=std::make_unique<RenderSubsystem>(nullptr);
    require(project(vec3(0,0,100))==std::pair<int,int>(17,29),"TransformPoint read uninitialized scene frame");
    auto& frame=game.render->MainFrame.Frame;
    frame.FX=800; frame.FY=600; frame.FX2=400; frame.FY2=300;
    frame.WorldToView=mat4::identity(); frame.ObjectToWorld=mat4::identity();
    frame.Projection=mat4::frustum(-1.0f,1.0f,-0.75f,0.75f,1.0f,32768.0f,handedness::left,clipzrange::zero_positive_w);
    game.render->Stats.Frames=1;
    require(project(vec3(0,0,100))==std::pair<int,int>(400,300),"TransformPoint center projection differs");
    auto offset=project(vec3(10,-20,100));
    require(std::abs(offset.first-440)<=1 && std::abs(offset.second-220)<=1,"TransformPoint axes/aspect projection differs");
    require(project(vec3(0.19f,0.09f,100))==std::pair<int,int>(400,300),"TransformPoint did not truncate toward zero");
    require(project(vec3(-100.2f,0,100)).first==0,"TransformPoint negative coordinate rounding differs");
    require(project(vec3(200,0,100)).first==1200,"TransformPoint incorrectly clipped offscreen coordinates");
    for(float z:{-100.0f,0.0f,0.99f,1.0f})
        require(project(vec3(0,0,z))==std::pair<int,int>(-1000,-1000),"TransformPoint near/behind sentinel differs");
    require(project(vec3(0,0,1.01f))==std::pair<int,int>(400,300),"TransformPoint rejected point beyond original near threshold");
    frame.FX=1600; frame.FY=1200; frame.FX2=800; frame.FY2=600;
    require(project(vec3(10,-20,100))==offset,"TransformPoint scene resolution changed logical UI position");
    vec3 camera(40,70,20); Rotator rotation(0,16384,0);
    frame.WorldToView=Coords::ViewToRenderDev().ToMatrix()*Coords::Rotation(rotation).Inverse().ToMatrix()*Coords::Location(camera).ToMatrix();
    auto rotated=project(camera+vec3(0,100,0));
    require(std::abs(rotated.first-400)<=1 && std::abs(rotated.second-300)<=1,"TransformPoint ignored camera translation/rotation");
    std::cout << "RUNE_CANVAS_TRANSFORM_OK center=1 axes=1 truncation=1 offscreen=1 near=1 no_frame=1 scaled_ui=1 camera=1\n";
}
