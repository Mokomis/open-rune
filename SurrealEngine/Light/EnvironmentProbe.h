#pragma once
#include "Math/vec.h"
#include <array>
#include <map>
#include <vector>

// Low-frequency radiance, +X/-X/+Y/-Y/+Z/-Z. This deliberately represents
// broad room reflections, not mirror images. No render target or per-frame rays.
struct EnvironmentLight
{
    std::array<vec4, 6> Lobes{};
};

struct EnvironmentProbe
{
    vec3 Position;
    EnvironmentLight Light;
};

class EnvironmentProbeField
{
public:
    void Clear() { Leaves.clear(); Ready=false; }
    EnvironmentLight Sample(int leaf, const vec3& position) const
    {
        EnvironmentLight result{};
        auto found=Leaves.find(leaf);
        if(found==Leaves.end()) return result;
        // BSP leaves are convex: never interpolate through a wall merely
        // because two probes share an authored zone number.
        float weights=0;
        for(const auto& probe:found->second) {
            vec3 delta=probe.Position-position;
            float weight=1.0f/(64.0f*64.0f+dot(delta,delta));
            for(int i=0;i<6;++i) result.Lobes[i]+=probe.Light.Lobes[i]*weight;
            weights+=weight;
        }
        if(weights>0) for(auto& lobe:result.Lobes) lobe/=weights;
        return result;
    }
    std::map<int,std::vector<EnvironmentProbe>> Leaves;
    bool Ready=false;
};
