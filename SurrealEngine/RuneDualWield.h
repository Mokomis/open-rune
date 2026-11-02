#pragma once
#include "RunePose.h"
#include <atomic>
class UPawn;
class UActor;
class UObject;
class UFunction;
class UWeapon;
class USkelModel;
class ExpressionValue;
template<class T> class Array;
template<class T> class ArrayView;
namespace RuneDualWield {
inline std::atomic<bool> Enabled{true};
struct State {
    UWeapon* offhand=nullptr;
    UWeapon* drawAfterRelease=nullptr;
    bool left=false,nextLeft=false,guard=false,damageHook=false,initialized=false,spawnPending=false;
    bool contactGuardOverride=false,contactGuard=false;
    float guardBlend=0,recovery=0;
    double lastWholeBody=-1; // Transient pose recovery, never a combat timing rule.
    int rightSwings=0,leftSwings=0,blocks=0;
};
bool Eligible(UPawn* p);
bool OneHanded(UWeapon* weapon);
bool Equipped(UPawn* p);
bool Equip(UPawn* p,UWeapon* weapon);
void Unequip(UPawn* p,bool drop=false);
void Tick(UPawn* p,float elapsed);
void UpdatePoseState(UPawn* p);
void BeginSwing(UPawn* p);
void Guard(UPawn* p,bool raised);
bool Blocks(UPawn* target,UPawn* attacker);
void BreakGuard(UPawn* p,float seconds);
bool ScriptCall(UFunction* function,UObject* instance,const ArrayView<ExpressionValue>& args,ExpressionValue& result);
void ApplyPose(const USkelModel& model,UActor* actor,Array<RuneJointPose>& pose);
mat4 Attachment(const USkelModel& model,UActor* parent,UActor* child,const Array<mat4>& bones);
}
