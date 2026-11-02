#pragma once
#include "Math/rotator.h"
#include "Math/quaternion.h"
#include <algorithm>
#include <cmath>

namespace RuneAnimation {
// Engine-thread option, set once at launch. Off retains the old sampler for A/B checks.
inline bool Interpolate = true;
}

struct RuneJointPose
{
    vec3 Position = vec3(0.0f);
    Rotator Rotation = {0,0,0};
    vec3 Scale = vec3(1.0f);
};

// Rune's local rotation convention, before the root joint's separate transpose.
// Existing conversion moved from USkelModel.cpp; UE Viewer reference (MIT),
// copyright 2022 Konstantin Nosov. See Docs/RuneModel-THIRD-PARTY.txt.
inline mat4 RunePoseRotation(const Rotator& r)
{
    constexpr float unit=3.14159265358979323846f/65536.0f;
    float sz=std::sin(r.Yaw*unit),cz=std::cos(r.Yaw*unit);
    float sy=std::sin(-r.Pitch*unit),cy=std::cos(-r.Pitch*unit);
    float sx=std::sin(-r.Roll*unit),cx=std::cos(-r.Roll*unit);
    return mat4::transpose(mat4::quaternion(cx*sy*sz-sx*cy*cz,-cx*sy*cz-sx*cy*sz,
        sx*sy*cz-cx*cy*sz,cx*cy*cz+sx*sy*sz));
}

inline Rotator RunePoseEuler(const mat4& m)
{
    constexpr float units=32768.0f/3.14159265358979323846f;
    // Columns are the rotated basis vectors: Rz(yaw) Ry(-pitch) Rx(-roll).
    float pitch=std::asin(std::clamp(m[2],-1.0f,1.0f));
    float yaw,roll;
    if(std::abs(std::cos(pitch))>1e-5f) { yaw=std::atan2(m[1],m[0]);roll=std::atan2(-m[6],m[10]); }
    else { yaw=std::atan2(-m[4],m[5]);roll=0; }
    return { (int)std::lround(pitch*units),(int)std::lround(yaw*units),(int)std::lround(roll*units) };
}

inline RuneJointPose BlendRunePose(const RuneJointPose& a,const RuneJointPose& b,float t)
{
    if(t<=0) return a;
    if(t>=1) return b;
    RuneJointPose p;
    p.Position=mix(a.Position,b.Position,t); p.Scale=mix(a.Scale,b.Scale,t);
    if(a.Rotation==b.Rotation) p.Rotation=a.Rotation;
    else {
        auto q0=normalize(quaternion::rotation_matrix(RunePoseRotation(a.Rotation)));
        auto q1=normalize(quaternion::rotation_matrix(RunePoseRotation(b.Rotation)));
        auto q=normalize(slerp(q0,q1,t));
        p.Rotation=RunePoseEuler(mat4::quaternion(q.x,q.y,q.z,q.w));
    }
    return p;
}
