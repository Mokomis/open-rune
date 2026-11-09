#pragma once
#include "RunePose.h"
#include "Utils/Array.h"
#include <vector>
class UActor; class UPawn; class UObject; class UFunction; class ExpressionValue; class USkelModel;
namespace RuneCorpse {
struct LimbMotion {
    int joint=-1,child=-1;
    float cone=0,spring=0;
    vec3 tip{0},anchor{0},velocity{0};
};
// Transient, actor-owned state. Saving sets manipulated bodies down first.
struct BodyState {
    bool active=false, flying=false, centered=false, presented=false;
    const USkelModel* poseModel=nullptr;
    Array<RuneJointPose> pose;
    Array<RuneJointPose> motionPose;
    std::vector<LimbMotion> limbs;
    mat4 motionWorld=mat4::identity();
    bool motionReady=false;
    float supportDepth=0;
    bool collide=false, blockActors=false, blockPlayers=false, world=false;
    float radius=0,height=0,life=0,age=0,carry=0;
    vec3 center{0},lower{0},upper{0};
    UPawn* thrower=nullptr;
    std::vector<UPawn*> victims;
};
struct CarryState {
    UActor* body=nullptr;
    float age=0,release=-1,cooldown=0,think=.4f;
    unsigned pickups=0,throws=0,hits=0,blocks=0;
};
bool Eligible(UPawn*);
vec3 InteractionPoint(UActor*);
bool Throwable(UActor*);
UActor* Held(UPawn*); // Also resolves presentation replicas from attachment links.
bool Busy(UPawn*);
bool Pickup(UPawn*,UActor*);
bool Use(UPawn*);
bool Throw(UPawn*);
void Drop(UPawn*);
void Tick(UPawn*,float);
bool TickBody(UActor*,float);
void OnDestroy(UActor*);
void BeforeSave();
float SpeedScale(UPawn*);
float Windup(UPawn*);
bool ScriptCall(UFunction*,UObject*,const ArrayView<ExpressionValue>&,ExpressionValue&);
Array<RuneJointPose> RelaxedPose(const USkelModel&);
Array<RuneJointPose> ReferencePose(const USkelModel&);
bool Posed(UActor*);
void Presentation(UActor*,bool);
void ApplyPose(const USkelModel&,UActor*,Array<RuneJointPose>&);
void ResetMotion(UActor*);
void TickMotion(UActor*,float); // Once per presentation frame, never from a draw pass.
void TickMotion(float);
bool CustomTransform(UActor*);
mat4 MeshToWorld(UActor*);
vec3 SnapshotLocation(UActor*,vec3);
}
