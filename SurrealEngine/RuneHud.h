#pragma once
#include <array>
#include <atomic>
#include <mutex>
#include <string>

// UI reads a copy only. Live UObject pointers and draw scopes stay on the engine thread.
namespace RuneHud {
#ifdef _WIN32
inline std::atomic<bool> Enabled{true};
#else
inline std::atomic<bool> Enabled{false};
#endif
inline std::mutex Mutex;
inline std::array<float, 12> Values{}; // mask, five current/max pairs, engine FPS
inline std::string Messages, Score;
inline thread_local void* LocalHud = nullptr;
inline thread_local unsigned Mask = 0;
inline thread_local bool CaptureMessages = false, CaptureScore = false;
inline thread_local std::string FrameMessages, FrameScore;
struct MessageScope {
    bool previous, previousScore;
    explicit MessageScope(bool capture, bool score) : previous(CaptureMessages), previousScore(CaptureScore) { CaptureMessages |= capture; CaptureScore |= score; }
    ~MessageScope() { CaptureMessages = previous; CaptureScore = previousScore; }
};
inline bool Capture(const std::string& text) {
    if (CaptureScore) { if (FrameScore.size()<256) { if (!FrameScore.empty()) FrameScore += " · "; FrameScore += text.substr(0,256); } return true; }
    if (!CaptureMessages) return false;
    if (!text.empty() && FrameMessages.size() < 4096) {
        if (!FrameMessages.empty()) FrameMessages += '\n';
        FrameMessages += text.substr(0, 4096 - FrameMessages.size());
    }
    return true;
}
}
