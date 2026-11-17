#pragma once
#include "Math/vec.h"
#include <algorithm>
#include <cmath>

namespace RuneIK {
inline bool Finite(vec3 p) {return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
inline vec3 Unit(vec3 p,vec3 fallback) {float n=dot(p,p);return n>1e-12f&&std::isfinite(n)?p/std::sqrt(n):fallback;}
struct TwoBoneResult {vec3 joint,end;float error=0;bool valid=false;};
// Analytic two-bone IK. Only rotations are needed to apply the result: both
// authored segment lengths are preserved, including unreachable targets.
inline TwoBoneResult TwoBone(vec3 root,vec3 joint,vec3 end,vec3 target,vec3 pole)
{
    TwoBoneResult out{joint,end};
    if(!Finite(root)||!Finite(joint)||!Finite(end)||!Finite(target)||!Finite(pole))return out;
    const float a=length(joint-root),b=length(end-joint);
    if(a<1e-5f||b<1e-5f)return out;
    vec3 direction=Unit(target-root,Unit(end-root,vec3(1,0,0)));
    const double requested=length(target-root),epsilon=std::min(std::max(double(a)+b,1.0)*1e-6,double(std::min(a,b))*.1);
    const double reach=std::clamp(requested,std::abs(double(a)-b)+epsilon,double(a)+b-epsilon);
    vec3 bend=pole-root; bend-=direction*dot(bend,direction);
    if(dot(bend,bend)<1e-10f){bend=joint-root;bend-=direction*dot(bend,direction);}
    if(dot(bend,bend)<1e-10f){vec3 axis=std::abs(direction.z)<.8f?vec3(0,0,1):vec3(0,1,0);bend=cross(direction,axis);}
    bend=Unit(bend,vec3(0,1,0));
    // Double intermediates avoid cancellation for very unequal limb lengths.
    const double along=(reach*reach+double(a)*a-double(b)*b)/(2.0*reach);
    const double height=std::sqrt(std::max(0.0,double(a)*a-along*along));
    out.joint=root+direction*float(along)+bend*float(height);
    out.end=root+direction*float(reach);
    out.error=length(out.end-target);out.valid=Finite(out.joint)&&Finite(out.end);
    return out;
}
}
