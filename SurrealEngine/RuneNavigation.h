#pragma once
#include "Math/vec.h"
#include <span>
class UPawn;class UActor;class ULevel;class UNavigationPoint;
namespace RuneNavigation {
void Reset();
// Runtime reverse view of validated authored links; never changes map arrays.
std::span<const int> Incoming(ULevel*,UNavigationPoint*);
void Tick(ULevel*,float,bool);
void OnDestroy(UActor*);
UActor* Fallback(UPawn*,vec3 goal);
}
