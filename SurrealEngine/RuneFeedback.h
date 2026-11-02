#pragma once
#include "GameFeedback.h"
class UObject;
class UActor;
class UPawn;
class UFunction;
class ExpressionValue;
template<class T> class ArrayView;
namespace RuneFeedback {
inline std::atomic<bool> Trace{false};
void Emit(GameFeedback::Event event,float strength);
void Impact(UPawn* attacker,UPawn* target,bool blocked,bool bash);
void Landed(UActor* actor,float downwardSpeed);
// Observe completion of original script calls; never replace their results.
struct ScriptScope {
    ScriptScope(UFunction* fn,UObject* obj,const ArrayView<ExpressionValue>& args);
    ~ScriptScope();
    UActor* actor=nullptr;
    UActor* item=nullptr;
    UPawn* instigator=nullptr;
    int kind=0,health=0,damage=0,exceptions=0;
    bool outer=false;
};
}
