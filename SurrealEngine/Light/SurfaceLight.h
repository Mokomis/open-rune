#pragma once
#include "Math/vec.h"
#include "Packages/Engine/Resources/Textures/UBitmap.h"

// A lightmap retains ambient, unusual effects and overflow lights. Ordinary
// point/wave lights are evaluated per fragment, with their original baked
// visibility masks. Coordinates match the lightmap builder, including movers.
struct SurfaceLight
{
    struct Light { vec3 Position, Color; float InvRadiusSquared, WaveSpeed; };
    Light Lights[8];
    int Count = 0;
    vec3 Origin, U, V, Normal;
    float Time = 0;
    UnrealMipmap Shadows[2];
    bool ShadowChanged[2] = {true, true};
};
