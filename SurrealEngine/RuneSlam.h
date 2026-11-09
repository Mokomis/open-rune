#pragma once
#include "RunePose.h"
class UPawn;class UActor;class UObject;class UFunction;class ExpressionValue;class UWeapon;class USkelModel;
template<class T>class Array;template<class T>class ArrayView;
namespace RuneSlam {
constexpr float Windup=.45f,Recovery=.32f;
constexpr float LiftSpeed=600.f,DiveSpeed=850.f,MaxDiveTime=1.25f;
struct State {
    int phase=0,hits=0;float age=0,cooldown=0,armed=0,tap=-1,air=0;
    bool launchRestored=false;
    vec3 launchVelocity{0};
    UWeapon* weapon=nullptr;
};
bool Heavy(UWeapon*);
bool Supported(UPawn*);
bool Busy(UPawn*);
bool Threat(UObject*);
bool Arm(UPawn*);
bool CanStart(UPawn*);
bool Start(UPawn*);
void Cancel(UPawn*);
void Tick(UPawn*,float);
void Landed(UPawn*,UActor*);
void BeforeSave();
void Apply(const USkelModel&,UActor*,Array<RuneJointPose>&);
bool ScriptCall(UFunction*,UObject*,const ArrayView<ExpressionValue>&,ExpressionValue&);
}
