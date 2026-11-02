#pragma once
#include "Engine.h"
#include "Package/Package.h"
#include "Package/PackageManager.h"
#include "Packages/Engine/Actors/Brush/UBrush.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Collision/BottomLevel/TraceRayModel.h"
#include "Collision/TopLevel/RuneJointCollision.h"
#include <array>
#include <iostream>
#include <stdexcept>

inline void RunRuneCollisionProbe(Engine& game)
{
    ULevel level("RuneSurfaceTestLevel",nullptr,ObjectFlags::Transient);
    UModel world("RuneSurfaceTestWorld",nullptr,ObjectFlags::Transient);
    UModel brush("RuneSurfaceTestBrush",nullptr,ObjectFlags::Transient);
    level.Model=&world;world.Surfaces.resize(4);brush.Surfaces.resize(1);
    // The brush's index3 is deliberately outside its local surface table.
    // Its world surface is solid; unrelated local surface0 is nonsolid.
    world.Surfaces[3].PolyFlags=0;brush.Surfaces[0].PolyFlags=PF_NotSolid;
    brush.Points={vec3(0,-1,-1),vec3(0,1,-1),vec3(0,0,1)};
    brush.Vertices.resize(3);for(int i=0;i<3;i++) brush.Vertices[i].Vertex=i;
    brush.Nodes.resize(1);auto& node=brush.Nodes[0];node={};
    node.PlaneX=1;node.Front=-1;node.Back=-1;node.Plane=-1;node.NumVertices=3;node.VertPool=0;node.Surf=3;
    auto* actor=UObject::Cast<UBrush>(game.packages->GetTransientPackage()->NewObject("RuneSurfacePolyobj",
        game.packages->GetPackage("Engine")->GetClass("Polyobj"),ObjectFlags::Transient));
    actor->Brush()=&brush;actor->XLevel()=&level;
    struct Clear { UBrush* actor; ~Clear(){actor->Brush()=nullptr;actor->XLevel()=nullptr;} } clear{actor};
    auto query=[&]()
    {
        TraceRayModel trace;
        return std::pair<bool,bool>(!trace.Trace(actor,&brush,dvec3(-2,0,0),0,dvec3(1,0,0),4,false).empty(),
            trace.TraceAnyHit(actor,&brush,dvec3(-2,0,0),0,dvec3(1,0,0),4,true));
    };
    if(query()!=std::pair<bool,bool>(true,true)) throw std::runtime_error("Polyobj ray did not use solid world surface");
    world.Surfaces[3].PolyFlags=PF_NotSolid;
    if(query()!=std::pair<bool,bool>(false,false)) throw std::runtime_error("Polyobj ray ignored world nonsolid surface");
    std::cout<<"RUNE_POLYOBJ_WORLD_SURFACE_OK local_index_out_of_range=1 solid_ray=1 nonsolid_ray=1 lighting_anyhit=1\n";
}

inline void RunRuneVillageCollisionProbe(Engine& game, ULevel* level)
{
    UActor* rack=nullptr;
    for(auto* actor:level->Actors) if(actor && actor->Name=="Skinrack0") rack=actor;
    if(!rack) throw std::runtime_error("Village collision fixture requires RagnarVillage2");
    auto* player=game.packages->GetPackage("RuneI")->GetClass("RunePlayer")->GetDefaultObject<UActor>();
    vec3 extent(player->CollisionRadius(),player->CollisionRadius(),player->CollisionHeight());
    vec3 start(2277.86f,63.69f,-85),end=start+vec3(100,0,0),normal;
    float fraction;
    if(!RuneJointCollision::Trace(rack,start,end,extent,fraction,normal) || fraction<1)
        throw std::runtime_error("Village skinrack still blocks open passage");
    bool hitFrame=false;
    for(float y=-120;y<=40;y+=8)
        if(RuneJointCollision::Trace(rack,vec3(2200,y,-70),vec3(2480,y,-70),extent,fraction,normal) && fraction<1) hitFrame=true;
    if(!hitFrame) throw std::runtime_error("Village rack frame lost collision");
    auto* flags=static_cast<uint8_t*>(rack->GetProperty("JointFlags"));
    std::array<uint8_t,50> saved;std::copy(flags,flags+50,saved.begin());
    std::fill(flags,flags+50,0);
    bool handled=RuneJointCollision::Trace(rack,start,end,extent,fraction,normal);
    std::copy(saved.begin(),saved.end(),flags);
    if(handled) throw std::runtime_error("Empty joint masks must retain cylinder fallback");
    rack->SetBool("bJointsBlock",false);
    handled=RuneJointCollision::Trace(rack,start,end,extent,fraction,normal);
    rack->SetBool("bJointsBlock",true);
    if(handled) throw std::runtime_error("Disabled joint blocking must retain cylinder fallback");
    vec4 cube[6]={vec4(1,0,0,1),vec4(-1,0,0,1),vec4(0,1,0,1),vec4(0,-1,0,1),vec4(0,0,1,1),vec4(0,0,-1,1)};
    if(!RuneJointCollision::Clip(cube,vec3(-10,0,4),vec3(10,0,4),vec3(2,2,4),fraction,normal) || normal.x!=-1)
        throw std::runtime_error("Tall movement extent missed joint");
    if(RuneJointCollision::Clip(cube,vec3(-10,4,0),vec3(10,4,0),vec3(2,2,4),fraction,normal))
        throw std::runtime_error("Movement height incorrectly expanded horizontal extent");
    if(RuneJointCollision::Clip(cube,vec3(-3,0,0),vec3(-3,1,0),vec3(2,2,4),fraction,normal))
        throw std::runtime_error("Resting joint contact prevented sliding");
    std::cout<<"RUNE_VILLAGE_JOINT_BLOCK_OK corridor_clear=1 rack_frame_solid=1 empty_mask_fallback=1 disabled_fallback=1 tall_extent=1 tangent_slide=1\n";
}
