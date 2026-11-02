#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Original, deliberately modest luminance-derived material estimate. This is
// not artist-authored geometry: painted lighting can also become shallow relief.
namespace MaterialRelief
{
inline std::vector<uint8_t> Pack(const std::vector<float>& input, int width, int height, float strength = 1.0f)
{
    std::vector<uint8_t> result((size_t)width * height * 4);
    auto sample = [&](int x,int y) { return input[((y+height)%height)*width+(x+width)%width]; };
    for (int y=0;y<height;y++) for(int x=0;x<width;x++)
    {
        // A wrapped 3x3 blur limits speckle and keeps tiling seams continuous.
        auto smooth = [&](int u,int v) {
            float sum=0;
            for(int j=-1;j<=1;j++) for(int i=-1;i<=1;i++) sum += sample(u+i,v+j);
            return sum/9.0f;
        };
        float dx=(smooth(x+1,y)-smooth(x-1,y))*strength;
        float dy=(smooth(x,y+1)-smooth(x,y-1))*strength;
        float inv=1.0f/std::sqrt(dx*dx+dy*dy+1.0f);
        size_t p=((size_t)y*width+x)*4;
        result[p]=(uint8_t)std::lround(127.5f*(1.0f-dx*inv));
        result[p+1]=(uint8_t)std::lround(127.5f*(1.0f-dy*inv));
        result[p+2]=(uint8_t)std::lround(127.5f*(1.0f+inv));
        result[p+3]=(uint8_t)std::lround(std::clamp(smooth(x,y),0.0f,1.0f)*255.0f);
    }
    return result;
}
}
