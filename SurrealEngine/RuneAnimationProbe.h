#pragma once
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Package/Package.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "VM/Frame.h"
#include <iostream>
#include <stdexcept>

inline void RunRuneAnimationProbe(Engine& game)
{
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(std::string("Animation probe: ")+why);};
    auto matrixError=[](const Array<mat4>& a,const Array<mat4>& b){float e=0;for(size_t j=0;j<a.size();j++)for(int k=0;k<16;k++)e=std::max(e,std::abs(a[j][k]-b[j][k]));return e;};
    auto* trans=game.packages->GetTransientPackage();auto* runei=game.packages->GetPackage("RuneI");
    auto* level=UObject::Cast<ULevelInfo>(trans->NewObject("InterpolationLevel",game.packages->GetPackage("Engine")->GetClass("LevelInfo"),ObjectFlags::Transient));level->bBegunPlay()=true;
    auto make=[&](const std::string& name,const char* cls){auto* a=UObject::Cast<UActor>(trans->NewObject(name,runei->GetClass(cls),ObjectFlags::Transient));a->Level()=level;a->DisableEvent("AnimEnd");a->DisableEvent("AnimProxyEnd");return a;};
    bool previousMode=RuneAnimation::Interpolate;
    struct Restore {bool value;~Restore(){RuneAnimation::Interpolate=value;}} restore{previousMode};
    RuneAnimation::Interpolate=true;
    for(const char* cls:{"Ragnar","GoblinWarrior","SnowBeast"}) {
        auto* a=make(std::string("Interpolation")+cls,cls);auto* m=UObject::Cast<USkelModel>(GetRuneDisplayMesh(a));
        auto* flags=static_cast<uint8_t*>(a->GetProperty("JointFlags"));for(int i=0;i<50;i++)flags[i]=0;
        size_t seq=0;for(size_t i=0;i<m->Sequences.size();i++)if(m->Sequences[i].Info.NumFrames>6){seq=i;break;}
        auto info=m->Sequences[seq].Info;require(info.NumFrames>1,"No moving sequence");
        // Every authored endpoint is preserved; loop wrap interpolates last -> first.
        for(int k=0;k<info.NumFrames;k++)require(matrixError(m->JointMatrices(m->SampleFrame(seq,(float)k/info.NumFrames,false)),m->JointMatrices(m->DecodeFrame(seq,k)))<.005f,"Source pose endpoint changed");
        auto last=m->JointMatrices(m->DecodeFrame(seq,info.NumFrames-1));
        require(matrixError(m->JointMatrices(m->SampleFrame(seq,1.1f,false)),last)<.0001f,"Nonloop wrapped to first pose");
        auto wrap=m->SampleFrame(seq,1-.5f/info.NumFrames,true),pa=m->DecodeFrame(seq,info.NumFrames-1),pb=m->DecodeFrame(seq,0);
        for(size_t j=0;j<pa.size();j++)pa[j]=BlendRunePose(pa[j],pb[j],.5f);
        require(matrixError(m->JointMatrices(wrap),m->JointMatrices(pa))<.005f,"Loop seam blend mismatch");
        a->PlayAnim(info.Name,1,0);a->AnimFrame()=.37f;
        auto before=m->JointMatrices(m->SampleAnimation(a));
        size_t other=(seq+1)%m->Sequences.size();a->PlayAnim(m->Sequences[other].Info.Name,1,.2f);
        require(matrixError(before,m->JointMatrices(m->SampleAnimation(a)))<.005f,"Transition starts with a snap");
        a->AnimFrame()*=.5f;auto middle=m->JointMatrices(m->SampleAnimation(a));
        a->PlayAnim(info.Name,1,.15f);
        require(matrixError(middle,m->JointMatrices(m->SampleAnimation(a)))<.005f,"Interrupted transition loses current pose");
        // Missing transient source after load is safe, with no dangling actor/model reference.
        a->RuneTweenPose.clear();a->RuneTweenModel=nullptr;
        require(matrixError(m->JointMatrices(m->SampleAnimation(a)),m->JointMatrices(m->DecodeFrame(seq,0)))<.0001f,"Missing tween source fallback");
        a->AnimFrame()=.271f;a->RuneJointOffsets[0]={70,-90,120};
        auto bones=m->ActorJointMatrices(a);m->PrepareMesh(a->GetByte("SkelMesh"),a->AnimSequence(),a->AnimFrame(),a);
        const auto& vertices=m->Meshes[a->GetByte("SkelMesh")].Vertices;
        for(size_t i=0;i<vertices.size();i++){const auto& v=vertices[i];vec3 expected=(bones[v.Joint[0]]*vec4(v.Point[0],1)).xyz()*v.Weight;if(v.Weight!=1)expected+=(bones[v.Joint[1]]*vec4(v.Point[1],1)).xyz()*(1-v.Weight);require(length(expected-m->Verts[i])<.001f,"Rendered skin differs from attachment skeleton");}
        std::cout<<"RUNE_INTERPOLATION_MODEL_OK class="<<cls<<" sequence="<<info.Name.ToString()<<" endpoints="<<info.NumFrames<<" wrap=1 tween=1 interruption=1 skin_joints=1 load_fallback=1\n";
    }
    auto* owner=make("InterpolationOwner","Ragnar");auto* m=UObject::Cast<USkelModel>(GetRuneDisplayMesh(owner));
    auto* flags=static_cast<uint8_t*>(owner->GetProperty("JointFlags"));for(int i=0;i<50;i++)flags[i]=0;
    auto* proxy=make("InterpolationProxy","RunePlayerProxy");proxy->Owner()=owner;proxy->EnableEvent("AnimEnd");
    UObject::Cast<UPawn>(owner)->Weapon()=nullptr;owner->SetObject("AnimProxy",proxy);owner->PlayAnim("neutral_idle",1,0);owner->AnimFrame()=0;
    auto* axe=trans->NewObject("InterpolationAxe",runei->GetClass("HandAxe"),ObjectFlags::Transient);
    NameString attack=axe->GetName("A_AttackA");require(m->GetSequence(attack)!=nullptr,"Hand axe attack sequence missing");
    proxy->PlayAnim("walkforward7",1,0);proxy->AnimFrame()=.121f;
    auto p0=m->ActorPose(owner);proxy->AnimFrame()=.122f;auto p1=m->ActorPose(owner);bool movingUpper=false;
    for(size_t j=0;j<p0.size();j++){
        float delta=length(p0[j].Position-p1[j].Position);auto r0=RunePoseRotation(p0[j].Rotation),r1=RunePoseRotation(p1[j].Rotation);for(int k=0;k<16;k++)delta+=std::abs(r0[k]-r1[k]);
        if(m->Joints[j].Group==0)require(delta<.0001f,"Proxy changed lower-body group");else if(delta>.0001f)movingUpper=true;
    }require(movingUpper,"Fractional proxy phase did not move upper body");
    auto before=m->JointMatrices(m->SampleAnimation(proxy));proxy->PlayAnim(attack,1,.15f);require(matrixError(before,m->JointMatrices(m->SampleAnimation(proxy)))<.005f,"Proxy transition snaps");
    std::cout<<"RUNE_INTERPOLATION_PROXY_OK fractional=1 lower_body_preserved=1 transition=1\n";
    for(NameString name:{NameString("walkforward7"),attack})for(int hz:{30,60,120}) {
        struct Result {float duration=0;int changes=0,ends=0;Array<int> frames;};Result results[2];
        for(int mode=0;mode<2;mode++) {
            RuneAnimation::Interpolate=mode!=0;auto& result=results[mode];
            proxy->PlayAnim("neutral_idle",1,0);proxy->PlayAnim(name,1,.12f);proxy->bAnimNotify()=false;proxy->SetBool("bFrameNotifies",true);
            auto savedTraces=Frame::Traces;
            Frame::AddTrace("FrameNotify",[&](UFunction*,UObject* instance,const ArrayView<ExpressionValue>& args){if(instance==proxy)result.frames.push_back(args[0].ToInt());});
            Frame::AddTrace("AnimEnd",[&](UFunction*,UObject* instance,const ArrayView<ExpressionValue>&){if(instance==proxy)result.ends++;});
            struct RestoreTraces {Array<Trace> traces;~RestoreTraces(){Frame::Traces=std::move(traces);}} resetTraces{savedTraces};
            auto previous=m->ActorJointMatrices(owner);
            for(int step=0;step<hz*10 && (proxy->AnimRate()!=0||proxy->AnimFrame()<0);step++){
                proxy->TickAnimation(1.0f/hz);result.duration+=1.0f/hz;
                auto current=m->ActorJointMatrices(owner);if(matrixError(previous,current)>.0001f)result.changes++;previous=std::move(current);
            }
            const auto* info=m->GetSequence(name);float expected=.12f+(info->NumFrames-1)/info->Rate;
            require(std::abs(result.duration-expected)<1.01f/hz,"Animation duration differs from authored rate");
            require(result.ends==1,"AnimEnd count differs");
            require(result.frames.size()==(size_t)(info->NumFrames-1),"FrameNotify skipped or repeated");
            for(size_t i=0;i<result.frames.size();i++)require(result.frames[i]==(int)i+1,"FrameNotify order changed");
        }
        require(results[0].duration==results[1].duration&&results[0].frames==results[1].frames,"Smoothing changed event timing");
        if(hz==120)require(results[1].changes>results[0].changes*2,"120 Hz did not produce more intermediate poses");
        std::cout<<"RUNE_INTERPOLATION_TIMING_OK sequence="<<name.ToString()<<" hz="<<hz<<" seconds="<<results[1].duration<<" old_poses="<<results[0].changes<<" smooth_poses="<<results[1].changes<<" frame_notifies="<<results[1].frames.size()<<" ends="<<results[1].ends<<'\n';
    }
}
