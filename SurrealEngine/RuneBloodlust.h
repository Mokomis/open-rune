#pragma once
#include "Math/vec.h"
#include "Utils/Array.h"
#include "Package/NameString.h"
#include <atomic>
#include <vector>
class UPawn; class UActor; class UObject; class UFunction; class ExpressionValue;
template<class T> class ArrayView;
namespace RuneBloodlust {
inline std::atomic<bool> Enabled{true};
inline std::atomic<bool> AllowNormal{false};
inline std::atomic<unsigned> ReadyMask{0};
enum Phase { None=0, LeapWindup=1, Leaping=2, ChargeWindup=3, Charging=4, Recovery=5 };
struct State {
    int phase=0, leaps=0, charges=0, hits=0;
    unsigned ready=0; // presentation-only server readiness on replicas
    float age=0,cooldown=0,tap=-1,rage=0,think=.5f,limit=600,travel=0,blocked=0;
    bool active=false,damageHook=false,bodyHook=false,rushing=false;
    vec3 direction{0},origin{0};
    NameString animation;
    std::vector<UPawn*> victims;
};
bool Eligible(UPawn*);
bool Busy(UPawn*);
bool Active(UPawn*);
unsigned Readiness(UPawn*);
bool Leap(UPawn*,vec3 direction);
bool Charge(UPawn*);
bool JumpPressed(UPawn*);
void Tick(UPawn*,float);
void Move(UPawn*,float);
bool ScriptCall(UFunction*,UObject*,const ArrayView<ExpressionValue>&,ExpressionValue&);
bool PrepareAnimation(UActor*,const NameString&,float&,float&);
bool AnimationFrames(UActor*);
}
