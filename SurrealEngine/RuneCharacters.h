#pragma once
#include "../Network/include/openrune/CharacterRoster.h"
#include <atomic>
class Engine;
namespace RuneCharacters {
// Packed character/skin pair makes a UI update indivisible on the game thread.
inline std::atomic<int> Multiplayer{0}, Campaign{-1}; // -1: original story character
openrune::CharacterChoice Selected(bool campaign=false);
void Probe(Engine&);
}
