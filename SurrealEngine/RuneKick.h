#pragma once
#include <atomic>
class UPawn;class UActor;class NameString;
class UObject;
namespace RuneKick {
inline std::atomic<bool> Enabled{true};
inline constexpr float ContactSeconds=.375f, AIIntervalMin=5.f, AIIntervalSpan=2.f, AIChance=.4f;
void PrepareAnimation(UActor*,const NameString&,float& rate,float& tween);
bool ConsiderAIKick(UPawn*);
bool Compatible(UPawn* pawn);
bool Start(UPawn* pawn);
bool Notify(UObject* instance);
void CommitImpact(UPawn*,UPawn*,bool blocked);
bool Threat(UObject* observer);
void Tick(UPawn* pawn,float elapsed);
void Recoil(UPawn* pawn,float elapsed);
bool ShieldBlocks(UPawn* target,UPawn* attacker);
UPawn* Target(UPawn* attacker);
}
