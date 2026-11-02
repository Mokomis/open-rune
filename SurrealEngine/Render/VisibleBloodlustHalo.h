#pragma once
#include "RuneBloodlust.h"
#include "VisibleFrame.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Resources/Mesh/USkelModel.h"
#include "Packages/Engine/Resources/Level/UModel.h"

inline bool HasBloodlustHalo(VisibleFrame* frame,UActor* actor) {
    if(!engine->LaunchInfo.IsRune() || frame->ShadowView)return false;
    auto* p=UObject::TryCast<UPawn>(actor);
    return p && RuneBloodlust::Active(p);
}
inline void DrawBloodlustHalo(VisibleFrame* frame,UActor* actor) {
    if(!HasBloodlustHalo(frame,actor))return;
    auto* model=UObject::TryCast<USkelModel>(GetRuneDisplayMesh(actor));if(!model)return;
    int head=-1;for(size_t i=0;i<model->Joints.size();++i)if(model->Joints[i].Name=="head"){head=(int)i;break;}
    if(head<0)return;
    float visibility=1;
    if(actor==engine->viewport->Actor() && actor->GetBool("bClientSideAlpha"))visibility=std::clamp(actor->GetFloat("ClientSideAlphaScale"),0.f,1.f);
    if(visibility<.05f)return;
    // One shared, soft annular sprite. Depth-tested like other world particles;
    // it neither shines through walls nor casts a light/shadow pass.
    static UnrealMipmap mip=[](){
        UnrealMipmap m;m.Width=m.Height=64;m.UBits=m.VBits=6;m.Data.resize(64*64*4);
        for(int y=0;y<64;++y)for(int x=0;x<64;++x){
            float dx=(x+.5f-32)/32,dy=(y+.5f-32)/32,r=std::sqrt(dx*dx+dy*dy);
            float glow=std::exp(-std::pow((r-.67f)/.19f,2.f))*std::clamp((1-r)*8.f,0.f,1.f);
            int i=(y*64+x)*4;auto v=(uint8_t)std::lround(glow*255);m.Data[i]=m.Data[i+1]=m.Data[i+2]=v;m.Data[i+3]=255;
        }return m;
    }();
    TextureInfo info;info.CacheID=(uint64_t)(uintptr_t)&mip;info.Format=TextureFormat::RGBA8_;info.USize=info.VSize=64;info.Mips=&mip;info.NumMips=1;
    auto bones=model->ActorJointMatrices(actor);
    auto center=(GetRuneMeshToWorld(actor)*bones[head]*vec4(0,0,0,1)).xyz();
    float radius=14*actor->DrawScale();auto side=frame->ViewRotation.YAxis*radius,up=frame->ViewRotation.ZAxis*radius;
    GouraudVertex v[4];v[0].Point=center-side-up;v[1].Point=center+side-up;v[2].Point=center+side+up;v[3].Point=center-side+up;
    v[0].UV={0,0};v[1].UV={64,0};v[2].UV={64,64};v[3].UV={0,64};
    for(auto& vertex:v){vertex.Light=vec3(.20f,.007f,.005f)*visibility;vertex.Fog=vec4(0);}
    frame->Device->DrawGouraudPolygon(&frame->Frame,info,v,4,PF_TwoSided|PF_Translucent);
}
