#pragma once
#include <cstdint>
class USkelModel;class UPawn;class UActor;class Engine;
namespace RuneShieldTaunt {
inline constexpr const char* Clip="OpenRune_ShieldTaunt";
inline constexpr float Duration=2.2f;
inline constexpr float Beats[3]={.55f,1.05f,1.55f};
struct State {bool bot=false,active=false;float cooldown=9,health=0;unsigned sounded=0,started=0;};
// Append an in-memory, interpolated clip; original sequences and packages stay intact.
void Install(USkelModel&);
bool Eligible(UPawn*);
bool Controls(UActor*);
bool Start(UPawn*);
void Tick(UPawn*,float);
bool AnimationFrames(UActor*,float from,float to);
void Probe(Engine&);
void Demo(Engine&,int frame);
}
