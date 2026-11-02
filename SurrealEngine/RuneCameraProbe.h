#pragma once
#include "Render/RuneAttachmentVisibility.h"
#include "Render/SpriteTexture.h"
#include "Engine.h"
#include "Package/Package.h"
#include "Package/PackageManager.h"
#include "Packages/Engine/Actors/Keypoint/UInterpolationPoint.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Subsystems/USurrealAudioDevice.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "VM/ScriptCall.h"
#include <iostream>
#include <stdexcept>

// Transient actors and an empty world: curve endpoints, real CineCamera pause
// callbacks, instant/skip/reverse traversal, and falling's no-collision guard.
inline void RunRuneCameraProbe(Engine& game)
{
    auto require=[](bool value,const char* message){if(!value) throw std::runtime_error(message);};
    auto* transient=game.packages->GetTransientPackage();
    auto* rune=game.packages->GetPackage("RuneI");
    auto* ep=game.packages->GetPackage("Engine");
    auto* info=UObject::Cast<ULevelInfo>(transient->NewObject("RunePathInfo",ep->GetClass("LevelInfo"),ObjectFlags::Transient));
    ULevel level("RunePathLevel",nullptr,ObjectFlags::Transient);
    UModel model("RunePathModel",nullptr,ObjectFlags::Transient);
    info->bBegunPlay()=true;
    level.Model=&model; level.SetOwner(info); model.Zones.resize(1); model.Zones[0].ZoneActor=info;
    struct Restore { Engine& game; ULevel* level; ULevelInfo* info; ~Restore(){game.Level=level;game.LevelInfo=info;} } restore{game,game.Level,game.LevelInfo};
    game.Level=&level; game.LevelInfo=info;
    Array<UActor*> fixtures;
    struct Clear { Array<UActor*>& actors; ~Clear(){for(auto* a:actors){a->StateFrame.reset();a->XLevel()=nullptr;a->Owner()=nullptr;a->Index=-1;}} } clear{fixtures};
    auto make=[&](const char* name,UClass* cls)
    {
        auto* a=UObject::Cast<UActor>(transient->NewObject(name,cls,ObjectFlags::Transient));
        a->XLevel()=&level; a->Level()=info; a->Region().Zone=info;
        a->bStatic()=false;a->bMovable()=true;a->bCollideActors()=false;a->bCollideWorld()=false;
        a->bBlockActors()=false;a->bBlockPlayers()=false;a->Event()="None";
        fixtures.push_back(a);return a;
    };
    auto* lookMarker=make("RuneLookMarker",rune->GetClass("LookTarget"));
    std::cout << "RUNE_LOOK_MARKER_DEFAULT draw=" << (int)lookMarker->DrawType()
        << " texture=" << (lookMarker->Texture() ? lookMarker->Texture()->Name.ToString() : "None")
        << " sprite=" << (lookMarker->Sprite() ? lookMarker->Sprite()->Name.ToString() : "None") << "\n";
    require(lookMarker->Texture() && lookMarker->Texture()->Name == "EyeCon" && !lookMarker->Sprite(),
        "Original LookTarget must separate editor EyeCon from null runtime Sprite");
    lookMarker->SetBool("bInitiallyActive",true);
    CallEvent(lookMarker,"PreBeginPlay");
    require(!lookMarker->bHidden() && !GetActorSpriteTexture(lookMarker,true),
        "Active look focus must remain script-visible without drawing editor eyes");
    require(GetActorSpriteTexture(lookMarker,false)==lookMarker->Texture(),
        "Non-Rune sprite texture selection changed");
    CallEvent(lookMarker,"Trigger",{ExpressionValue::ObjectValue(nullptr),ExpressionValue::ObjectValue(nullptr)});
    require(lookMarker->bHidden(),"Original look-target trigger no longer disables focus");
    auto* spriteActor=make("RuneSpriteFixture",ep->GetClass("Actor"));
    spriteActor->Sprite()=lookMarker->Texture();spriteActor->Texture()=nullptr;
    require(GetActorSpriteTexture(spriteActor,true)==lookMarker->Texture(),
        "Rune runtime sprite must render even without editor Texture");
    std::cout << "RUNE_SPRITE_TEXTURE_OK editor_icon_excluded=1 active_focus_preserved=1 trigger_preserved=1 runtime_sprite=1 other_games=1\n";
    auto* camera=make("RunePathCamera",rune->GetClass("CineCamera"));
    UInterpolationPoint* points[4];
    const char* names[]={"RunePathA","RunePathB","RunePathC","RunePathD"};
    vec3 positions[]={vec3(0,0,0),vec3(100,0,0),vec3(200,100,0),vec3(400,0,0)};
    for(int i=0;i<4;i++)
    {
        auto* p=UObject::Cast<UInterpolationPoint>(make(names[i],rune->GetClass("CineInterpolationPoint")));
        points[i]=p;p->Location()=positions[i];p->Rotation()={0,0,0};
        p->RateModifier()=1;p->GameSpeedModifier()=1;p->FovModifier()=1;
        p->SetBool("bSplineThruPoints",true);p->SetBool("bInterpInstant",false);
        p->bSkipNextPath()=false;p->bEndOfPath()=false;p->SetFloat("PauseTime",0);
    }
    for(int i=0;i<4;i++){points[i]->Prev()=i?points[i-1]:nullptr;points[i]->Next()=i<3?points[i+1]:nullptr;}
    camera->GotoState("Interpolating","Begin");
    auto reset=[&](float alpha=0.0f)
    {
        camera->Target()=points[1];camera->PhysAlpha()=alpha;camera->PhysRate()=1;
        camera->bInterpolating()=true;camera->Physics()=PHYS_Interpolating;camera->Location()=positions[1];
    };
    reset();camera->TickInterpolating(0.5f);
    require(length(camera->Location()-vec3(143.75f,56.25f,0))<0.01f,"Rune through-point curve midpoint failed");
    reset();camera->TickInterpolating(1.0f);
    require(length(camera->Location()-positions[2])<0.01f,"Rune through-point curve misses endpoint");
    points[1]->SetBool("bSplineThruPoints",false);reset();camera->TickInterpolating(0.5f);
    require(length(camera->Location()-vec3(152.083333f,47.916667f,0))<0.01f,"Rune B-spline curve midpoint failed");
    points[1]->SetBool("bSplineThruPoints",true);
    points[0]->Rotation().Yaw=65000;points[1]->Rotation().Yaw=65500;points[2]->Rotation().Yaw=36;points[3]->Rotation().Yaw=500;
    reset();camera->TickInterpolating(0.5f);
    require(std::abs(normalize(camera->Rotation()).Yaw)<200,"Rune camera took long rotation across angle wrap");
    points[0]->Rotation().Yaw=0;points[1]->Rotation().Yaw=100;points[2]->Rotation().Yaw=200;points[3]->Rotation().Yaw=400;
    reset();camera->TickInterpolating(0.01f);
    require(camera->Rotation().Yaw==101,"Rune camera changed original intermediate integer rotation rounding");
    std::cout<<"RUNE_CAMERA_CURVES_OK catmull_midpoint=1 through_endpoint=1 bspline_midpoint=1 wrapped_rotation=1 integer_rotation=101\n";

    auto* owner=UObject::Cast<UPlayerPawn>(make("RuneCameraOwner",ep->GetClass("PlayerPawn")));
    camera->Owner()=owner;
    float defaultFov=owner->Class->GetDefaultObject<UPlayerPawn>()->FovAngle();
    require(defaultFov>0,"Camera owner fixture has no default FOV");
    owner->FovAngle()=defaultFov*0.5f;
    points[1]->FovModifier()=0.8f;points[2]->FovModifier()=1.2f;
    points[1]->GameSpeedModifier()=0.6f;points[2]->GameSpeedModifier()=1.4f;
    points[1]->ScreenFlashScale()=0.25f;points[2]->ScreenFlashScale()=0.75f;
    points[1]->ScreenFlashFog()=vec3(0.1f,0.2f,0.3f);points[2]->ScreenFlashFog()=vec3(0.5f,0.6f,0.7f);
    points[2]->RateModifier()=2;
    reset(0.25f);camera->TickInterpolating(0.1f);
    require(std::abs(camera->PhysAlpha()-0.375f)<0.0001f && std::abs(info->TimeDilation()-0.8f)<0.0001f,
        "Rune camera rate/time dilation must interpolate from pre-step alpha");
    require(std::abs(owner->FovAngle()-defaultFov*0.9f)<0.001f,
        "Rune camera owner FOV must use class default and pre-step alpha");
    require(std::abs(owner->DesiredFlashScale()-0.375f)<0.0001f && length(owner->FlashScale()-vec3(0.375f))<0.0001f &&
        length(owner->DesiredFlashFog()-vec3(0.2f,0.3f,0.4f))<0.0001f && length(owner->FlashFog()-vec3(0.2f,0.3f,0.4f))<0.0001f,
        "Rune camera did not update both immediate and desired owner flash values");
    camera->Owner()=nullptr;info->TimeDilation()=1;
    for(auto* p:{points[1],points[2]}) { p->FovModifier()=1;p->GameSpeedModifier()=1;p->RateModifier()=1; }
    std::cout<<"RUNE_CAMERA_OWNER_OK old_alpha=1 rate=1 time_dilation=0.8 default_fov_factor=0.9 flash_scale=0.375 flash_fog=1\n";

    points[2]->SetFloat("PauseTime",0.4f);camera->TimerRate()=0;reset();camera->TickInterpolating(1.0f);
    require(camera->Target()==points[1] && camera->PhysAlpha()==1.0f && camera->bInterpolating() && camera->TimerRate()==0,
        "Rune camera fired reached-node callback at equality instead of crossing");
    camera->TickInterpolating(0.001f);
    require(camera->Target()==points[2] && !camera->bInterpolating() && std::abs(camera->TimerRate()-0.4f)<0.001f,
        "Rune camera failed to fire callback immediately after crossing equality");
    points[2]->SetFloat("PauseTime",0.4f);reset();camera->TickInterpolating(1.1f);
    require(camera->Target()==points[2] && !camera->bInterpolating() && std::abs(camera->TimerRate()-0.4f)<0.001f,
        "CineCamera pause callback did not belong to the reached node");
    CallEvent(camera,"Timer");require(camera->bInterpolating(),"Original camera timer failed to resume path");
    points[2]->SetBool("bInterpInstant",true);reset();camera->TickInterpolating(0.001f);
    require(camera->Target()==points[2] && length(camera->Location()-positions[2])<0.01f && !camera->bInterpolating(),
        "Instant camera cut consumed segment duration or missed reached-node pause");
    points[2]->SetBool("bInterpInstant",false);points[2]->SetFloat("PauseTime",0);
    points[2]->bSkipNextPath()=true;reset();camera->TickInterpolating(1.1f);
    require(camera->Target()==points[3],"Camera failed to skip a marked path node");
    points[2]->bSkipNextPath()=false;
    // Generic actor avoids CineCamera's forward-only callback assumptions.
    auto* reverse=make("RuneReversePath",ep->GetClass("Actor"));
    reverse->Target()=points[2];reverse->PhysAlpha()=0.1f;reverse->PhysRate()=-1;reverse->bInterpolating()=true;
    reverse->TickInterpolating(0.2f);
    require(reverse->Target()==points[1] && std::abs(reverse->PhysAlpha()-0.9f)<0.001f,"Reverse path lost remaining frame time");
    std::cout<<"RUNE_CAMERA_EVENTS_OK reached_node_pause=1 timer_resume=1 instant=1 skip=1 reverse=1 equality_threshold=1\n";

    // Exercise original CineCamera.InterpolateEnd -> ResetPath -> Destroy on
    // private registered actors. No RunePlayer is in this level, so returning
    // an actual player's ViewTarget/control is intentionally outside this test.
    USurrealAudioDevice silentAudio("RuneCameraCleanupAudio",nullptr,ObjectFlags::Transient);
    struct RestoreAudio { Engine& game; USurrealAudioDevice* audio; ~RestoreAudio(){game.audiodev=audio;} } restoreAudio{game,game.audiodev};
    if(!game.audiodev) game.audiodev=&silentAudio;
    auto registered=[&](const char* name,UClass* cls) {
        auto* a=make(name,cls);a->bNoDelete()=false;a->Index=(int)level.Actors.size();level.Actors.push_back(a);return a;
    };
    auto* ending=registered("RuneEndingCamera",rune->GetClass("CineCamera"));
    auto* start=registered("RuneEndingStart",rune->GetClass("CineInterpolationPointTemp"));
    auto* start2=registered("RuneEndingStart2",rune->GetClass("CineInterpolationPointTemp"));
    auto* end=UObject::Cast<UInterpolationPoint>(registered("RuneEndingEnd",rune->GetClass("CineInterpolationPointTemp")));
    ending->SetObject("FirstNode",points[0]);ending->SetObject("StartPt",start);ending->SetObject("StartPt2",start2);ending->SetObject("EndPt",end);
    end->Prev()=points[3];
    ending->GotoState("Interpolating","Begin");ending->Target()=points[1];ending->PhysAlpha()=0;
    ending->PhysRate()=1;ending->bInterpolating()=true;ending->Location()=positions[1];
    points[2]->bEndOfPath()=true;
    ending->TickInterpolating(1.1f);
    require(ending->bDeleteMe() && start->bDeleteMe() && start2->bDeleteMe() && end->bDeleteMe(),
        "Original endpoint callback failed to destroy camera and temporary path points");
    require(points[0]->Prev()==points[3] && points[3]->Next()==points[0],"Original camera cleanup failed to restore path links");
    for(auto* a:level.Actors) require(a==nullptr,"Destroyed cinematic fixture retained an actor slot");
    points[2]->bEndOfPath()=false;points[0]->Prev()=nullptr;points[3]->Next()=nullptr;
    std::cout<<"RUNE_CAMERA_END_OK original_callback=1 camera_destroyed=1 temporary_points_destroyed=3 links_restored=1 slots_cleared=1\n";

    auto* gear=make("RuneVisibilityGear",ep->GetClass("Actor"));
    auto* effect=make("RuneVisibilityEffect",ep->GetClass("Actor"));
    gear->bCarriedItem()=true;gear->SetObject("AttachParent",owner);
    effect->bCarriedItem()=true;effect->SetObject("AttachParent",gear);
    gear->bHidden()=true;
    auto visibility=GetRuneAttachmentVisibility(effect,owner);
    require(visibility.HiddenParent && visibility.AttachedToPlayer,"Nested hidden attachment visibility");
    gear->bHidden()=false;
    visibility=GetRuneAttachmentVisibility(effect,owner);
    require(!visibility.HiddenParent && visibility.AttachedToPlayer,"Visible attachment restoration");
    effect->bCarriedItem()=false;effect->Owner()=gear;gear->bHidden()=true;
    visibility=GetRuneAttachmentVisibility(effect,owner);
    require(!visibility.HiddenParent && !visibility.AttachedToPlayer,"Detached owned effect must remain visible");
    std::cout<<"RUNE_ATTACHMENT_VISIBILITY_OK nested_hidden=1 restore=1 detached_owner_ignored=1\n";

    auto* immovable=UObject::Cast<UPawn>(make("RuneImmovableFall",ep->GetClass("Pawn")));
    immovable->Region().ZoneNumber=1;immovable->FootRegion().Zone=info;immovable->Physics()=PHYS_Falling;immovable->bMovable()=false;
    immovable->Velocity()=vec3(0,0,-100);vec3 before=immovable->Location();
    immovable->TickFalling(0.01f);
    require(immovable->Location()==before,"Immovable falling actor moved");
    std::cout<<"RUNE_FALLING_NO_HIT_OK immovable=1 null_hit=1\n";

    // Trying to catch a ledge in open air must not teleport the pawn, consume
    // its fall, change state, or leave the temporary warping flag set.
    model.Nodes.resize(1);
    auto& emptyNode=model.Nodes[0];emptyNode={};emptyNode.PlaneX=1;
    emptyNode.Front=-1;emptyNode.Back=-1;emptyNode.Plane=-1;emptyNode.CollisionBound=-1;
    auto* falling=UObject::Cast<UPawn>(make("RuneOpenAirLedge",ep->GetClass("Pawn")));
    falling->FootRegion().Zone=info;falling->Physics()=PHYS_Falling;
    falling->Location()=vec3(100,100,100);falling->Velocity()=vec3(20,0,-100);
    for(bool enabled:{false,true}) for(bool warping:{false,true}) for(float scale:{2.0f,2.5f})
    {
        falling->SetBool("bCanGrabEdges",enabled);falling->bWarping()=warping;
        require(!falling->TryGrabRuneEdge(scale),"Open-air pawn grabbed a nonexistent ledge");
        require(falling->Location()==vec3(100,100,100) && falling->Velocity()==vec3(20,0,-100) &&
            falling->Physics()==PHYS_Falling && falling->GetStateName()=="None" && bool(falling->bWarping())==warping,
            "Rejected ledge attempt changed pawn motion, state, or warping flag");
    }
    std::cout<<"RUNE_LEDGE_OPEN_AIR_OK flags=4 heights=2 location_velocity_physics_state_preserved=1\n";
}
