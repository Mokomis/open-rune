#pragma once
#include "RunePose.h"
#include "Utils/Array.h"
class UPawn;class UActor;class UObject;class UFunction;class USkelModel;class Engine;class ExpressionValue;
namespace RuneSpider {
struct State {
 bool active=false,mantle=false;float forward=0,strafe=0,phase=0,age=0,stalled=0,think=.4f,cooldown=0;int step=0;
 vec3 normal{0},last{0},exit{0},path[3]{};bool npc=false;
 UActor* hidden[3]{};bool wasHidden[3]{};
};
bool Busy(UActor*);
bool Eligible(UPawn*);
bool Start(UPawn*);
void Stop(UPawn*,bool jump=false);
void Damaged(UPawn*);
void Input(UPawn*,float forward,float strafe);
bool Tick(UPawn*,float);
void Move(UPawn*,float);
bool Plan(UPawn*,vec3 goal,vec3& normal,vec3& exit);
void BeforeSave();
bool ScriptCall(UFunction*,UObject*,const ArrayView<ExpressionValue>&,ExpressionValue&);
void ApplyPose(const USkelModel&,UActor*,Array<RuneJointPose>&);
void Probe(Engine&);
void Demo(Engine&,int);
}
