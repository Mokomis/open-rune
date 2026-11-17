#pragma once
#include <atomic>
#include "RunePose.h"
#include "Utils/Array.h"
class USkelModel;
class NameString;
class UPawn;
class UActor;
class UObject;
namespace RuneBash {
inline std::atomic<bool> Enabled{true};
bool Compatible(UPawn* pawn);
// Brief heavy-hit stagger in Cinematic/Chaos; original hit/damage remains authoritative.
bool DamageReaction(UPawn* target,UPawn* instigator,int healthLost);
void PresentImpact(UPawn* target,bool staggered);
void ApplyPose(const USkelModel&,UActor*,Array<RuneJointPose>&);
bool Start(UPawn* pawn);
void CommitImpact(UPawn*,UPawn*,bool blocked);
void BlockPressed(UPawn* pawn);
void Tick(UPawn* pawn, float elapsed);
bool Threat(UObject* observer);
bool PrepareAnimation(UActor* actor, const NameString& sequence, float& rate, float& tween);
bool AnimationFrames(UActor* actor, float from, float to);
}
