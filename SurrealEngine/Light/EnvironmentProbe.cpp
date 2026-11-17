#include "Precomp.h"
#include "LightSystem.h"
#include "Engine.h"
#include "Render/RenderSubsystem.h"
#include "RenderDevice/RenderDevice.h"
#ifdef __ANDROID__
#include "RenderDevice/Vulkan/ShaderOptimizationDefaults.h"
#include "RenderDevice/Vulkan/VulkanRenderDevice.h"
#endif
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include "Packages/Engine/Resources/UPalette.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Collision/BottomLevel/TraceRayModel.h"
#include "Math/hsb.h"
#include "Utils/Logger.h"
#include "Utils/CommandLine.h"
#include <atomic>
#include <bit>
#include <future>
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
    std::array<vec3,24> directions;
    for (int face=0; face<6; ++face) {
        const vec3 axis=axes[face], u=axes[(face/2+1)%3*2], v=cross(axis,u);
        int ray=face*4;
        for (int y : {-1,1}) for (int x : {-1,1})
            directions[ray++]=normalize(axis+u*(float(x)*.45f)+v*(float(y)*.45f));
    }

    struct RayHit {
        int surface=-1;
        float distance=0;
        vec3 normal{0};
    };
    using ProbeHits = std::array<RayHit,24>;
    struct ProbeLocation { int leaf; vec3 position; };
    std::vector<ProbeLocation> probes;
    for (const auto& [leaf, points] : positions)
        for (const auto& point : points) probes.push_back({leaf,point});

    auto traceProbe = [&](const vec3& position, TraceRayModel& tracer) {
        ProbeHits result;
        for (size_t i=0; i<directions.size(); ++i) {
            // A null owner selects the static world's surface flags directly.
            // Workers only read immutable BSP data and never touch actor properties.
            auto hits = tracer.Trace(nullptr, model, to_dvec3(position), 1.0,
                                     to_dvec3(directions[i]), 4096.0, true);
            if (!hits.empty() && hits.front().Node) {
                const auto& hit = hits.front();
                result[i] = {hit.Node->Surf, hit.Fraction, hit.Normal};
            }
        }
        return result;
    };
    auto publish = [&](const ProbeLocation& point, const ProbeHits& hits) {
        EnvironmentProbe probe{};
        probe.Position=point.position;
        for (int face=0; face<6; ++face) {
            vec3 color(0);
            for (int ray=face*4; ray<(face+1)*4; ++ray) {
                const auto& hit=hits[ray];
                if (hit.surface<0 || size_t(hit.surface)>=model->Surfaces.size()) continue;
                const auto& surface=model->Surfaces[hit.surface];
                if (!(surface.PolyFlags&PF_FakeBackdrop))
                    color+=radiance(hit.surface,hit.normal,point.position+directions[ray]*hit.distance)*.25f;
            }
            probe.Light.Lobes[face]=vec4(color,1);
        }
        Environment.Leaves[point.leaf].push_back(probe);
    };

    // The only parallel phase is read-only tracing. Cache population and
    // floating-point accumulation execute on the caller in original order.
    constexpr size_t maxParallelProbes=8192;
    int defaultWorkers=0;
#ifdef __ANDROID__
    const auto* renderer=dynamic_cast<const VulkanRenderDevice*>(engine->render->Device);
    if (renderer && UsesQualifiedShaderDefaults(*renderer->Device)) defaultWorkers=1;
#endif
    const bool requestWorker=commandline->GetArgInt("", "--environment-probe-workers", defaultWorkers)>0 &&
        probes.size()>=64 && probes.size()<=maxParallelProbes;
    bool usedWorker=false;
    if (requestWorker) {
        std::vector<ProbeHits> results(probes.size());
        std::atomic<size_t> next{0};
        auto traceBatch = [&] {
            TraceRayModel tracer;
            constexpr size_t chunk=8;
            for (;;) {
                const size_t first=next.fetch_add(chunk,std::memory_order_relaxed);
                if (first>=probes.size()) break;
                for (size_t index=first; index<std::min(first+chunk,probes.size()); ++index)
                    results[index]=traceProbe(probes[index].position,tracer);
            }
        };
        std::future<void> helper;
        try {
            helper=std::async(std::launch::async,traceBatch);
        } catch (const std::system_error&) {
            // A host that cannot create a helper can trace on the caller.
        }
        usedWorker=helper.valid();
        traceBatch();
        if (usedWorker) helper.get();
        for (size_t i=0; i<probes.size(); ++i) publish(probes[i],results[i]);
    } else {
        TraceRayModel tracer;
        for (const auto& probe : probes) publish(probe,traceProbe(probe.position,tracer));
    }
    const size_t count=probes.size(),rays=count*directions.size();
    double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    LogMessage("RUNE_ENVIRONMENT_PROBES probes="+std::to_string(count)+" leaves="+std::to_string(Environment.Leaves.size())+
        " rays="+std::to_string(rays)+" bake_ms="+std::to_string(ms)+" frame_rays=0 helpers="+std::to_string(usedWorker));
    if (commandline->HasArg("", "--frame-benchmark")) {
        uint64_t signature=14695981039346656037ull;
        auto hashValue=[&](uint32_t bits) {
            signature=(signature ^ bits)*1099511628211ull;
        };
        for (const auto& [leaf, entries] : Environment.Leaves) {
            hashValue(static_cast<uint32_t>(leaf));
            for (const auto& probe : entries) {
                for (size_t axis=0; axis<3; ++axis)
                    hashValue(std::bit_cast<uint32_t>(probe.Position[axis]));
                for (const auto& lobe : probe.Light.Lobes) {
                    hashValue(std::bit_cast<uint32_t>(lobe.x));
                    hashValue(std::bit_cast<uint32_t>(lobe.y));
                    hashValue(std::bit_cast<uint32_t>(lobe.z));
                    hashValue(std::bit_cast<uint32_t>(lobe.w));
                }
            }
        }
        LogMessage("RUNE_ENVIRONMENT_SIGNATURE "+std::to_string(signature));
    }
}

EnvironmentLight LightSystem::SampleEnvironment(const vec3& position) const
{
    if(!Environment.Ready || !Level || !Level->Model)return {};
    auto region=Level->Model->FindRegion(position,engine->LevelInfo);
    return Environment.Sample(region.BspLeaf,position);
}
