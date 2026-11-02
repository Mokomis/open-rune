#pragma once
#include <cstdint>
#include <vector>
#include <limits>
class Engine;
namespace RuneCharacterPreview {
inline constexpr int Width=192,Height=224;
// UI-facing calls only exchange copied pixels and bounded requests.
int Poll(int character,int skin,std::vector<uint32_t>& pixels,bool expanded=false,float yaw=std::numeric_limits<float>::quiet_NaN());
void Rotate(int character,int skin,float yaw);
bool Active(); // Cheap request-state check for covered-menu preview wakeups.
void Tick(Engine& game); // Package/model access stays on the engine thread.
void Probe(Engine& game);
}
