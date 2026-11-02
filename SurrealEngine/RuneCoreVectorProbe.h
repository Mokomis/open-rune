#pragma once

#include "Engine.h"
#include "Package/Package.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UFunction.h"
#include "VM/Frame.h"
#include "VM/ScriptCall.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Exercise the original Core.Object function metadata and Rune's native index,
// not a direct call to the shared C++ vector helper.
inline void RunRuneCoreVectorProbe(Engine& game)
{
    auto require=[](bool ok,const char* why) { if(!ok) throw std::runtime_error(why); };
    auto* object=game.packages->GetTransientPackage()->NewObject("RuneCoreVectorFixture",
        game.packages->GetPackage("Core")->GetClass("Object"),ObjectFlags::Transient);
    auto* function=FindEventFunction(object,"VSize2D");
    require(function && function->NativeFuncIndex==662,"Original Rune VSize2D declaration/index is missing");
    auto size=[&](vec3 value) {
        Array<ExpressionValue> args={ExpressionValue::VectorValue(value)};
        return Frame::Call(function,object,{args.data(),args.size()}).ToFloat();
    };
    require(size(vec3(3,4,0))==5 && size(vec3(-3,-4,9999))==5,
        "Rune VSize2D must return unsigned XY magnitude and ignore Z");
    require(size(vec3(0,0,-9999))==0,"Rune VSize2D vertical-only vector must return zero");
    require(std::abs(size(vec3(0.3f,0.4f,50))-0.5f)<0.000001f,"Rune VSize2D lost fractional magnitude");
    // PlayerRopeClimbing.LeapOff compares horizontal deviation with >50.
    require(!(size(vec3(30,40,10000))>50) && size(vec3(30,41,-10000))>50,
        "Rune VSize2D changed the original rope-release threshold");
    std::cout << "RUNE_CORE_VSIZE2D_OK native=662 xy=5 sign=1 ignores_z=1 vertical_zero=1 fractional=0.5 rope_threshold=1\n";
}
