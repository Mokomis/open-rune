#pragma once
#include "RunePose.h"
#include "Utils/Array.h"
class UPawn; class UActor; class UObject; class UFunction; class ExpressionValue; class USkelModel;
template<class T> class ArrayView;
namespace RuneFinisher {
enum class Family { Sword, Axe, Hammer, Torch, Improvised, Natural };
Family Style(UActor* weapon);
bool CanFinish(UActor* weapon);
bool Opening(UPawn* attacker,UPawn* target);
// Snapshot the opening at contact; the host's existing defense window decides admission.
struct ReplayScope {
    UActor* weapon; UPawn* target; bool opening,defended;
    const ReplayScope* previous;
    ReplayScope(UActor*,UPawn*,bool,bool);
    ~ReplayScope();
};
bool Swipe(UFunction*,UObject*,const ArrayView<ExpressionValue>&,ExpressionValue&);
void AfterCall(UFunction*,UObject*,const ArrayView<ExpressionValue>&,ExpressionValue&);
// Scope ordinary kick contact, including the original damage script it invokes.
struct KickContact {
    UPawn* previous;
    explicit KickContact(UPawn* victim);
    ~KickContact();
};
void AimThrust(UPawn* attacker,UPawn* victim);
unsigned Hands(UPawn*);
unsigned PoseCode(UPawn*); // 0 idle, 1 single follow-through, 2 edged cut, 3 paired follow-through
void PresentHands(UPawn*,unsigned code,bool held);
void ApplyPose(const USkelModel&,UActor*,Array<RuneJointPose>&);
}
