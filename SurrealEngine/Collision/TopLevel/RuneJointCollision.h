#pragma once
#include <limits>
#include "Engine.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"

// Rune USkelModel::LineCheck uses collision joints when bJointsBlock is set,
// falling back to the actor cylinder only when there are no collision joints.
// See retained Engine.SkelLineCheck evidence. The hash still uses actor bounds.
namespace RuneJointCollision
{
inline bool Clip(const vec4* planes, const vec3& start, const vec3& end,
    const vec3& extent, float& fraction, vec3& normal)
{
    float enter=0, leave=1, closest=-std::numeric_limits<float>::infinity();
    vec3 closestNormal(0), entryNormal(0);
    bool inside=true;
    for(int i=0;i<6;i++) {
        auto n=planes[i].xyz();
        float support=std::abs(n.x)*extent.x+std::abs(n.y)*extent.y+std::abs(n.z)*extent.z;
        float a=dot(n,start)-planes[i].w-support;
        float b=dot(n,end)-planes[i].w-support;
        if(a>closest){closest=a;closestNormal=n;}
        if(a>0) inside=false;
        if(a>0 && b>0) return false;
        if(a<=0 && b<=0) continue;
        float t=a/(a-b);
        if(a>b){if(t>=enter){enter=t;entryNormal=n;}}
        else leave=std::min(leave,t);
        if(enter>leave) return false;
    }
    if(inside) {
        // Resting contact must allow sliding along, or moving out of, a face.
        if(closest>=-0.001f && dot(closestNormal,end-start)>=0) return false;
        fraction=0;normal=closestNormal;return true;
    }
    if(enter>=1) return false;
    fraction=enter;normal=entryNormal;return true;
}

// Returns whether joint collision handled this actor, independently of a hit.
inline bool Trace(UActor* actor, const vec3& start, const vec3& end, const vec3& extent,
    float& fraction, vec3& normal)
{
    if(!engine->LaunchInfo.IsRune() || !actor->GetBool("bJointsBlock") || actor->DrawScale()==0) return false;
    auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(actor));
    if(!model) return false;
    auto* flags=static_cast<uint8_t*>(actor->GetProperty("JointFlags"));
    bool handled=false;
    for(size_t j=0;j<model->Joints.size() && j<50;j++) handled|=bool(flags[j]&0x10);
    if(!handled) return false;
    auto bones=model->ActorJointMatrices(actor);
    auto world=GetRuneMeshToWorld(actor);
    fraction=1;normal=vec3(0);
    for(size_t j=0;j<model->Joints.size() && j<50;j++) if(flags[j]&0x10) {
        mat4 transform=world*bones[j];
        mat3 normals=mat3::transpose(mat3::inverse(mat3(transform)));
        vec3 origin=(transform*vec4(0,0,0,1)).xyz();
        vec4 planes[6];
        for(int p=0;p<6;p++) {
            auto plane=model->Joints[j].CollisionPlanes[p];
            auto n=normals*plane.xyz();
            float size=length(n);
            if(size<=0 || !std::isfinite(size)) Exception::Throw("Invalid Rune blocking joint plane");
            planes[p]=vec4(n/size,(plane.w+dot(n,origin))/size);
        }
        float candidate;vec3 candidateNormal;
        if(Clip(planes,start,end,extent,candidate,candidateNormal) && candidate<fraction) {
            fraction=candidate;normal=candidateNormal;
        }
    }
    return true;
}
}
