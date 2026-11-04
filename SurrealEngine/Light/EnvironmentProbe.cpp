#include "Precomp.h"
#include "LightSystem.h"
#include "Engine.h"
#include "Render/RenderSubsystem.h"
#include "RenderDevice/RenderDevice.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include "Packages/Engine/Resources/UPalette.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Collision/BottomLevel/TraceRayModel.h"
#include "Math/hsb.h"
#include "Utils/Logger.h"
#include <chrono>
#include <set>
#include <tuple>

void LightSystem::PrepareEnvironmentProbes()
{
    if(Environment.Ready || !Level || !Level->Model || !engine->LaunchInfo.IsRune() ||
       (engine->render->Device->WorldFeatures & (512|131072))!=(512|131072)) return;
    Environment.Ready=true;
    auto start=std::chrono::steady_clock::now();
    auto* model=Level->Model;
    if(model->Nodes.empty()) return;
    // Loading/toggle work only. Never bake moving actors or brushes into the
    // environment: doors, combat, shadows and scripted lighting stay live.
    std::map<int,std::vector<vec3>> positions;
    std::set<std::tuple<int,int,int,int>> occupied;
    auto add=[&](vec3 p) {
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return;
        auto region=model->FindRegion(p,engine->LevelInfo);
        if(region.BspLeaf<0)return;
        auto key=std::make_tuple(region.BspLeaf,int(std::floor(p.x/256)),int(std::floor(p.y/256)),int(std::floor(p.z/256)));
        auto& leaf=positions[region.BspLeaf];
        // Bounded cost per leaf, independent of the total playable map size.
        if(leaf.size()<4 && occupied.insert(key).second)leaf.push_back(p);
    };
    for(auto* actor:Level->Actors) if(actor && (actor->IsA("NavigationPoint")||actor->IsA("Pawn"))) add(actor->Location());
    for(const auto& node:model->Nodes) {
        if(node.NumVertices<3 || node.Surf<0 || size_t(node.Surf)>=model->Surfaces.size())continue;
        auto& surface=model->Surfaces[node.Surf];
        if(surface.PolyFlags&(PF_Invisible|PF_NotSolid|PF_FakeBackdrop))continue;
        vec3 center(0);
        for(int i=0;i<node.NumVertices;++i)center+=model->Points[model->Vertices[node.VertPool+i].Vertex];
        center/=float(node.NumVertices);
        vec3 normal(node.PlaneX,node.PlaneY,node.PlaneZ);
        add(center+normal*24.0f);add(center-normal*24.0f);
    }
    std::map<UTexture*,vec3> albedos;
    auto albedo=[&](UTexture* texture) {
        if(!texture)return vec3(.35f);
        auto found=albedos.find(texture);if(found!=albedos.end())return found->second;
        vec3 color(.35f);
        if(texture->UsedFormat==TextureFormat::P8 && texture->Palette() && !texture->UsedMipmaps.empty()) {
            auto& mip=texture->UsedMipmaps[std::min(size_t(3),texture->UsedMipmaps.size()-1)];
            auto& palette=texture->Palette()->Colors;
            vec3 sum(0);int samples=0;
            for(size_t i=0,step=std::max(size_t(1),mip.Data.size()/64);i<mip.Data.size();i+=step) {
                auto index=mip.Data[i];if(index>=palette.size() || (index==0 && texture->bMasked()))continue;
                uint32_t c=palette[index];sum+=vec3(float(c&255),float((c>>8)&255),float((c>>16)&255))/255.0f;++samples;
            }
            if(samples)color=sum/float(samples);
        }
        albedos[texture]=color;return color;
    };
    std::map<int,vec3> surfaceRadiance;
    auto radiance=[&](int index,vec3 normal,vec3 hitPosition) {
        int key=index*2+(dot(model->Vectors[model->Surfaces[index].vNormal],normal)<0?1:0);
        auto found=surfaceRadiance.find(key);if(found!=surfaceRadiance.end())return found->second;
        auto& surface=model->Surfaces[index];
        vec3 point=hitPosition+normal*2.0f;
        auto region=model->FindRegion(point,engine->LevelInfo);
        auto* zone=region.Zone?region.Zone:engine->LevelInfo;
        vec3 light=hsbtorgb(zone->AmbientHue(),zone->AmbientSaturation(),zone->AmbientBrightness())*3.0f;
        if(surface.PolyFlags&PF_Unlit)light=vec3(.5f);
        else if(surface.LightMap>=0 && size_t(surface.LightMap)<model->LightMap.size()) {
            // Only authored light links, with static-world visibility. Exclude
            // special/animated effects rather than baking them in the wrong state.
            int first=model->LightMap[surface.LightMap].LightActors;
            for(int i=first;i>=0 && size_t(i)<model->Lights.size();++i) {
                auto* source=model->Lights[i];if(!source)break;
                if(!source->bStatic() || source->bSpecialLit() || !source->LightBrightness() || source->LightType()!=LT_Steady)continue;
                int effect=source->LightEffect();if(effect!=LE_None && effect!=LE_TorchWaver && effect!=LE_FireWaver)continue;
                float radius=source->WorldLightRadius();vec3 L=source->Location()-point;float length2=dot(L,L);
                if(radius<=0 || length2>=radius*radius || TraceLevelVisibility(point,source->Location(),nullptr))continue;
                float angle=std::max(dot(normal,L/std::sqrt(std::max(length2,.001f))),0.0f);
                light+=LightmapBuilder::GetLightColor(source)*VertexLight::LightDistanceFalloff(length2/(radius*radius))*angle*3.0f;
            }
        }
        // One bounded bounce; preserve the original artwork and darkness.
        for(int axis=0;axis<3;++axis)light[axis]=std::clamp(light[axis],0.0f,1.0f);
        vec3 result=light*albedo(surface.Material);
        surfaceRadiance[key]=result;return result;
    };
    const vec3 axes[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    TraceRayModel tracer;size_t count=0,rays=0;
    for(auto& entry:positions)for(const vec3& position:entry.second) {
        EnvironmentProbe probe{};probe.Position=position;
        for(int face=0;face<6;++face) {
            vec3 axis=axes[face],u=axes[(face/2+1)%3*2],v=cross(axis,u),color(0);
            for(int y:{-1,1})for(int x:{-1,1}) {
                vec3 direction=normalize(axis+u*(float(x)*.45f)+v*(float(y)*.45f));
                auto hits=tracer.Trace(engine->LevelInfo,model,to_dvec3(position),1.0,to_dvec3(direction),4096.0,true);++rays;
                if(!hits.empty() && hits.front().Node) {
                    auto& hit=hits.front();auto& surface=model->Surfaces[hit.Node->Surf];
                    // Backdrop imagery is not a physical reflecting wall. Keep
                    // its contribution empty; original zone ambient stays live.
                    if(!(surface.PolyFlags&PF_FakeBackdrop))color+=radiance(hit.Node->Surf,hit.Normal,position+direction*hit.Fraction)*.25f;
                }
            }
            probe.Light.Lobes[face]=vec4(color,1);
        }
        Environment.Leaves[entry.first].push_back(probe);++count;
    }
    double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    LogMessage("RUNE_ENVIRONMENT_PROBES probes="+std::to_string(count)+" leaves="+std::to_string(Environment.Leaves.size())+
        " rays="+std::to_string(rays)+" bake_ms="+std::to_string(ms)+" frame_rays=0");
}

EnvironmentLight LightSystem::SampleEnvironment(const vec3& position) const
{
    if(!Environment.Ready || !Level || !Level->Model)return {};
    auto region=Level->Model->FindRegion(position,engine->LevelInfo);
    return Environment.Sample(region.BspLeaf,position);
}
