#pragma once
#include "Light/BuildLightingVisibilityBvh.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Collision/BottomLevel/TraceRayModel.h"
#include <iostream>

inline void RunRuneLightingVisibilityProbe(Engine& game,ULevel* level)
{
    auto* info=UObject::Cast<ULevelInfo>(level->Actors[0]);
    level->SetOwner(info);
    auto scene=BuildLightingVisibilityBvh(level->Model);
    std::vector<vec3> points;
    for(auto* actor:level->Actors)if(actor)points.push_back(actor->Location());
    for(const auto& point:level->Model->Points)points.push_back(point);
    if(points.empty()||!scene->TriangleCount())throw std::runtime_error("Visibility oracle needs geometry");
    uint32_t random=0x13ed00u;
    auto next=[&]{random=random*1664525u+1013904223u;return random;};
    int hits=0,misses=0;
    for(int i=0;i<6000;++i) {
        vec3 from=points[next()%points.size()],to=points[next()%points.size()];
        if(i%3){from+=vec3(int(next()%17)-8,int(next()%17)-8,int(next()%17)-8)*.01f;}
        bool reference=level->Collision.TraceAnyHit(from,to,nullptr,false,true,true,TraceWorldPart::LevelOnly);
        bool optimized=scene->Trace(from,to);
        if(reference!=optimized) {
            std::cout<<"RUNE_LIGHTING_BVH_MISMATCH ray="<<i<<" reference="<<reference<<" optimized="<<optimized<<" from="<<from.x<<','<<from.y<<','<<from.z<<" to="<<to.x<<','<<to.y<<','<<to.z<<'\n';
            std::cout<<"RUNE_LIGHTING_BVH_LINEAR result="<<scene->TraceLinear(from,to)<<" triangles="<<scene->TriangleCount()<<'\n';
            TraceRayModel ray;auto origin=dvec3(from),direction=dvec3(to)-origin;auto distance=length(direction);direction*=1.f/distance;
            auto hits=ray.Trace(info,level->Model,origin,.01,direction,distance+1,true);
            for(const auto& hit:hits)std::cout<<"RUNE_LIGHTING_BVH_ORACLE_NODE index="<<(hit.Node-level->Model->Nodes.data())<<" vertices="<<int(hit.Node->NumVertices)<<" flags="<<int(hit.Node->NodeFlags)<<" surface="<<hit.Node->Surf<<'\n';
            throw std::runtime_error("Fast world visibility differs at oracle ray "+std::to_string(i));
        }
        if(reference)++hits;else ++misses;
    }
    std::cout<<"RUNE_LIGHTING_BVH_OK rays=6000 hits="<<hits<<" misses="<<misses<<" triangles="<<scene->TriangleCount()<<'\n';
}
