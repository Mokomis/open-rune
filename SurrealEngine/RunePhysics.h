#pragma once
#include "RunePose.h"
#include "Utils/Array.h"
#include <atomic>
#include <string>
class UActor;
class UPawn;
class UPlayerPawn;
class ULevel;
class Engine;
class UObject;
class UFunction;
class ExpressionValue;
template <class T> class ArrayView;
namespace openrune::net {
struct PhysicalPose;
}
namespace RunePhysics {
struct PresentationScope {
  PresentationScope();
  ~PresentationScope();
  PresentationScope(const PresentationScope &) = delete;
};
enum Mode { Original = 0, Cinematic = 1, Chaos = 2 };
inline std::atomic<int> RequestedMode{Original};
struct Counters {
  unsigned ragdolls = 0, props = 0, awake = 0, bodies = 0;
  double stepMs = 0;
  unsigned wallSlams = 0, brokenProps = 0;
};
int mode();
void Reset();
void Tick(ULevel *, float elapsed, bool paused);
bool Controls(UActor *);
// Restores motion only from a validated authoritative rigid-body owner.
bool RepairInvalidMotion(UActor *);
bool IsProp(UActor *);
bool SafeProp(UActor *);
void Hazard(UActor *,
            int kind); // Explicit scene authoring: 1=impact spikes, 2=fire.
bool Carrying(UPawn *);
bool UseProp(UPawn *);
bool ThrowProp(UPawn *);
// Shared placement for isolated visual/native playground fixtures.
bool FindImpactLane(UPlayerPawn *, vec3 &, vec3 &, vec3 &);
bool KickObject(UPawn *);
bool ApplyPose(UActor *, Array<RuneJointPose> &);
bool CustomTransform(UActor *);
mat4 MeshToWorld(UActor *);
vec3 RenderOffset(UActor *);
bool Ragdoll(UActor *, vec3 velocity, UPawn *instigator = nullptr,
             bool living = false);
bool Knockdown(UPawn *, UPawn *instigator, vec3 velocity, float strength = 1);
// Shield-rush knockdowns also run in Original mode without enabling ambient props/corpses.
bool ShieldRushKnockdown(UPawn *, UPawn *instigator, vec3 velocity);
bool Prop(UActor *, vec3 velocity, UPawn *instigator = nullptr);
void Impact(UPawn *attacker, UPawn *target, vec3 velocity, bool blocked);
void Release(UActor *);
void OnDestroy(UActor *);
void BeforeSave();
bool ScriptCall(UFunction *, UObject *, const ArrayView<ExpressionValue> &,
                ExpressionValue &);
void AfterScriptCall(UFunction *, UObject *,
                     const ArrayView<ExpressionValue> &);
Counters Stats();
std::string Status();
void RunProbe(Engine &);
#ifdef OPENRUNE_ENABLE_NETWORK
bool Capture(UActor *, openrune::net::PhysicalPose &);
bool ValidateReplica(UActor *, const openrune::net::PhysicalPose &);
void Present(UActor *, const openrune::net::PhysicalPose &,
             const openrune::net::PhysicalPose &, float);
#endif
} // namespace RunePhysics
