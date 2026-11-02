#pragma once
#include <mutex>

// Android UI publishes one coherent sample; only the engine thread touches actors.
namespace RuneController {
struct Axes { float strafe=0, forward=0, yaw=0, pitch=0; };
inline std::mutex Mutex;
inline Axes State;
inline void Set(Axes state) { std::lock_guard<std::mutex> lock(Mutex); State=state; }
inline Axes Get() { std::lock_guard<std::mutex> lock(Mutex); return State; }
}
