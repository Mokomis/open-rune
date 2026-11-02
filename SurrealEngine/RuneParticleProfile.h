#pragma once
#include <array>
#include <chrono>

namespace RuneParticleProfile
{
enum Kind { Tick, Bounds, Draw };
inline bool Enabled = false;
inline std::array<double, 3> Milliseconds = {};
class Scope
{
public:
    explicit Scope(Kind kind) : kind(kind), active(Enabled)
    {
        if (active) start = std::chrono::steady_clock::now();
    }
    ~Scope()
    {
        if (active) Milliseconds[kind] += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }
private:
    Kind kind;
    bool active;
    std::chrono::steady_clock::time_point start;
};
}
