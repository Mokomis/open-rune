#pragma once
#include <cctype>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// Open Rune's authored procedural profiles. These estimate detail from the
// user's textures; they are not artist-painted replacement game assets.
namespace MaterialMaps
{
enum Kind { Generic, Stone, Wood, Metal, Skin, Cloth };
inline Kind Classify(std::string name)
{
    for(char& c:name) c=char(std::tolower((unsigned char)c));
    auto has=[&](const char* s){return name.find(s)!=std::string::npos;};
    if(has("wood")||has("plank")) return Wood;
    if(has("rock")||has("stone")||has("brck")||has("brick")) return Stone;
    if(has("metal")||has("steel")||has("iron")||has("axe")||has("sword")||has("hammer")||has("blade")||has("shield")) return Metal;
    if(has("cloth")||has("banner")||has("fabric")||has("sail")) return Cloth;
    if(has("ship")||has("hull")) return Wood;
    if(has("ragnar")||has("goblin")||has("viking")||has("dwarf")||has("crab")||has("skin")||has("sark")) return Skin;
    return Generic;
}
inline std::vector<uint8_t> Generate(const std::vector<float>& luma, const std::vector<float>& warmth, int w,int h,Kind kind)
{
    std::vector<float> height(luma.size());
    auto at=[&](int x,int y){return luma[((y+h)%h)*w+(x+w)%w];};
    const float amplitude[]={0.35f,1.8f,1.1f,0.55f,0.32f,0.6f};
    const float roughness[]={0.78f,0.84f,0.72f,0.30f,0.65f,0.94f};
    for(int y=0;y<h;++y) for(int x=0;x<w;++x)
    {
        float blur=0;for(int j=-2;j<=2;++j)for(int i=-2;i<=2;++i)blur+=at(x+i,y+j)/25.0f;
        float detail=at(x,y)-blur; // reject broad painted illumination
        float u=float(x)/w,v=float(y)/h;
        float micro=0;
        if(kind==Wood) micro=0.018f*std::sin(u*6.2831853f*32+std::sin(v*6.2831853f*3)*0.8f);
        if(kind==Metal) micro=0.008f*std::sin(v*6.2831853f*48);
        if(kind==Cloth) micro=0.022f*(std::sin(u*6.2831853f*32)+std::sin(v*6.2831853f*32));
        height[y*w+x]=detail*amplitude[kind]+micro;
    }
    auto sample=[&](int x,int y){return height[((y+h)%h)*w+(x+w)%w];};
    std::vector<uint8_t> result((size_t)w*h*4);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)
    {
        float dx=(sample(x+1,y)-sample(x-1,y))*1.8f,dy=(sample(x,y+1)-sample(x,y-1))*1.8f;
        float norm=1/std::sqrt(1+dx*dx+dy*dy);
        auto encode=[](float value){return uint8_t(std::lround(std::clamp(value,0.0f,1.0f)*255));};
        size_t i=(size_t)y*w+x,p=i*4;
        result[p]=encode(0.5f-dx*norm*0.5f);result[p+1]=encode(0.5f-dy*norm*0.5f);result[p+2]=encode(0.5f+norm*0.5f);
        float r=roughness[kind]+std::abs(height[i])*0.45f+(0.5f-luma[i])*0.12f;
        // Warm flesh/leather stays rough. Neutral armor regions get a tighter
        // response without treating bright skin as polished steel.
        if(kind==Skin) r=warmth[i]>0.10f ? 0.66f+std::abs(height[i])*0.35f : 0.48f+std::abs(height[i])*0.4f;
        result[p+3]=encode(std::clamp(r,0.18f,0.98f));
    }
    return result;
}
inline std::vector<uint8_t> Downsample(const std::vector<uint8_t>& src,int w,int h)
{
    int nw=std::max(1,w/2),nh=std::max(1,h/2);std::vector<uint8_t> out((size_t)nw*nh*4);
    for(int y=0;y<nh;++y)for(int x=0;x<nw;++x)
    {
        float n[3]={},r=0;
        for(int j=0;j<2;++j)for(int i=0;i<2;++i) {
            size_t p=(((y*2+j)%h)*w+(x*2+i)%w)*4;
            for(int k=0;k<3;++k)n[k]+=(src[p+k]/127.5f-1)*0.25f;
            r+=src[p+3]/255.0f*0.25f;
        }
        float length=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]),inv=1/std::max(length,0.0001f);
        size_t p=(y*nw+x)*4;
        for(int k=0;k<3;++k)out[p+k]=uint8_t(std::lround(std::clamp(n[k]*inv*0.5f+0.5f,0.0f,1.0f)*255));
        out[p+3]=uint8_t(std::lround(std::min(r+(1-length)*0.4f,1.0f)*255));
    }
    return out;
}
}
