#pragma once

#include "Math/mat.h"
#include <cmath>

// Conservative receiver test for the same zero-to-w clip volume used by
// movingShadow. The shader evaluates receiver positions at the centroid,
// keeping partially covered MSAA pixels inside the primitive. Expand every axis beyond its maximum 0.37 world-unit normal
// offset. A straddling surface always keeps the original per-pixel test.
inline bool ShadowReceiverMayIntersect(const mat4& projection, const vec3& minimum, const vec3& maximum)
{
    for(int axis=0;axis<3;++axis)
        if(!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) || minimum[axis]>maximum[axis]) return true;
    for(int i=0;i<16;++i) if(!std::isfinite(projection[i])) return true;
    for(int plane=0;plane<6;++plane)
    {
        // Column-major rows: w+x, w-x, w+y, w-y, z, w-z.
        const int row=plane<4 ? plane/2 : 2;
        const float sign=(plane&1) ? -1.0f : 1.0f;
        float coefficients[4];
        for(int c=0;c<4;++c)
            coefficients[c]=(plane==4 ? 0.0f : projection[c*4+3])+sign*projection[c*4+row];
        double upper=coefficients[3], magnitude=std::abs(double(coefficients[3]));
        for(int axis=0;axis<3;++axis)
        {
            double extent=coefficients[axis]>=0 ? double(maximum[axis])+0.5 : double(minimum[axis])-0.5;
            double term=coefficients[axis]*extent;
            upper+=term; magnitude+=std::abs(term);
        }
        // Retain near-boundary receivers, accounting for shader float rounding.
        if(upper < -1e-5*(1.0+magnitude)) return false;
    }
    return true;
}
