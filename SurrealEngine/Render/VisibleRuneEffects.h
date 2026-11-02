#pragma once
#include "RuneVisualEffects.h"
#include "RuneParticles.h"
#include "VisibleFrame.h"
#include <array>
#include "Packages/Engine/Resources/Level/UModel.h"

// Cosmetic replacement of existing particles: never spawn gameplay actors,
// consume gameplay randomness, or change particle lifetimes/physics.
namespace RuneEffects {
inline constexpr int FlameFrames=64;
inline constexpr float FlameFps=32.f; // Two-second cycle; preserve frame cadence.
enum Kind { Original, Flame, Metal, Wood, Stone, Blood, GreenBlood };
inline Kind Classify(UActor* actor) {
    static const NameString fire("Fire"),torch("TorchFire"),small("SmallFire"),big("BigFire");
    static const NameString metal("HitMetal"),weapon("HitWeapon"),spark("SparkSystemHit");
    static const NameString wood("HitWood"),stone("HitStone"),blood("BloodSpray"),green("GreenBloodSpray");
    const auto name=actor->Class->Name;
    // Exact normal-fire classes: preserve respawn, magical and colored flames.
    if(name==fire||name==torch||name==small||name==big)return Flame;
    if(actor->IsA(metal)||actor->IsA(weapon)||actor->IsA(spark))return Metal;
    if(actor->IsA(wood))return Wood;
    if(actor->IsA(stone))return Stone;
    if(actor->IsA(green))return Original;
    if(actor->IsA(blood))return Original;
    return Original;
}
struct Sprite {
    bool worldSurface;
    std::array<UnrealMipmap,8> mips;
    Sprite(int phase,bool flame,bool world=false):worldSurface(world) {
        auto& m=mips[0];m.Width=m.Height=128;m.UBits=m.VBits=7;m.Data.resize(128*128*4);
        const float angle=phase*6.2831853f/FlameFrames;
        for(int y=0;y<128;y++)for(int x=0;x<128;x++) {
            float px=(x+.5f)/64-1, h=(y+.5f)/128;
            float v=h;
            // Original DMflame2 has a black border below row 106. Its BSP UVs
            // wrap across that border at the top of the crossed flame planes.
            if(world)h=(.82f-h)/.43f;
            float a;vec3 rgb;
            if(flame && h>=0 && h<=1) {
                // A tapered, three-tongue flame with a hot base and ember tips.
                float center=.16f*std::sin(h*8-angle)+.08f*std::sin(h*17+angle*2);
                float width=.055f+.43f*std::pow(1-h,.85f);
                float body=std::exp(-std::pow((px-center)/width,2.f)*2.2f);
                for(int branch=-1;branch<=1;branch+=2) {
                    float top=.65f+.12f*std::sin(angle+branch);
                    float reach=std::clamp((top-h)*8.f,0.f,1.f);
                    float tongue=.25f*branch+.08f*std::sin(h*11-angle+branch);
                    float spread=.035f+.12f*(1-h);
                    body=std::max(body,.8f*reach*std::exp(-std::pow((px-tongue)/spread,2.f)*2.f));
                }
                float split=.72f+.28f*std::sin(px*13+h*16-angle*2);
                a=body*split*std::clamp(h*18.f,0.f,1.f)*std::clamp((1-h)*9.f,0.f,1.f);
                float heat=std::clamp(body*(1-h)*1.35f,0.f,1.f);
                rgb=vec3(1.f,.18f+.77f*heat,.015f+.42f*heat*heat)*a;
            } else if(!flame) {
                float py=h*2-1;
                a=std::pow(std::max(0.f,1-px*px-py*py),2.f);
                rgb=vec3(a);
            } else {a=0;rgb=vec3(0);}
            if(world) for(int i=0;i<4;i++) {
                float life=std::fmod(phase/float(FlameFrames)+i*.25f,1.f);
                float ex=.20f*std::sin(angle+i*2.4f);
                float ey=.70f-life*.55f;
                float dx=(px-ex)/.018f,dy=(v-ey)/.025f;
                float glow=std::max(0.f,1-dx*dx-dy*dy)*std::sin(life*3.14159265f)*.65f;
                rgb+=vec3(1,.32f,.025f)*glow;a=std::max(a,glow);
            }
            int n=(y*128+x)*4;
            for(int k=0;k<3;k++)m.Data[n+k]=(uint8_t)std::lround(std::clamp(rgb[k],0.f,1.f)*255);
            m.Data[n+3]=world?255:(uint8_t)std::lround(a*255);
        }
        for(int level=1;level<(world?1:8);level++) {
            auto& prev=mips[level-1];auto& next=mips[level];
            next.Width=next.Height=prev.Width/2;next.UBits=next.VBits=7-level;next.Data.resize(next.Width*next.Height*4);
            for(int y=0;y<next.Height;y++)for(int x=0;x<next.Width;x++)for(int c=0;c<4;c++) {
                int sum=0;for(int j=0;j<2;j++)for(int i=0;i<2;i++)sum+=prev.Data[((y*2+j)*prev.Width+x*2+i)*4+c];
                next.Data[(y*next.Width+x)*4+c]=(uint8_t)((sum+2)/4);
            }
        }
    }
    TextureInfo Info() {
        TextureInfo info;info.CacheID=(uint64_t)(uintptr_t)this;info.Format=TextureFormat::RGBA8_;
        info.USize=info.VSize=128;info.Mips=mips.data();info.NumMips=worldSurface?1:8;return info;
    }
};
inline TextureInfo Texture(int phase,bool flame,bool world=false) {
    if(!flame){static Sprite soft(0,false);return soft.Info();}
    auto make=[](bool isWorld){std::vector<Sprite> result;result.reserve(FlameFrames);for(int i=0;i<FlameFrames;i++)result.emplace_back(i,true,isWorld);return result;};
    if(world){static auto frames=make(true);return frames[phase&(FlameFrames-1)].Info();}
    static auto frames=make(false);return frames[phase&(FlameFrames-1)].Info();
}
// Retain the source single mip, UV scale and empty wrap border. This authored
// surface flame ends near V=.82; extending its base to V=1 repeats it at the
// top of the crossed brush planes. Geometry and lighting remain unchanged.
// Other FireTextures include lightning/lava/magic and deliberately stay original.
inline void WorldTexture(TextureInfo& info,float time) {
    auto* t=info.Texture;
    if(!RuneVisualEffects::Enabled.load() || !t || !std::isfinite(time))return;
    static const NameString package("RuneFX2"),name("DMflame2");
    if(t->package->GetPackageName()!=package || t->Name!=name || t->USize()!=128 || t->VSize()!=128 || !t->IsA("FireTexture"))return;
    auto enhanced=Texture((int)std::fmod(time*FlameFps,float(FlameFrames)),true,true);
    enhanced.USize=info.USize;enhanced.VSize=info.VSize;
    enhanced.UScale=info.UScale;enhanced.VScale=info.VScale;
    info=enhanced;
    static bool logged=false;if(!logged){logged=true;LogMessage("RUNE_EFFECTS_WORLD texture=RuneFX2.DMflame2 frames=64 cycle_seconds=2 size=128 embers=4");}
}
inline void Quad(VisibleFrame* frame,TextureInfo& texture,const vec3& center,const vec3& side,const vec3& up,const vec3& color,float opacity,uint32_t flags) {
    GouraudVertex v[4];
    v[0].Point=center-side-up;v[1].Point=center+side-up;v[2].Point=center+side+up;v[3].Point=center-side+up;
    v[0].UV={0,0};v[1].UV={128,0};v[2].UV={128,128};v[3].UV={0,128};
    for(auto& vertex:v){vertex.Light=color;vertex.Fog=vec4(0);vertex.Opacity=opacity;}
    frame->Device->DrawGouraudPolygon(&frame->Frame,texture,v,4,PF_TwoSided|flags);
    --frame->EffectQuadBudget;
}
inline bool Draw(VisibleFrame* frame,UActor* actor,RuneParticles::ArrayView& particles,int index,Kind kind,float width,float height) {
    if(kind==Original||frame->ShadowView||frame->EffectQuadBudget<=0)return false;
    const vec3 p=particles.Ref<vec3>(index,RuneParticles::Field::Location);
    float distance=length(p-frame->ViewLocation.xyz());if(distance>1800)return false;
    float fade=std::clamp((1800-distance)/500.f,0.f,1.f);
    vec3 alpha=particles.Ref<vec3>(index,RuneParticles::Field::Alpha)*actor->ScaleGlow();
    for(int k=0;k<3;k++)alpha[k]=std::clamp(alpha[k],0.f,1.f);
    float opacity=std::max({alpha.x,alpha.y,alpha.z});
    float sx=std::abs(width*particles.Ref<float>(index,RuneParticles::Field::XScale))*.5f;
    float sy=std::abs(height*particles.Ref<float>(index,RuneParticles::Field::YScale))*.5f;
    if(!std::isfinite(sx+sy+opacity)||sx<.001f||sy<.001f)return false;
    float time=actor->GetFloat(RuneParticles::PropertyName(RuneParticles::Property::CurrentTime));
    if(!std::isfinite(time))return false;
    if(kind==Flame) {
        auto texture=Texture((int)std::fmod(time*FlameFps+index*5,float(FlameFrames)),true);
        vec3 side=frame->ViewRotation.YAxis,up=frame->ViewRotation.ZAxis;
        if(actor->GetByte("ParticleSpriteType")==1){side.z=0;side=dot(side,side)>.000001f?normalize(side):vec3(1,0,0);up=vec3(0,0,1);}
        Quad(frame,texture,p,side*sx,up*sy,alpha*.95f,1,PF_Translucent);
        // Four small, rising embers per emitter, only near the viewer. All stay
        // within the existing conservative particle bounds; no extra lights.
        if(index==0&&distance<900) {
            auto ember=Texture(0,false);
            for(int i=0;i<4&&frame->EffectQuadBudget>0;i++) {
                float t=std::fmod(time*.325f+i*.25f,1.f);
                vec3 q=p+vec3(std::sin(time*.65f+i*2.4f)*sx*.4f,std::cos(time*.5f+i*2.f)*sx*.35f,(t-.15f)*sy);
                float glow=std::sin(t*3.14159265f)*opacity*fade;
                float radius=std::clamp(sx*.035f,.35f,.9f);
                Quad(frame,ember,q,frame->ViewRotation.YAxis*radius,frame->ViewRotation.ZAxis*radius*1.8f,vec3(1.f,.38f,.035f)*glow,1,PF_Translucent);
            }
        }
    } else {
        auto texture=Texture(0,false);
        vec3 velocity=particles.Ref<vec3>(index,RuneParticles::Field::Velocity);
        vec3 tangent=frame->ViewRotation.YAxis*dot(velocity,frame->ViewRotation.YAxis)+frame->ViewRotation.ZAxis*dot(velocity,frame->ViewRotation.ZAxis);
        float speed=length(tangent);
        vec3 up=speed>.01f?tangent/speed:frame->ViewRotation.ZAxis;
        vec3 side=frame->ViewRotation.YAxis*dot(up,frame->ViewRotation.ZAxis)-frame->ViewRotation.ZAxis*dot(up,frame->ViewRotation.YAxis);
        float radius=std::clamp(std::min(sx,sy)*.3f,.35f,2.5f);
        float trail=std::clamp(speed*.012f,radius,std::max(radius,std::min(12.f,std::max(sx,sy))));
        vec3 color;uint32_t flags=PF_Highlighted;
        if(kind==Metal){color=vec3(1.f,.65f,.18f);flags=PF_Translucent;}
        else if(kind==Wood)color=vec3(.28f,.14f,.055f);
        else if(kind==Stone)color=vec3(.32f,.29f,.24f);
        else color=kind==GreenBlood?vec3(.04f,.22f,.025f):vec3(.28f,.009f,.005f);
        Quad(frame,texture,p,side*radius,up*trail,color,opacity,flags);
    }
    static std::set<int> drawn;
    if(drawn.insert(int(kind)).second)LogMessage("RUNE_EFFECTS_DRAW kind="+std::to_string(int(kind)));
    return true;
}
}
