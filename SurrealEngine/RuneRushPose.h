#pragma once
#include "RunePose.h"
class UPawn;
class UActor;
class USkelModel;
template<class T> class Array;
namespace RuneRushPose {
constexpr float StowSeconds=.24f;
constexpr float PlayerPrepare=.52f;
constexpr float EnemyPrepare=.68f;
constexpr float RecoverSeconds=.40f;
bool Supported(UPawn* pawn);
void Apply(const USkelModel& model,UActor* actor,Array<RuneJointPose>& pose);
}
