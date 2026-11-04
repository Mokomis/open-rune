#pragma once

#include <atomic>
#include <cstdint>

// UI requests are consumed only on the engine thread, between frames.
// These are session diagnostics, separate from the game's saved configuration.
namespace VisualComparison
{
inline std::atomic<int> RequestedMode{-1};
inline std::atomic<int> AppliedMode{-1};
// 0 = native, 1 = GSR 75%, 2 = GSR 50%, 3 = GSR 25%.
inline std::atomic<int> RequestedUpscale{-1};
inline std::atomic<int> AppliedUpscale{-1};
// Coherent rendered-frame snapshot: scene W/H and output W/H, 15 bits each;
// bit 60 identifies the GSR shader. Published after frame submission.
inline std::atomic<uint64_t> ScalingSnapshot{0};
// bits 0-1: sharpening (0/1/2/3), bit 2: 16x filtering, bits 3-4: shadow lift (0/1/2/3).
inline std::atomic<int> RequestedClarity{-1};
inline std::atomic<int> AppliedClarity{-1};
// Bits 16/17: GGX material lighting / local environment probes (Remastered only).
// Low bits: environmental audio=1, relief=2, parallax=4, HRTF=8.
// Bits 16/17: GGX material lighting / local environment probes (Remastered only).
// Bits 4-5 detail and 6-7 depth: 1=Low, 2=Medium, 3=High; 0=legacy Medium.
inline std::atomic<int> RequestedWorldFeatures{-1};
inline std::atomic<int> AppliedWorldFeatures{-1};
inline std::atomic<float> EngineFps{0.0f};
inline std::atomic<int> RequestedTexturePack{-1};
inline std::atomic<int> AppliedTexturePack{0};
inline std::atomic<int> TexturePackLoaded{0};
inline std::atomic<uint64_t> TexturePackBytes{0};
}
