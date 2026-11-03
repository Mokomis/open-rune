#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>

namespace RuneShadows {
// Windows and Android expose eight slots; Mac retains its legacy layout.
#if defined(_WIN32) || defined(__ANDROID__)
inline constexpr int Capacity=8,Columns=4,Rows=2;
#else
inline constexpr int Capacity=3,Columns=3,Rows=1;
#endif
inline constexpr int TileSize=1024;
inline std::atomic<int> RequestedBudget{0}; // zero: legacy world-feature setting
inline constexpr int ChoiceBudget(int choice){constexpr int counts[]={1,3,4,6,8};return counts[std::clamp(choice,0,4)];}
inline int Budget(int worldFeatures){int requested=RequestedBudget.load();return std::clamp(requested>0?requested:((worldFeatures&4096)?3:1),1,Capacity);}

template<int Count> struct SlotLayout {
    static_assert(Count>0&&Count<=8);
    static constexpr int Bits=Count<=3?2:4;
    static constexpr uint32_t Mask=(1u<<Bits)-1;
    static constexpr uint32_t Encode(int light,int slot){
        return light<0||light>=8||slot<0||slot>=Count?0u:(uint32_t(slot)+1u)<<(light*Bits);
    }
    static constexpr int Decode(uint32_t packed,int light){return light<0||light>=8?-1:int((packed>>(light*Bits))&Mask)-1;}
};
using Slots=SlotLayout<Capacity>;
static_assert(Columns*Rows==Capacity);
}
