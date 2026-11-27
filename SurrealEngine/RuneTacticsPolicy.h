#pragma once
#include <algorithm>
#include "RuneTactics.h"

// Pure combat decisions. Inputs are observations, never player input/buttons.
namespace RuneTactics {
struct Temperament {
    float reaction, recovery, blockChance;
    int attackers;
};
inline Temperament ForDifficulty(int difficulty) {
    constexpr Temperament values[] = {{.42f,.70f,.45f,1}, {.30f,.52f,.65f,2}, {.22f,.40f,.80f,2}, {.18f,.35f,.85f,2}};
    return values[std::clamp(difficulty,0,3)];
}
struct Observation {
    bool finisher=false;
    bool visible=false, facing=false, threat=false, shield=false;
    bool kick=false, bash=false, targetBlocking=false, crowded=false;
    bool wounded=false, repositionReady=false, counterOpening=false;
    float visibleFor=0, threatFor=0, blockingFor=0, distance=0, reach=100, contact=80;
    int otherAttackers=0;
};
inline Choice Decide(const Observation& o, const Temperament& t, float defenseRoll) {
    if(!o.visible || o.visibleFor<t.reaction)return Choice::Wait;
    if(o.threat && o.threatFor<t.reaction)return Choice::Wait;
    if(o.threat && o.threatFor>=t.reaction && defenseRoll<t.blockChance) {
        if(o.shield && o.facing)return Choice::Block;
        return Choice::Retreat;
    }
    if(o.otherAttackers>=t.attackers || o.crowded)return Choice::Flank;
    if(o.distance>o.reach*.90f)return Choice::Approach;
    if(!o.facing)return Choice::Wait;
    if(o.targetBlocking && o.blockingFor<t.reaction)return Choice::Wait;
    if(o.finisher && !o.targetBlocking)return Choice::Attack;
    if(o.distance<=o.contact && o.blockingFor>=t.reaction && o.targetBlocking && o.bash)return Choice::Bash;
    if(o.distance<o.reach*.60f && o.distance<=o.contact && o.kick && !o.targetBlocking)return Choice::Kick;
    if(o.distance<o.reach*.52f)return Choice::Retreat;
    // Do not repeatedly swing into a held shield. Change the angle instead.
    if(o.targetBlocking && o.blockingFor>=t.reaction)return Choice::Flank;
    // An observed recovery is an opportunity, but never extends attack range
    // or cancels the attacker's own recovery. Wounded fighters occasionally
    // change the angle; a cooldown prevents permanent retreat/kiting.
    if(o.wounded && o.repositionReady && !o.counterOpening)return Choice::Flank;
    return Choice::Attack;
}
}
