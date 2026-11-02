#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>

namespace RuneView {
inline std::atomic<int> FieldOfView{90};
inline std::atomic<bool> AutomaticAspect{true};
inline std::atomic<float> AppliedFov{90.0f};

// Authored FOVs are horizontal at 4:3. Scale tangent space so zoom retains
// its magnification, and widen horizontally without cropping vertical vision.
inline float Adjust(float authored, float defaultFov, float aspect, int selected, bool automatic, bool cinematic)
{
    constexpr float toRadians=0.01745329251994329577f;
    if(!std::isfinite(authored)) authored=90.0f;
    authored=std::clamp(authored,1.0f,170.0f);
    if(cinematic) return authored;
    if(!std::isfinite(defaultFov) || defaultFov<1.0f || defaultFov>170.0f) defaultFov=90.0f;
    if(!std::isfinite(aspect) || aspect<=0.0f) aspect=4.0f/3.0f;
    float tangent=std::tan(authored*toRadians*.5f);
    tangent*=std::tan(std::clamp(selected,70,120)*toRadians*.5f)/std::tan(defaultFov*toRadians*.5f);
    if(automatic) tangent*=aspect/(4.0f/3.0f);
    return std::clamp(2.0f*std::atan(tangent)/toRadians,1.0f,170.0f);
}
}
