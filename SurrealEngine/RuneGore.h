#pragma once
#include <atomic>
#include <cstdint>
#include "Math/mat.h"

class UActor;class UMesh;class VisibleFrame;struct TextureInfo;
namespace RuneGore {
inline std::atomic<bool> Enabled{true};
void Reset();
void Tick(float elapsed);
void Draw(VisibleFrame* frame);
void BleedMark(vec3 source,vec3 color);
// Draw-local snapshot: classification cannot change between mesh triangles.
// Engine-thread override used only by isolated frozen-frame comparisons.
inline int MaterialBatchOverride=-1;
struct MaterialState {bool enabled=false,gib=false,red=false;};
MaterialState ActorMaterial(UActor* actor);
void Material(TextureInfo& info,const MaterialState& state);
void Material(TextureInfo& info,UActor* actor);
mat4 MeshTransform(UActor* actor,UMesh* mesh,const mat4& original);
struct Statistics {
    uint64_t bursts=0,fragments=0,impacts=0,stains=0,wounds=0,settles=0;
    int active=0,sleeping=0,drawn=0,traces=0;
    double updateUs=0;
};
Statistics Stats();
}
