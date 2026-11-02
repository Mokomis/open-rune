#pragma once
#include "RuneVisualEffects.h"
#include "RenderDevice/RenderDevice.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include "Packages/Engine/Actors/UActor.h"
#include <array>
#include <map>
#include <memory>

// Presentation only: never alter source palettes, authored emitters or gameplay RNG.
namespace RuneBloodPresentation {
inline bool RedEmitter(UActor* a) {
    return a && !a->IsA("SarkBloodMist") && !a->IsA("GreenBloodSpray") &&
        (a->IsA("BloodMist") || a->IsA("Blood") || a->IsA("BloodSpurt") || a->IsA("DrippingBlood"));
}
inline TextureColor Crimson(TextureColor source) {
    // Original modulated blood has a neutral gray background. Convert its
    // absorption into coverage so overlapping clouds don't multiply to black.
    float density=std::clamp((128.f-std::min(source.G,source.B))/128.f,0.f,1.f);
    // Reject neutral gray border/mip texels, retaining the original silhouette.
    density*=std::clamp((int(source.R)-std::min(source.G,source.B))/12.f,0.f,1.f);
    float alpha=density*.88f;
    // Muted crimson: lower red intensity without reducing silhouette/coverage.
    float red=.48f+.09f*(1-density);
    return TextureColor(uint8_t(std::lround(red*alpha*255)),uint8_t(std::lround(.060f*alpha*255)),
        uint8_t(std::lround(.035f*alpha*255)),uint8_t(std::lround(alpha*255)));
}
struct Palette {std::array<TextureColor,256> colors;};
inline std::map<UTexture*,std::unique_ptr<Palette>> palettes;
inline bool Recolor(UActor* actor,TextureInfo& info,uint32_t& flags,int style) {
    if(!RuneVisualEffects::Enabled.load() || !RedEmitter(actor) || (!(flags&PF_Modulated) && style!=5) || !info.Palette || info.Format!=TextureFormat::P8)return false;
    auto found=palettes.find(info.Texture);
    if(found==palettes.end()) {
        if(palettes.size()>=64)return false;
        auto p=std::make_unique<Palette>();for(int i=0;i<256;i++) {
            auto src=info.Palette[i];
            if(flags&PF_Modulated)p->colors[i]=Crimson(src);
            else {
                float shade=.40f+.14f*std::sqrt(src.R/255.f),a=.88f;
                p->colors[i]=TextureColor(uint8_t(shade*a*255),uint8_t(.055f*a*255),uint8_t(.035f*a*255),uint8_t(a*255));
            }
        }
        if(flags&PF_Masked)p->colors[0]=TextureColor(0,0,0,0);
        found=palettes.emplace(info.Texture,std::move(p)).first;
    }
    info.CacheID=uint64_t(uintptr_t(found->second.get()));info.Palette=found->second->colors.data();
    flags=(flags&~(PF_Modulated|PF_Translucent|PF_Masked|PF_Occlude))|PF_Highlighted;
    return true;
}
struct Wound {
    bool known=false;int health=0;double until=0,next=0,pending=0;
    bool Observe(int value,double now,bool redHit,bool reset=false) {
        if(reset || !known || (health<=0 && value>0)) {*this={};known=true;health=value;return false;}
        if(health>0 && health-value>=25)pending=now+.18;
        health=value;
        if(redHit && pending>now) {until=now+2.;next=now;pending=0;return true;}
        return false;
    }
    bool Emit(double now) {if(now>=until || now<next)return false;next=now+.12;return true;}
};
}
