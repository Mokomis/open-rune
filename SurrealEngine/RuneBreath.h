#pragma once
#include "Math/vec.h"
#include "RunePose.h"
#include "Utils/Array.h"
#include <atomic>
class UPawn;
class UActor;
class USkelModel;
class Engine;
namespace RuneBreath {
inline std::atomic<bool> Enabled{true};
bool Undead(UPawn*);
// Shared by body/proxy sampling and menu previews; independent of vapor settings.
void IdlePose(const USkelModel&,UActor*,size_t sequence,Array<RuneJointPose>&,double idleSeconds=-1);
bool Eligible(UPawn*);
bool Mouth(UPawn*,vec3& position,vec3& forward);
bool Outside(const vec3&);
void Probe(Engine&);
void Demo(Engine&,int frame);
}
