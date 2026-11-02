#pragma once

#include "RuneParticles.h"
#include "RenderSubsystem.h"
#include "VisibleFrame.h"
#include "RenderDevice/RenderDevice.h"
#include "Packages/Engine/Resources/UPalette.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "VisibleRuneEffects.h"
#include "RuneBloodPresentation.h"

inline void DrawRuneParticles(VisibleFrame* frame,UActor* actor)
{
    if(!RuneParticles::IsSystem(actor) || actor->bDeleteMe() || !actor->GetBool("IsLoaded")) return;
    RuneParticleProfile::Scope profile(RuneParticleProfile::Draw);
    int type=actor->GetByte("ParticleType");
    if(type==1) return; // Swipe trail generation remains explicitly unsupported.
    int count=std::min(64,(int)actor->GetByte("ParticleCount"));
    if(type==3) count=std::min(count,3*std::max(0,std::min(32,(int)actor->GetByte("NumConPts"))-1));
    RuneParticles::ArrayView particles(actor);
    auto* textures=static_cast<UTexture**>(actor->GetProperty("ParticleTexture"));
    TextureInfo infos[4];
    UTexture* resolved[4]={};
    bool prepared[4]={};
    bool any=false;
    auto effect=RuneVisualEffects::Enabled.load()&&type==0?RuneEffects::Classify(actor):RuneEffects::Original;
    if(effect!=RuneEffects::Original) {
        static std::set<std::string> seen;
        if(seen.insert(actor->Class->Name.ToString()).second)
            std::cout<<"RUNE_EFFECTS class="<<actor->Class->Name.ToString()<<" kind="<<int(effect)<<" budget=192\n";
    }
    for(int i=0;i<count;i++)
    {
        if(!particles.Valid(i)) continue;
        int index=particles.Ref<uint8_t>(i,RuneParticles::Field::TextureIndex);
        if(index>=4) continue;
        if(!prepared[index])
        {
            prepared[index]=true;
            UTexture* texture=textures[index];
            if(texture)
            {
                engine->render->UpdateTexture(texture);
                texture=texture->GetAnimTexture();
                if(texture)
                {
                    engine->render->UpdateTexture(texture);
                    if(!texture->UsedMipmaps.empty())
                    {
                        engine->render->UpdateTextureInfo(infos[index],texture);
                        infos[index].UScale=infos[index].VScale=1.0f;
                        resolved[index]=texture;
                    }
                }
            }
        }
        UTexture* texture=resolved[index];
        if(!texture) continue;
        uint32_t flags=PF_TwoSided|texture->PolyFlags();
        int style=particles.Ref<uint8_t>(i,RuneParticles::Field::Style);
        if(style==2 || texture->bMasked()) flags|=PF_Masked;
        if(style==3) flags|=PF_Translucent;
        if(style==4) flags|=PF_Modulated;
        if(actor->bNoSmooth()) flags|=PF_NoSmooth;
        if(flags&PF_Invisible) continue;
        float width=(float)texture->USize(), height=(float)texture->VSize();
        int sprite=actor->GetByte("ParticleSpriteType");
        if((sprite==0 || (sprite==1 && effect==RuneEffects::Flame)) && RuneEffects::Draw(frame,actor,particles,i,effect,width,height)) {any=true;continue;}
        GouraudVertex vertices[4];
        float u0=0,v0=0,u1=1,v1=1;
        if(type==3 || sprite==3)
        {
            for(int j=0;j<4;j++) vertices[j].Point=particles.Ref<vec3>(i,RuneParticles::Field::Points,j);
            u0=particles.Ref<float>(i,RuneParticles::Field::U0); v0=particles.Ref<float>(i,RuneParticles::Field::V0);
            u1=particles.Ref<float>(i,RuneParticles::Field::U1); v1=particles.Ref<float>(i,RuneParticles::Field::V1);
        }
        else
        {
            vec3 side=frame->ViewRotation.YAxis, up=frame->ViewRotation.ZAxis;
            if(sprite==1)
            {
                side.z=0;
                side=dot(side,side)>0.000001f ? normalize(side) : vec3(1,0,0);
                up=vec3(0,0,1);
            }
            else if(sprite==2) { side=vec3(1,0,0); up=vec3(0,1,0); }
            else if(sprite!=0) { RuneParticles::Unsupported(actor,"sprite-orientation"); continue; }
            side*=0.5f*width*particles.Ref<float>(i,RuneParticles::Field::XScale);
            up*=0.5f*height*particles.Ref<float>(i,RuneParticles::Field::YScale);
            vec3 position=particles.Ref<vec3>(i,RuneParticles::Field::Location);
            vertices[0].Point=position-side-up;
            vertices[1].Point=position+side-up;
            vertices[2].Point=position+side+up;
            vertices[3].Point=position-side+up;
        }
        vertices[0].UV=vec2(u0*width,v0*height);
        vertices[1].UV=vec2(u1*width,v0*height);
        vertices[2].UV=vec2(u1*width,v1*height);
        vertices[3].UV=vec2(u0*width,v1*height);
        vec3 color=particles.Ref<vec3>(i,RuneParticles::Field::Alpha)*actor->ScaleGlow();
        for(int axis=0;axis<3;axis++) color[axis]=clamp(color[axis],0.0f,1.0f);
        auto bloodInfo=infos[index];
        bool crimson=RuneBloodPresentation::Recolor(actor,bloodInfo,flags,style);
        for(auto& vertex:vertices) { vertex.Light=crimson?vec3(1):color; vertex.Fog=vec4(0); if(crimson)vertex.Opacity=std::max({color.x,color.y,color.z}); }
        frame->Device->DrawGouraudPolygon(&frame->Frame,bloodInfo,vertices,4,flags);
        any=true;
    }
    if(any) engine->render->Stats.Actors++;
}
