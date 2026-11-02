#pragma once

#include "Math/rotator.h"

// Rune's physPathing uses cubic B-splines or Catmull-Rom curves, selected by
// bSplineThruPoints. These are different from the older UE spline helper.
template<class T> T RunePathCurve(const T& a, const T& b, const T& c, const T& d, float t, bool through)
{
    float t2=t*t, t3=t2*t;
    if (through)
        return (a*(-t3+2*t2-t)+b*(3*t3-5*t2+2)+c*(-3*t3+4*t2+t)+d*(t3-t2))*0.5f;
    float s=1-t;
    return (a*(s*s*s)+b*(3*t3-6*t2+4)+c*(-3*t3+3*t2+3*t+1)+d*t3)*(1.0f/6.0f);
}

inline Rotator RunePathCurve(const Rotator& a, const Rotator& b, const Rotator& c, const Rotator& d, float t, bool through)
{
    if (!through) return RunePathCurve<Rotator>(a,b,c,d,t,false);
    // Rotator products truncate to integers. Preserve physPathing's polynomial
    // grouping: expanding its coefficients changes these intermediate results.
    float t2=t*t, t3=t2*t;
    return ((a*(-1.0f)+b*3.0f-c*3.0f+d)*t3
        +(a*2.0f-b*5.0f+c*4.0f-d)*t2+(a*(-1.0f)+c)*t+b*2.0f)*0.5f;
}

inline int RunePathAngle(int angle, int previous)
{
    // Original helper truncates the previous revolution toward zero, then
    // chooses the nearest equivalent 16-bit angle (ties are left unchanged).
    int result=(previous/65536)*65536+(angle&65535);
    int delta=result-previous;
    if (delta>32768) result-=65536;
    else if (delta<-32768) result+=65536;
    return result;
}

inline Rotator RunePathRotation(Rotator angle, const Rotator& previous)
{
    return {RunePathAngle(angle.Pitch,previous.Pitch),RunePathAngle(angle.Yaw,previous.Yaw),RunePathAngle(angle.Roll,previous.Roll)};
}
