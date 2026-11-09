#include "Precomp.h"
#include "RuneStairPresentation.h"
#include "Engine.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Utils/CommandLine.h"

namespace RuneStairPresentation {
namespace { Height height; UPlayerPawn* focus=nullptr; float offset=0; }
void Reset(){height.reset();focus=nullptr;offset=0;}
void Update(UPlayerPawn* p,float dt) {
    static const bool enabled=commandline->GetArgInt("","--rune-local-stair-smoothing",1)!=0;
    if(!enabled || !p || !engine->LaunchInfo.IsRune() || !p->IsA("RunePlayer")){Reset();return;}
    if(focus!=p){Reset();focus=p;}
    auto* zone=p->Region().Zone;
    bool eligible=!p->NetworkReplica && p->Health()>0 && p->Physics()==PHYS_Walking &&
        p->bBehindView() && !p->ViewTarget() && engine->CameraActor==p && !p->bFixedCamera() &&
        !p->GetBool("bCameraLock") && !p->GetBool("bCameraOverhead") && p->GetStateName()!="Scripting" &&
        !(zone && zone->HasProperty("bTakeOverCamera") && zone->GetBool("bTakeOverCamera"));
    vec3 at=p->Location();
    float anchorOffset=p->GetVector("OldCameraStart").z-(at.z+p->PrePivot().z+p->GetFloat("CameraHeight"));
    offset=height.update(at.x,at.y,at.z,anchorOffset,dt,p->MaxStepHeight(),eligible);
}
float Offset(UActor* actor) {
    if(!focus || !offset || !actor || actor->NetworkReplica)return 0;
    // Held weapons, shields and bodies follow the same presentation as the
    // player. The authoritative attachment/joint transforms stay untouched.
    for(int depth=0;actor && depth<17;++depth) {
        if(actor==focus)return offset;
        if(!actor->bCarriedItem())break;
        actor=UObject::TryCast<UActor>(actor->GetUObject("AttachParent"));
    }
    return 0;
}
}
