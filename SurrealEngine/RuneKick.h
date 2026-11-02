#pragma once
#include <atomic>
class UPawn;
class UObject;
namespace RuneKick {
inline std::atomic<bool> Enabled{true};
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
