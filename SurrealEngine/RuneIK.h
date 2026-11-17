#pragma once
#include "RunePose.h"
#include "Utils/Array.h"
class USkelModel;class UActor;class Engine;class UPlayerPawn;
namespace RuneIK {
struct Metrics {unsigned solves=0,cacheHits=0,groundedFeet=0;float maxCorrection=0,maxLengthError=0;};
void Apply(const USkelModel&,UActor*,Array<RuneJointPose>&);
void ApplyAtWorld(const USkelModel&,UActor*,Array<RuneJointPose>&,const mat4&);
void SetGrounded(UActor*,bool);
void Forget(UActor*);
void Reset();
Metrics Stats();
void RunProbe(Engine&,UPlayerPawn*);
}
