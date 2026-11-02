#pragma once
#include "Packages/Engine/Actors/Brush/MovingBrush.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"

inline uint32_t CollisionModelSurfaceFlags(UActor* owner,UModel* geometry,int index)
{
    // Rune Polyobj BSP nodes index the world's surface table (also used by
    // original native GetTexture), while vertices/hulls remain brush-local.
    // Treating these indices as brush-local crashes lighting traces in HelLift.
    UModel* surfaces=IsRunePolyobj(owner) && owner->XLevel()?owner->XLevel()->Model:geometry;
    if(!surfaces || index<0 || (size_t)index>=surfaces->Surfaces.size()) return 0;
    return surfaces->Surfaces[index].PolyFlags;
}
