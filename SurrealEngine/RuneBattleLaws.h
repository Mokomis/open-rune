#pragma once
#include <atomic>
#include <array>
#include <cstdint>

// A match owns one immutable ruleset. Campaign preferences never overwrite it.
namespace RuneBattleLaws {
enum Law : uint32_t { Kick=1u<<0, Bash=1u<<1, Dual=1u<<2, Cross=1u<<3, Leap=1u<<4, Rush=1u<<5, Slam=1u<<6, Corpse=1u<<7, Props=1u<<8, Knockdown=1u<<9, Tactics=1u<<10, Normal=1u<<11 };
inline constexpr std::array<const char*,12> Keys={"kick","bash","dual-wield","crossed-block","giant-leap","shield-rush","aerial-slam","corpses","props","knockdowns","tactics","normal-moves"};
inline constexpr uint32_t All=(1u<<12)-1;
inline std::atomic<uint32_t> Campaign{All & ~Leap}, Multiplayer{All};
inline std::atomic<bool> InMatch{false};
inline uint32_t Effective(uint32_t mask){mask&=All;if(!(mask&Dual))mask&=~Cross;return mask;}
inline uint32_t Mask(){return Effective(InMatch?Multiplayer.load():Campaign.load());}
inline bool Allowed(Law law){return (Mask()&law)!=0;}
inline bool Enabled(Law law,bool legacy=true){return Allowed(law)&&(InMatch||legacy);}
inline bool NormalMoves(bool legacy){return InMatch?Allowed(Normal):(Allowed(Normal)&&legacy);}
struct MatchScope {
 bool previous;uint32_t mask;
 explicit MatchScope(uint32_t selected):previous(InMatch.exchange(true)),mask(Multiplayer.exchange(selected&All)){}
 ~MatchScope(){Multiplayer=mask;InMatch=previous;}
};
struct Entry {Law bit;const char* name;const char* description;};
inline constexpr std::array<Entry,12> Entries={{
{Kick,"Kick","F / right-stick click / Kick button. Faster close-range kick; raised shields absorb most force."},
{Bash,"Shield bash","Double-tap Block with a shield. Shield swipe, brief stun and head recoil on a clean hit."},
{Dual,"Dual wield","Pick up a second compatible one-handed weapon with Use; Attack alternates weapons. Shields stay stowed."},
{Cross,"Crossed-weapon block","Hold Block while dual wielding. Weaker protection than a shield; requires Dual wield."},
{Leap,"Giant leap","Double-tap Jump in a direction. Multiplayer only; always disabled for everyone in the campaign."},
{Rush,"Shield rush","Hold Block and press Attack with a shield. Stow the weapon and charge forward; knockdowns follow the physical-knockdown law."},
{Slam,"Two-handed aerial slam","Jump, tap Jump again while airborne, then Attack with a two-handed weapon. Flip upward and slam down hard."},
{Corpse,"Corpse pickup & throw","Use near an intact, finished corpse to lift it. Attack or Throw launches it; Use drops it. Eligible humanoids can do this too."},
{Props,"Loose prop interaction","Use lifts safe loose stools, kegs and buckets; Attack or Throw launches them; Use drops them. Kick can shove them. Requires Cinematic or Chaos physics; scripted scenery stays protected."},
{Knockdown,"Physical knockdowns","Strong impacts can knock eligible living fighters into recoverable ragdolls. Shield rush can do this even with Original physics. No extra control; bosses and scripted actors stay protected."},
{Tactics,"Tactical campaign enemies","Eligible campaign humanoids use spacing, defense and permitted special moves. Multiplayer bots always obey the selected laws."},
{Normal,"Special moves without rage","Leap and shield rush need no bloodlust. Uncheck to require bloodlust; their cooldowns and individual checkboxes still apply."},
}};
}
