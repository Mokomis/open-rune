#include "RuneTacticsPolicy.h"
#include <cassert>
#include <iostream>
using namespace RuneTactics;
int main() {
    Observation o;o.visible=o.facing=true;o.visibleFor=1;o.distance=85;o.reach=110;o.contact=90;
    auto normal=ForDifficulty(1);
    assert(Decide(o,normal,0)==Choice::Attack);
    o.visible=false;assert(Decide(o,normal,0)==Choice::Wait);o.visible=true;
    o.visibleFor=.1f;assert(Decide(o,normal,0)==Choice::Wait);o.visibleFor=1;
    o.distance=160;assert(Decide(o,normal,0)==Choice::Approach);o.distance=85;
    o.threat=o.shield=true;o.threatFor=.1f;
    assert(Decide(o,normal,0)!=Choice::Block); // No frame-perfect defense.
    o.threatFor=.4f;assert(Decide(o,normal,0)==Choice::Block);
    assert(Decide(o,normal,.99f)!=Choice::Block); // A failed roll stays a mistake.
    o.shield=false;assert(Decide(o,normal,0)==Choice::Retreat);o.threat=false;
    o.kick=true;o.distance=55;assert(Decide(o,normal,1)==Choice::Kick);
    o.kick=false;assert(Decide(o,normal,1)==Choice::Retreat);
    o.distance=85;o.bash=o.targetBlocking=true;o.blockingFor=.1f;
    assert(Decide(o,normal,1)!=Choice::Bash);
    o.blockingFor=.4f;assert(Decide(o,normal,1)==Choice::Bash);
    o.bash=false;assert(Decide(o,normal,1)==Choice::Flank);
    o.targetBlocking=false;o.otherAttackers=2;assert(Decide(o,normal,1)==Choice::Flank);
    o.otherAttackers=0;o.crowded=true;assert(Decide(o,normal,1)==Choice::Flank);
    o={};o.visible=o.facing=true;o.visibleFor=1;o.distance=85;o.reach=110;
    o.wounded=o.repositionReady=true;
    assert(Decide(o,normal,1)==Choice::Flank);
    o.repositionReady=false;assert(Decide(o,normal,1)==Choice::Attack);
    o.repositionReady=o.counterOpening=true;assert(Decide(o,normal,1)==Choice::Attack);
    o.threat=true;o.threatFor=.1f;assert(Decide(o,normal,1)==Choice::Wait); // Counter opportunities do not bypass reactions.
    o.threat=false;o.otherAttackers=2;assert(Decide(o,normal,1)==Choice::Flank); // Nor group limits.
    for(int difficulty=0;difficulty<=3;++difficulty) {
        auto t=ForDifficulty(difficulty);
        assert(t.reaction>=.18f && t.recovery>=.35f && t.blockChance<1);
        o={};o.visible=o.facing=o.threat=o.shield=true;o.visibleFor=2;o.distance=80;o.reach=110;
        o.threatFor=t.reaction-.01f;assert(Decide(o,t,0)!=Choice::Block);
        o.threatFor=t.reaction+.01f;assert(Decide(o,t,0)==Choice::Block);
        o.visible=false;assert(Decide(o,t,0)==Choice::Wait);
    }
    std::cout<<"TACTICS_POLICY_OK visibility, reaction, mistakes, range, specials, crowding, difficulty\n";
}
