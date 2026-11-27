#pragma once
#include "RunePose.h"
#include <cstdint>
#include <vector>
#include "Utils/Array.h"
#include "Math/mat.h"
class UActor;class UPawn;class UObject;class UFunction;class USkelModel;class Engine;class VisibleFrame;class ExpressionValue;
template<class T>class ArrayView;
namespace RuneLeg {
// Existing saved limb-health slots carry the injury. No source package changes.
inline constexpr int Severed=-30001;
struct Contact {UActor* weapon=nullptr;UPawn* attacker=nullptr;UPawn* target=nullptr;int side=-1;};
Contact Exposure(UActor*,UPawn*,uint32_t low,uint32_t high,vec3 hit);
struct ReplayScope {Contact previous;explicit ReplayScope(Contact);~ReplayScope();};
struct WeaponScope {Contact previous;WeaponScope(UFunction*,UObject*,const ArrayView<ExpressionValue>&);~WeaponScope();};
bool Observing();
void AfterDamage(UPawn*,UPawn*,int damage,int before,int joint);
void AfterScriptCall(UFunction*,UObject*,const ArrayView<ExpressionValue>&);
const std::vector<uint8_t>* TriangleMask(UActor*);
void SeverForFinisher(UPawn*,int side);
int Missing(UActor*); // -1 intact, 0 left leg, 1 right leg
void ReplicaAppearance(UActor*);
float SpeedScale(UPawn*);
void ApplyPose(const USkelModel&,UActor*,Array<RuneJointPose>&);
bool LooseTransform(UActor*,mat4&);
bool Detached(UActor*);
bool LandDetached(UActor* leg,UActor* support);
bool TipDetached(UActor* leg,float elapsed);
void Draw(VisibleFrame*);
void Probe(Engine&);
void Demo(Engine&,int frame);
}
