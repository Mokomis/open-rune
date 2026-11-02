#pragma once

#include "Engine.h"
#include "Package/Package.h"
#include "Package/PackageManager.h"
#include "Packages/Engine/Actors/Brush/MovingBrush.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include "VM/Frame.h"
#include "VM/ScriptCall.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

inline void RunRunePolyobjNativeProbe(Engine& game)
{
    auto require=[](bool ok,const char* why) { if(!ok) throw std::runtime_error(why); };
    auto* transient=game.packages->GetTransientPackage();
    auto* enginePackage=game.packages->GetPackage("Engine");
    auto* brush=UObject::Cast<UBrush>(transient->NewObject("RunePolyobjNativeFixture",enginePackage->GetClass("Polyobj"),ObjectFlags::Transient));
    ULevel level("RunePolyobjNativeLevel",nullptr,ObjectFlags::Transient);
    UModel world("RunePolyobjNativeWorld",nullptr,ObjectFlags::Transient);
    UModel model("RunePolyobjNativeBrush",nullptr,ObjectFlags::Transient);
    level.Model=&world; brush->XLevel()=&level; brush->Brush()=&model;
    struct Clear { UBrush* Brush; ~Clear() { Brush->XLevel()=nullptr; Brush->Brush()=nullptr; } } clear{brush};
    model.BoundingBox=BBox(vec3(-2,-3,-5),vec3(2,3,5));
    brush->Location()=vec3(0); brush->Rotation()={0,0,0}; brush->PrePivot()=vec3(0); brush->MainScale().Scale=vec3(1);
    auto invoke=[&](const char* name) { return Frame::Call(FindEventFunction(brush,name),brush,{}); };
    require(std::abs(invoke("GetCollisionRadius").ToFloat()-std::sqrt(38.0f))<0.001f,
        "Polyobj radius is not local AABB half diagonal");
    brush->MainScale().Scale=vec3(2,3,4); brush->Rotation()={0,8192,0};
    brush->Location()=vec3(100,-40,7); brush->PrePivot()=vec3(9,2,-4);
    float radius=invoke("GetCollisionRadius").ToFloat();
    require(std::abs(radius-std::sqrt(569.0f))<0.002f,
        "Polyobj radius ignored rotation/scale or included translation/pivot in size");

    auto texture=[&](const char* name) { return UObject::Cast<UTexture>(transient->NewObject(name,enginePackage->GetClass("Texture"),ObjectFlags::Transient)); };
    auto* a=texture("RunePolyobjTextureA"); auto* b=texture("RunePolyobjTextureB");
    world.Surfaces.resize(4); model.Nodes.resize(4);
    for(int i=0;i<4;i++) { world.Surfaces[i]={}; model.Nodes[i]={}; model.Nodes[i].Surf=i; }
    world.Surfaces[0].Material=a; world.Surfaces[1].Material=b; world.Surfaces[2].Material=b; world.Surfaces[3].Material=a;
    require(invoke("GetTexture").ToObject()==a,"Polyobj texture final-count tie must prefer first surface");
    world.Surfaces[3].PolyFlags=PF_Invisible;
    require(invoke("GetTexture").ToObject()==b,"Polyobj texture did not exclude invisible surface or choose majority");
    world.Surfaces[3].PolyFlags=0; a->AnimCurrent()=b;
    require(invoke("GetTexture").ToObject()==b,"Polyobj texture did not resolve current animation frame");
    world.Surfaces[0].Material=nullptr; world.Surfaces[1].Material=nullptr; world.Surfaces[2].Material=nullptr;
    require(invoke("GetTexture").ToObject()==nullptr,"Polyobj texture null majority differs");
    model.Nodes.clear();
    require(invoke("GetTexture").ToObject()==nullptr,"Empty Polyobj texture query did not return None");
    std::cout << "RUNE_POLYOBJ_NATIVE_OK transformed_radius=" << radius
        << " majority=1 invisible=1 tie_first=1 animated=1 null=1 empty=1\n";
}
