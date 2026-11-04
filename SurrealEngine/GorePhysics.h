#pragma once
#include "Math/vec.h"
#include <algorithm>
#include <cmath>

// Local cosmetic fragments only. No actors, script callbacks or gameplay RNG.
namespace GorePhysics {
struct Hit {bool blocked=false;float fraction=1;vec3 normal=vec3(0,0,1);bool stable=true;};
struct Body {
    bool live=false,sleeping=false;
    vec3 position=vec3(0),velocity=vec3(0),angles=vec3(0),spin=vec3(0),restNormal=vec3(0,0,1);
    float radius=2,age=0,carry=0;unsigned contacts=0;
};
struct Impact {bool hit=false;vec3 position=vec3(0),normal=vec3(0,0,1);float speed=0;};
inline vec3 Tangent(vec3 n) {
    return normalize(cross(n,std::abs(n.z)<.85f?vec3(0,0,1):vec3(1,0,0)));
}
template<class Trace> Impact Step(Body& b,float elapsed,vec3 gravity,Trace trace)
{
    Impact impact;
    if(!b.live || !std::isfinite(elapsed) || elapsed<=0)return impact;
    b.age+=elapsed;
    if(b.age>7){b.live=false;return impact;}
    if(b.sleeping)return impact;
    constexpr float dt=1.f/120.f;
    b.carry+=std::min(elapsed,.1f);
    int steps=0;
    while(b.carry+1e-7f>=dt && steps++<12) {
        b.carry=std::max(0.f,b.carry-dt);
        b.velocity+=gravity*dt;
        float left=dt;
        for(int contact=0;contact<2 && left>1e-5f;++contact) {
            vec3 end=b.position+b.velocity*left;
            Hit hit=trace(b.position,end,b.radius);
            if(!hit.blocked){b.position=end;break;}
            if(!hit.stable){b.live=false;return impact;} // never float on a departed lift
            float fraction=std::clamp(hit.fraction,0.f,1.f);
            vec3 normal=hit.normal;
            float length2=dot(normal,normal);
            if(!std::isfinite(length2)||length2<.01f){b.live=false;return impact;}
            normal/=std::sqrt(length2);
            b.position+=(end-b.position)*fraction+normal*.06f;
            float inward=dot(b.velocity,normal),speed=std::max(-inward,0.f);
            ++b.contacts;
            if(speed>impact.speed)impact={true,b.position-normal*b.radius,normal,speed};
            // Soft tissue loses most normal energy; retain a short tangential
            // skid/roll. Resolve only the remaining time after the collision.
            if(inward<0)b.velocity=(b.velocity-normal*inward)*.68f-normal*inward*.22f;
            b.spin*=.55f;
            if(normal.z>.6f && (dot(b.velocity,b.velocity)<45*45 || b.contacts>=7)) {
                b.sleeping=true;b.restNormal=normal;b.velocity=b.spin=vec3(0);break;
            }
            left*=1-fraction;
        }
        if(b.sleeping)break;
        b.angles+=b.spin*dt;
    }
    return impact;
}
}
