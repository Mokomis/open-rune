#include "RenderDevice/Vulkan/ShadowReceiverBounds.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <random>

int main()
{
    mat4 identity{};
    for(int i=0;i<4;++i)identity[i*5]=1;
    assert(ShadowReceiverMayIntersect(identity,vec3(-.1f,-.1f,.1f),vec3(.1f,.1f,.9f)));
    assert(ShadowReceiverMayIntersect(identity,vec3(1.2f,0,.5f),vec3(1.2f,0,.5f))); // offset can reach volume
    assert(!ShadowReceiverMayIntersect(identity,vec3(2,0,.5f),vec3(3,1,.7f)));
    assert(!ShadowReceiverMayIntersect(identity,vec3(0,0,-2),vec3(.1f,.1f,-1)));
    assert(!ShadowReceiverMayIntersect(identity,vec3(0,0,2),vec3(.1f,.1f,3)));
    mat4 invalid=identity; invalid[0]=std::numeric_limits<float>::quiet_NaN();
    assert(ShadowReceiverMayIntersect(invalid,vec3(2),vec3(3)));

    // Independent sampled oracle: project points and shader-sized normal
    // offsets through arbitrary affine/projective matrices. Any visible sample
    // requires the entire receiver to survive the conservative CPU test.
    std::mt19937 rng(7793);
    std::uniform_real_distribution<float> unit(0,1),signedUnit(-1,1);
    int visible=0,rejected=0;
    for(int test=0;test<100000;++test)
    {
        mat4 m{}; for(int i=0;i<16;++i)m[i]=signedUnit(rng);
        vec3 low(signedUnit(rng)*100,signedUnit(rng)*100,signedUnit(rng)*100);
        vec3 high=low+vec3(unit(rng)*20,unit(rng)*20,unit(rng)*20);
        bool retained=ShadowReceiverMayIntersect(m,low,high);
        rejected+=!retained;
        for(int sample=0;sample<32;++sample)
        {
            vec3 p;
            for(int axis=0;axis<3;++axis)
                p[axis]=low[axis]+unit(rng)*(high[axis]-low[axis])+signedUnit(rng)*.37f;
            double q[4]{};
            for(int r=0;r<4;++r)q[r]=double(m[r])*p.x+double(m[4+r])*p.y+double(m[8+r])*p.z+m[12+r];
            if(q[3]>0 && q[2]>0 && q[2]<q[3] && std::abs(q[0])<q[3] && std::abs(q[1])<q[3])
            { ++visible; assert(retained); }
        }
    }
    assert(visible>1000 && rejected>1000);
    std::cout << "SHADOW_RECEIVER_BOUNDS_OK cases=100000 samples=3200000 visible=" << visible << " rejected=" << rejected << '\n';
}
