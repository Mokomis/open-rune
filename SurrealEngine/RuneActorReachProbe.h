#pragma once

#include "Engine.h"
#include "Package/Package.h"
#include "Package/PackageManager.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "VM/Frame.h"
#include "VM/ScriptCall.h"

// Original native520's close-pawn early return, through the real VM binding.
// Synthetic actors and wall geometry never enter a campaign map or save.
inline void RunRuneActorReachProbe(Engine& game)
{
    auto require=[](bool ok,const char* why) { if(!ok) throw std::runtime_error(why); };
    auto* transient=game.packages->GetTransientPackage();
    auto* package=game.packages->GetPackage("Engine");
    auto make=[&](const char* name,const char* cls) { return transient->NewObject(name,package->GetClass(cls),ObjectFlags::Transient); };
    auto* info=UObject::Cast<ULevelInfo>(make("RuneReachInfo","LevelInfo"));
    auto* zone=UObject::Cast<UZoneInfo>(make("RuneReachZone","ZoneInfo"));
    auto* targetZone=UObject::Cast<UZoneInfo>(make("RuneReachTargetZone","ZoneInfo"));
    auto* pawn=UObject::Cast<UPawn>(make("RuneReachPawn","Pawn"));
    auto* target=UObject::Cast<UPawn>(make("RuneReachTarget","Pawn"));
    auto* point=UObject::Cast<UActor>(make("RuneReachPoint","Actor"));
    ULevel level("RuneReachLevel",nullptr,ObjectFlags::Transient);
    UModel model("RuneReachModel",nullptr,ObjectFlags::Transient);
    level.Model=&model; level.SetOwner(info); model.RootOutside=1;
    model.Nodes.resize(1); model.Surfaces.resize(1); model.Zones.resize(1);
    model.Zones[0].ZoneActor=zone;
    auto& wall=model.Nodes[0]; wall={}; wall.PlaneX=1; wall.PlaneW=20;
    wall.Front=-1; wall.Back=-1; wall.Plane=-1; wall.CollisionBound=-1;
    model.Points={vec3(20,-1000,-1000),vec3(20,1000,-1000),vec3(20,0,1000)};
    model.Vertices.resize(3); for(int i=0;i<3;i++) model.Vertices[i].Vertex=i;
    struct Clear {
        UActor* Pawn; UActor* Target; UActor* Point;
        ~Clear() { for(auto* p:{Pawn,Target,Point}) p->XLevel()=nullptr; }
    } clear{pawn,target,point};
    for(UActor* p:{static_cast<UActor*>(pawn),static_cast<UActor*>(target),point})
    {
        p->Level()=info; p->XLevel()=&level; p->Region().Zone=zone;
        p->Location()=vec3(0,0,100); p->Physics()=PHYS_None;
    }
    pawn->FootRegion().Zone=zone; target->FootRegion().Zone=targetZone;
    pawn->BaseEyeHeight()=0; pawn->CollisionRadius()=20; target->CollisionRadius()=10;
    pawn->MeleeRange()=100; // Capped at 1.5*20: threshold is exactly60.
    auto* function=FindEventFunction(pawn,"actorReachable");
    auto reachable=[&](UActor* goal) {
        Array<ExpressionValue> args={ExpressionValue::ObjectValue(goal)};
        return Frame::Call(function,pawn,{args.data(),args.size()}).ToBool();
    };
    target->Location().x=60;
    for(auto physics:{PHYS_None,PHYS_Falling,PHYS_Interpolating})
    {
        pawn->Physics()=physics;
        require(reachable(target),"Rune melee-range pawn incorrectly rejected by physics dispatch");
    }
    pawn->Physics()=PHYS_None;
    target->Location().x=60.01f;
    require(!reachable(target),"Rune nearby-pawn reach ignored distance cap");
    target->Location()=vec3(0,0,160.01f);
    require(!reachable(target),"Rune nearby-pawn reach ignored vertical distance");
    pawn->MeleeRange()=5; target->Location()=vec3(35,0,100);
    require(reachable(target),"Rune nearby-pawn reach lost inclusive melee-range boundary");
    target->Location().x=35.01f;
    require(!reachable(target),"Rune nearby-pawn reach ignored smaller MeleeRange");
    target->Location().x=35;
    point->Location()=target->Location();
    require(!reachable(point) && !reachable(nullptr),"Rune nearby-pawn shortcut accepted nonpawn or null");
    targetZone->bPainZone()=true; targetZone->DamageType()="fire"; pawn->ReducedDamageType()="None";
    require(!reachable(target),"Rune melee shortcut bypassed pain-zone rejection");
    targetZone->bPainZone()=false; targetZone->bWaterZone()=true;
    target->Region().Zone=targetZone; pawn->bCanSwim()=false;
    require(!reachable(target),"Rune melee shortcut bypassed swimming capability");
    targetZone->bWaterZone()=false;
    wall.NumVertices=3;
    require(!pawn->FastTrace(target->Location(),pawn->Location()),"Rune reach fixture wall does not block trace");
    require(!reachable(target),"Rune melee shortcut accepted target through a wall");
    require(pawn->Location()==vec3(0,0,100),"Rune reach query changed pawn location");
    std::cout << "RUNE_ACTOR_REACH_CLOSE_OK physics=3 radius_cap=1 melee_limit=1 distance3d=1 inclusive=1 wall=1 pain=1 water=1 nonpawn=1 null=1 location_preserved=1\n";
}
