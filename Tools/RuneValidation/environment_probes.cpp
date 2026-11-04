#include "Light/EnvironmentProbe.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
    EnvironmentProbeField field;
    assert(field.Sample(3,vec3(0)).Lobes[0].w==0);
    EnvironmentProbe a{},b{};a.Position=vec3(-100,0,0);b.Position=vec3(100,0,0);
    for(int i=0;i<6;++i) {a.Light.Lobes[i]=vec4(1,0,0,1);b.Light.Lobes[i]=vec4(0,0,1,1);}
    field.Leaves[1]={a,b};field.Leaves[2]={a};field.Ready=true;
    auto middle=field.Sample(1,vec3(0)).Lobes[0];
    assert(std::abs(middle.x-.5f)<1e-6f && std::abs(middle.z-.5f)<1e-6f && middle.w==1);
    assert(field.Sample(2,b.Position).Lobes[0].z==0); // no cross-wall light leakage
    assert(field.Sample(1,a.Position).Lobes[0].x>.9f);
    assert(field.Sample(99,a.Position).Lobes[0].w==0); // missing coverage is unmodified
    for(int x=-500;x<500;++x) {
        auto p=field.Sample(1,vec3(float(x),0,0)).Lobes[0];
        auto q=field.Sample(1,vec3(float(x)+.001f,0,0)).Lobes[0];
        assert(std::isfinite(p.x) && p.x>=0 && p.x<=1 && p.z>=0 && p.z<=1);
        assert(std::abs(p.x-q.x)<.001f);
    }
    field.Clear();assert(!field.Ready && field.Sample(1,vec3(0)).Lobes[0].w==0);
    std::cout<<"ENVIRONMENT_PROBE_FIELD_OK blend=1 wall_isolation=1 missing_coverage=1 smooth=1000 reset=1\n";
}
