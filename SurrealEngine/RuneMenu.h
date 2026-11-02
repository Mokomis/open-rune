#pragma once
#include <atomic>
#include <mutex>
#include <string>

// The Android UI never touches live actors or renderer objects. Requests cross
// the frame boundary; metadata is published under its own small lock.
namespace RuneMenu {
inline std::atomic<bool> Open{false};
// The native Android menu fully covers the 3D view. HUD/control editors do not.
inline std::atomic<bool> Covered{false};
inline std::atomic<bool> PlayerDead{false};
inline std::atomic<int> Antialias{-1}, TargetFps{-1}, Pacing{-1};
inline std::atomic<int> Master{100}, Music{-1}, Effects{-1};
inline std::atomic<int> ActualMusic{100}, ActualEffects{100};
inline std::atomic<int> Slot{9}, Difficulty{1};
inline std::mutex MetadataMutex;
inline std::string Metadata;
}
