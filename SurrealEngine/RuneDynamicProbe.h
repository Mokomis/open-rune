#pragma once

#include "Engine.h"
#include "Package/PackageManager.h"
#include "Package/Package.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "VM/ScriptCall.h"
#include "VM/Frame.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

// Headless, transient fixtures. Run after Engine construction; never spawns into
// a user's map or writes assets/saves. Parameter callbacks run real Rune script.
inline void RunRuneDynamicProbe(Engine& game)
{
    auto* transient=game.packages->GetTransientPackage();
    auto* rune=game.packages->GetPackage("RuneI");
    auto* level=UObject::Cast<ULevelInfo>(transient->NewObject("RuneDynamicsLevel",
        game.packages->GetPackage("Engine")->GetClass("LevelInfo"),ObjectFlags::Transient));
    level->bBegunPlay()=true;
    auto invoke=[](UActor* actor,NameString name,Array<ExpressionValue> args=Array<ExpressionValue>())
    {
        auto* function=FindEventFunction(actor,name);
        if(!function) throw std::runtime_error("Dynamic probe function missing: "+name.ToString());
        return Frame::Call(function,actor,{args.data(),args.size()});
    };
    auto make=[&](const char* name,const char* className)
    {
        auto* actor=UObject::Cast<UActor>(transient->NewObject(name,rune->GetClass(className),ObjectFlags::Transient));
        actor->Level()=level;
        actor->Location()=vec3(0.0f); actor->Rotation()={0,0,0}; actor->DrawScale()=1.0f;
        auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(actor));
        if(!model || model->Sequences.empty()) throw std::runtime_error("Dynamic fixture has no skeleton");
        actor->AnimSequence()=model->Sequences[0].Info.Name; actor->AnimFrame()=0.0f;
        invoke(actor,"SetDefaultJointFlags");
        model->ActorJointMatrices(actor);
        return actor;
    };
    auto finite=[](const vec3& v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); };
    auto force=[&](UActor* actor,int joint,const vec3& impulse)
    {
        invoke(actor,"ApplyJointForce",{ExpressionValue::IntValue(joint),ExpressionValue::VectorValue(impulse)});
    };
    auto position=[&](UActor* actor,int joint) { return invoke(actor,"GetJointPos",{ExpressionValue::IntValue(joint)}).ToVector(); };
    auto validateState=[&](UActor* actor)
    {
        for(const auto& state:actor->RuneDynamics)
            if(!finite(state.Position)||!finite(state.Target)||!finite(state.Velocity)||!std::isfinite(state.Length))
                throw std::runtime_error("Dynamic integration produced nonfinite state");
    };

    auto* drum=make("RuneDynamicsDrum","Drum1");
    auto* drumModel=UObject::Cast<USkelModel>(GetRuneDisplayMesh(drum));
    int drumJoint=invoke(drum,"JointNamed",{ExpressionValue::NameValue("drum")}).ToInt();
    if(drumJoint<0 || drumJoint>=50 || !(static_cast<uint8_t*>(drum->GetProperty("JointFlags"))[drumJoint]&4))
        throw std::runtime_error("Drum1 fixture does not have the expected spring joint");
    vec3 rest=position(drum,drumJoint);
    for(int i=0;i<120;i++) drumModel->TickDynamics(drum,1.0f/120.0f);
    if(length(position(drum,drumJoint)-rest)>0.001f) throw std::runtime_error("Unforced spring moved from rest");
    force(drum,drumJoint,vec3(0,0,-50));
    if(length(drum->RuneDynamics[drumJoint].Velocity)<0.01f) throw std::runtime_error("Native joint impulse was discarded");
    float maximum=0;
    for(int i=0;i<1200;i++)
    {
        drumModel->TickDynamics(drum,1.0f/120.0f); validateState(drum);
        maximum=std::max(maximum,length(position(drum,drumJoint)-rest));
        if(maximum>5.01f) throw std::runtime_error("Drum spring exceeded its script displacement bound");
    }
    float finalDistance=length(position(drum,drumJoint)-rest);
    if(maximum<0.01f || finalDistance>0.101f || length(drum->RuneDynamics[drumJoint].Velocity)>0.32f)
        throw std::runtime_error("Drum spring did not visibly move and settle after impulse");
    std::cout << "RUNE_DYNAMIC_SPRING_OK class=Drum1 joint=" << drumJoint << " max_displacement=" << maximum
        << " final_displacement=" << finalDistance << " bound=5 steps=1200 hz=120\n";

    auto* tree=make("RuneDynamicsTree","Tree1");
    auto* treeModel=UObject::Cast<USkelModel>(GetRuneDisplayMesh(tree));
    auto* flags=static_cast<uint8_t*>(tree->GetProperty("JointFlags"));
    int accelerator=-1,unflagged=-1;
    for(size_t j=0;j<treeModel->Joints.size() && j<50;j++)
    {
        if((flags[j]&2) && !(flags[j]&0x80) && length(tree->RuneDynamics[j].Position)>0.01f && accelerator<0) accelerator=(int)j;
        if(!(flags[j]&0x86) && unflagged<0) unflagged=(int)j;
    }
    if(accelerator<0 || unflagged<0) throw std::runtime_error("Tree1 dynamic fixture lacks accelerator/control joints");
    auto before=tree->RuneDynamics;
    force(tree,-1,vec3(10,20,30)); force(tree,50,vec3(10,20,30));
    force(tree,unflagged,vec3(10,20,30));
    force(tree,accelerator,vec3(std::numeric_limits<float>::quiet_NaN(),0,0));
    for(size_t j=0;j<before.size();j++)
        if(before[j].Velocity!=tree->RuneDynamics[j].Velocity || before[j].Position!=tree->RuneDynamics[j].Position)
            throw std::runtime_error("Invalid or unflagged force changed dynamic state");
    std::cout << "RUNE_DYNAMIC_FORCE_GUARDS_OK invalid=2 unflagged=1 nonfinite=1\n";

    vec3 initialDirection=tree->RuneDynamics[accelerator].Position;
    vec3 perpendicular=cross(initialDirection,vec3(1,0,0));
    if(length(perpendicular)<0.001f) perpendicular=cross(initialDirection,vec3(0,1,0));
    mat3 world(GetRuneMeshToWorld(tree)*treeModel->ActorJointMatrices(tree)[accelerator]);
    force(tree,accelerator,world*(normalize(perpendicular)*100.0f));
    auto meshBefore=treeModel->SkinFrame(tree->GetByte("SkelMesh"),0,0,tree);
    float directionChange=0,meshChange=0;
    for(int i=0;i<600;i++)
    {
        treeModel->TickDynamics(tree,1.0f/120.0f); validateState(tree);
        const auto& state=tree->RuneDynamics[accelerator];
        if(std::abs(length(state.Position)-state.Length)>0.001f) throw std::runtime_error("Accelerative endpoint length changed");
        directionChange=std::max(directionChange,length(state.Position-initialDirection));
        if(i%20==0)
        {
            auto mesh=treeModel->SkinFrame(tree->GetByte("SkelMesh"),0,0,tree);
            for(size_t v=0;v<mesh.size();v++)
            {
                if(!finite(mesh[v])) throw std::runtime_error("Dynamic skinning produced nonfinite vertices");
                meshChange=std::max(meshChange,length(mesh[v]-meshBefore[v]));
            }
        }
    }
    if(directionChange<0.001f || meshChange<0.001f) throw std::runtime_error("Accelerative impulse did not bend the rendered skeleton");
    std::cout << "RUNE_DYNAMIC_ACCELERATOR_OK class=Tree1 joint=" << accelerator << " direction_change=" << directionChange
        << " mesh_change=" << meshChange << " length_preserved=1 finite=1 steps=600 hz=120\n";

    invoke(tree,"SetDefaultJointFlags");
    if(!tree->RuneDynamics.empty()) throw std::runtime_error("Default joint reset retained stale dynamic state");
    treeModel->ActorJointMatrices(tree);
    if(length(tree->RuneDynamics[accelerator].Velocity)>0.001f) throw std::runtime_error("Dynamic reset retained impulse velocity");
    if(length(tree->RuneDynamics[accelerator].Position-initialDirection)>0.001f) throw std::runtime_error("Dynamic reset failed to restore initial pose");
    std::cout << "RUNE_DYNAMIC_RESET_OK velocity_cleared=1 pose_restored=1\n";
}
