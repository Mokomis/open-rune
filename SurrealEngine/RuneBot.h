#pragma once
#ifdef OPENRUNE_ENABLE_NETWORK
#include "RuneNetwork.h"
#include "Math/vec.h"
#include <optional>
#include <algorithm>
#include <vector>
#include <random>
class Engine;
class UPlayerPawn;
class UPawn;
class UNavigationPoint;
class ULevel;
class UActor;
class UBrush;
namespace RuneBot {
struct Decision { openrune::net::Input input; std::optional<openrune::net::ActionKind> action; };
// --rune-bot-brain=classic keeps the pre-weapon-awareness combat rules, for A/B measurement.
bool ClassicRulesRequested();
// A weapon's measured melee reach from the wielder's centre and its swing timing (seconds).
struct WeaponProfile { float reach=0,windup=0,duration=0; bool valid=false; };
WeaponProfile ProfileWeapon(UPawn*,UActor* weapon);
// A normal authenticated client: no authority, damage, teleport or input-reading privileges.
class Brain {
public:
    explicit Brain(uint32_t seed, int difficulty=1):rng(seed),difficulty(std::clamp(difficulty,0,3)){}
    Decision think(Engine&,RuneNetworkReplica&,const openrune::net::World&,const openrune::net::MatchStatus&,openrune::net::EntityRef,uint64_t);
    bool debug=false,limbsScore=false;
    bool classic=ClassicRulesRequested();
    uint64_t maxThinkUs=0;
    unsigned punishes=0,legCounters=0,throws=0,spacing=0,guardPunishes=0,guardEscapes=0,thirdParty=0,woundedTargets=0,targetOverrides=0,earlySwings=0,swingHolds=0;
    // Keep match-long usage counters when the brain is rebuilt for a new map/round.
    void carryCounters(const Brain& o){
        punishes+=o.punishes;legCounters+=o.legCounters;throws+=o.throws;spacing+=o.spacing;guardPunishes+=o.guardPunishes;guardEscapes+=o.guardEscapes;thirdParty+=o.thirdParty;woundedTargets+=o.woundedTargets;targetOverrides+=o.targetOverrides;earlySwings+=o.earlySwings;swingHolds+=o.swingHolds;
        decisions+=o.decisions;attacks+=o.attacks;defenses+=o.defenses;specials+=o.specials;objectives+=o.objectives;routes+=o.routes;supportMoves+=o.supportMoves;
        powers+=o.powers;bashes+=o.bashes;crossBlocks+=o.crossBlocks;leaps+=o.leaps;charges+=o.charges;maxThinkUs=std::max(maxThinkUs,o.maxThinkUs);
    }
    unsigned decisions=0,attacks=0,defenses=0,specials=0,objectives=0,routes=0,supportMoves=0,powers=0,bashes=0,crossBlocks=0,leaps=0,charges=0;
private:
    friend struct TraversalFixture;
    std::mt19937 rng;
    int difficulty=1,side=1,lastHealth=0;
    uint32_t life=0;
    uint64_t nextThink=0,readyAt=0,guardUntil=0,kickAt=0,bashAt=0,useAt=0,seenAt=0,lastSeen=0,threatAt=0,blockedAt=0,routeAt=0,patrolAt=0,progressAt=0,debugAt=0,jumpAt=0,searchUntil=0,equipmentUntil=0,woundedAt=0,leapAt=0,jumpUntil=0,unreachableAt=0,supportUntil=0,guardAt=0,powerAt=0,escapeUntil=0,trafficUntil=0,trafficWaitUntil=0,corpseAt=0,carrySince=0;
    uint64_t spiderAt=0,spiderRetry=0;vec3 spiderExit{},spiderNormal{};
    uint64_t ledgeSince=0,ledgeJumpAt=0;
    vec3 ledgePosition{};
    uint16_t leapYaw=0;
    uint64_t slamAt=0,slamCooldown=0;
    uint64_t foeRecoveredAt=0,throwAt=0,spacingSince=0,swingWindupUntil=0;
    bool guardSawSwing=false;
    uint8_t slamStage=0;
    float defenseRoll=1;
    bool wasThreat=false,partialRoute=false;
    openrune::net::EntityRef target{},trafficBlocker{},carriedBody{};
    openrune::net::EntityRef supplyTarget{};
    unsigned supplyTries=0;
    vec3 remembered{},progress{},waypoint{},jumpGoal{},supportGoal{},escapeGoal{},trafficGoal{};
    struct LiftStop {vec3 center{};std::vector<vec3> landings;};
    struct LiftPlan {vec3 offset{};UBrush* actor=nullptr;std::vector<LiftStop> stops;UActor* trigger=nullptr;bool use=false,automatic=false;float cycleSeconds=0;};
    std::vector<LiftPlan> lifts;
    ULevel* liftLevel=nullptr;float liftRadius=0,liftHeight=0;
    int liftActive=-1,liftFrom=0,liftTo=0,liftPhase=0;
    uint64_t liftDeadline=0,liftUseAt=0,liftRetryAt=0,liftScanAt=0,liftMovedAt=0,liftWakeAt=0,liftApproachAt=0;
    float liftLastZ=0,liftApproachDistance=0;
    vec3 liftEntry{},liftExit{},liftGoal{};
    unsigned liftTrips=0,liftWaits=0;
    void discoverLifts(Engine&,UPlayerPawn*,uint64_t now=0);
    std::optional<Decision> liftStep(Engine&,UPlayerPawn*,const openrune::net::Entity&,vec3,uint64_t);
    struct Link { int to; float cost; };
    std::vector<vec3> navPoints;
    std::vector<float> navRadius,navHeight;
    std::vector<std::vector<Link>> navLinks;
    std::vector<vec3> route;
    size_t routeIndex=0;
    struct ScoringOpening { UActor* zone; vec3 inside,outward,approach; };
    ULevel* goalLevel=nullptr;
    std::vector<ScoringOpening> scoringOpenings;
    struct RejectedGoal{vec3 point;uint64_t until;};
    std::vector<RejectedGoal> rejected;
    std::vector<vec3> searchedPatrols;
    bool rejectedAt(vec3,uint64_t);
    void rejectGoal(vec3,uint64_t);
    Decision intent;
    float random(){return std::generate_canonical<float,24>(rng);}
    void avoidTeammates(UPlayerPawn*,RuneNetworkReplica&,const openrune::net::World&,const openrune::net::MatchStatus&,const openrune::net::Entity&,uint64_t,Decision&);
    bool safe(UPlayerPawn*,vec3,vec3);
    bool walkableLine(UPlayerPawn*,vec3,vec3);
    std::vector<vec3> localPath(UPlayerPawn*,vec3,vec3,float,float);
    vec3 navigate(Engine&,UPlayerPawn*,vec3,uint64_t);
};
}
#endif
