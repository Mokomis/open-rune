#pragma once

#include "Math/vec.h"
#include <vector>

// Per lightmap, per ordered ray. Only immutable level-BSP results are retained;
// moving-brush visibility is queried on every call. Exact endpoint changes
// invalidate a ray, including translated/rotated faces and moving lights.
class WorldVisibilityCache
{
public:
    void Begin() { cursor = 0; }
    void End() { rays.resize(cursor); }

    template<class WorldTest, class MoverTest>
    bool Trace(const vec3& from, const vec3& to, WorldTest world, MoverTest movers)
    {
        if (cursor == rays.size()) rays.push_back({from, to, world()});
        else if (rays[cursor].from != from || rays[cursor].to != to)
            rays[cursor] = {from, to, world()};
        bool blocked = rays[cursor++].blocked;
        return blocked || movers();
    }

private:
    struct Ray { vec3 from, to; bool blocked; };
    std::vector<Ray> rays;
    std::size_t cursor = 0;
};
