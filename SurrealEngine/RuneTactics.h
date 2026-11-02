#pragma once
#include <atomic>
class UPawn;
class UObject;
namespace RuneTactics {
enum class Choice { Wait, Approach, Retreat, Flank, Block, Attack, Kick, Bash };
inline std::atomic<bool> Enabled{true};
// Stored on each pawn: no global actor pointers survive travel or save/load.
struct State {
    bool active=false, attacking=false, blocking=false, special=false;
    bool sawThreat=false, sawBlock=false;
    float think=0, visibleFor=0, threatFor=0, blockingFor=0;
    float recovery=0, actionAge=0, actionLimit=0, lostSight=0, defenseRoll=1;
    float moveTime=0, repositionCooldown=0, counterWindow=0;
    int lastAttackVariant=-1;
    Choice move=Choice::Wait;
    int side=1;
    int attacks=0, blocks=0, kicks=0, bashes=0, moves=0, decisions=0;
    UPawn* observed=nullptr; // compared only; dereference always uses current Enemy
};
bool Eligible(UPawn* pawn);
bool Controls(UObject* object);
bool MayCommit(UPawn* pawn);
bool SuppressRandomSpecial(UPawn* pawn);
void Tick(UPawn* pawn,float elapsed);
}
