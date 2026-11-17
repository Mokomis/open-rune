#include "Precomp.h"
#include "RuneFeedback.h"
#include "RunePhysics.h"
#include "RuneLeg.h"
#include "RuneSpider.h"
#include "RuneBash.h"
#include "Engine.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Core/UFunction.h"
#include "VM/ExpressionValue.h"
#include "Utils/Logger.h"
#ifdef OPENRUNE_ENABLE_NETWORK
#include "RuneNetwork.h"
#endif
#include <exception>

namespace RuneFeedback {
namespace {
thread_local int damageDepth=0;
UPlayerPawn* Local() {return engine&&engine->LaunchInfo.IsRune()&&engine->viewport?engine->viewport->Actor():nullptr;}
bool IsLocal(UObject* obj) {return obj&&obj==Local();}
}
void Emit(GameFeedback::Event event,float strength) {
    if(GameFeedback::Emit(event,strength)&&Trace.load())
        LogMessage("RUNE_FEEDBACK event="+std::to_string(int(event))+" strength="+std::to_string(strength));
}
void Impact(UPawn* attacker,UPawn* target,bool blocked,bool bash) {
    if(IsLocal(target)) {
        if(blocked)RuneFeedback::Emit(GameFeedback::Event::ShieldBlock,.65f);
        // Unblocked incoming damage is measured by the script completion hook.
    } else if(IsLocal(attacker))RuneFeedback::Emit(bash?GameFeedback::Event::Bash:GameFeedback::Event::Kick,blocked?.35f:1.0f);
#ifdef OPENRUNE_ENABLE_NETWORK
    if (engine && engine->NetworkCombatAuthority)
        engine->NetworkCombatAuthority->recordCombat(attacker,target,bash,blocked,bash && target->RuneBashPhase==2);
#endif
}
void Landed(UActor* actor,float downwardSpeed) {
    if(IsLocal(actor)&&downwardSpeed>450)RuneFeedback::Emit(GameFeedback::Event::Landing,std::clamp((downwardSpeed-450)/650,.15f,1.0f));
}
ScriptScope::ScriptScope(UFunction* fn,UObject* obj,const ArrayView<ExpressionValue>& args) {
    bool observeNetwork=false;
#ifdef OPENRUNE_ENABLE_NETWORK
    observeNetwork=engine && engine->NetworkCombatAuthority;
#endif
    if(!observeNetwork && !RuneLeg::Observing() && !RuneSpider::Busy(UObject::TryCast<UActor>(obj)) && RunePhysics::RequestedMode.load()==0 && (!GameFeedback::Enabled.load()||!Local()))return;
    static const NameString joint="JointDamaged",acquire="AcquireInventory",add="AddInventory";
    if(fn->Name!=joint&&fn->Name!=acquire&&fn->Name!=add)return;
    actor=UObject::TryCast<UActor>(obj);if(!actor)return;
    exceptions=std::uncaught_exceptions();
    if(fn->Name==joint&&args.size()>=2) {
        kind=1;outer=damageDepth++==0;
        if(!outer)return;
        if(args.size()>=6){hit=args[2].ToVector();momentum=args[3].ToVector();hitJoint=args[5].ToInt();}
        damage=args[0].ToInt();instigator=UObject::TryCast<UPawn>(args[1].ToObject());
        if(auto* pawn=UObject::TryCast<UPawn>(actor))health=pawn->Health();
#ifdef OPENRUNE_ENABLE_NETWORK
        if(observeNetwork && instigator) {
            auto* target=UObject::TryCast<UPawn>(actor->IsA("Shield")?actor->Owner():actor);
            if(target && target!=instigator)engine->NetworkCombatAuthority->prepareCombat(instigator,target);
        }
#endif
    } else if(IsLocal(actor)&&args.size()>=1) {
        item=UObject::TryCast<UActor>(args[0].ToObject());
        if(item) {
            if(fn->Name==acquire&&item!=actor->GetUObject("Weapon")&&item!=actor->GetUObject("Shield"))kind=3;
            else if(fn->Name==add&&item->Owner()!=actor)kind=2;
        }
    }
}
ScriptScope::~ScriptScope() {
    if(kind==1)--damageDepth;
    if(!kind||std::uncaught_exceptions()!=exceptions)return;
    if(kind>=2) {
        bool acquired=item&&!item->bDeleteMe()&&(kind==2?item->Owner()==actor:
            item==actor->GetUObject("Weapon")||item==actor->GetUObject("Shield"));
        if(acquired)RuneFeedback::Emit(GameFeedback::Event::Pickup,1);
        return;
    }
    if(!outer||damage<=0)return;
    if(auto* pawn=UObject::TryCast<UPawn>(actor);pawn && pawn->Health()>0 && pawn->Health()<health)
    {
        RuneSpider::Damaged(pawn);
        RuneLeg::AfterDamage(pawn,instigator,damage,health,hitJoint);
        RunePhysics::React(pawn,hit,momentum,hitJoint);
        RuneBash::DamageReaction(pawn,instigator,health-pawn->Health());
    }
#ifdef OPENRUNE_ENABLE_NETWORK
    if(engine && engine->NetworkCombatAuthority && instigator && !instigator->RuneKickActive && instigator->RuneBashPhase!=1) {
        auto* target=UObject::TryCast<UPawn>(actor->IsA("Shield")?actor->Owner():actor);
        if(target && target!=instigator && (actor->IsA("Shield") || target->Health()<health))
            engine->NetworkCombatAuthority->recordWeapon(instigator,target,actor->IsA("Shield"));
    }
#endif
    if(IsLocal(actor)) {
        int lost=health-Local()->Health();
        if(lost>0)RuneFeedback::Emit(GameFeedback::Event::Damage,std::clamp(lost/45.0f,.25f,1.0f));
    } else if(actor->IsA("Shield")&&IsLocal(actor->Owner()))RuneFeedback::Emit(GameFeedback::Event::ShieldBlock,1);
    else if(IsLocal(instigator)&&actor!=instigator) {
        // Kick/bash resolve their own impact type after the damage script.
        if(instigator->RuneKickActive||instigator->RuneBashPhase==1)return;
        RuneFeedback::Emit(GameFeedback::Event::WeaponHit,std::clamp(damage/50.0f,.25f,1.0f));
    }
}
}
