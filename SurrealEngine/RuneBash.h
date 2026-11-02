#pragma once
#include <atomic>
class NameString;
class UPawn;
class UActor;
class UObject;
namespace RuneBash {
inline std::atomic<bool> Enabled{true};
bool Compatible(UPawn* pawn);
bool Start(UPawn* pawn);
void CommitImpact(UPawn*,UPawn*,bool blocked);
void BlockPressed(UPawn* pawn);
void Tick(UPawn* pawn, float elapsed);
bool Threat(UObject* observer);
bool PrepareAnimation(UActor* actor, const NameString& sequence, float& rate, float& tween);
bool AnimationFrames(UActor* actor, float from, float to);
}
