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
class UNavigationPoint;
class ULevel;
class UActor;
namespace RuneBot {
struct Decision { openrune::net::Input input; std::optional<openrune::net::ActionKind> action; };
// A normal authenticated client: no authority, damage, teleport or input-reading privileges.
class Brain {
public:
    explicit Brain(uint32_t seed, int difficulty=1):rng(seed),difficulty(std::clamp(difficulty,0,3)){}
    Decision think(Engine&,RuneNetworkReplica&,const openrune::net::World&,const openrune::net::MatchStatus&,openrune::net::EntityRef,uint64_t);
    bool debug=false,limbsScore=false;
    uint64_t maxThinkUs=0;
    unsigned decisions=0,attacks=0,defenses=0,specials=0,objectives=0,routes=0,supportMoves=0,powers=0,bashes=0,crossBlocks=0,leaps=0,charges=0;
private:
    std::mt19937 rng;
    int difficulty=1,side=1,lastHealth=0;
    uint32_t life=0;
    uint64_t nextThink=0,readyAt=0,guardUntil=0,kickAt=0,bashAt=0,useAt=0,seenAt=0,lastSeen=0,threatAt=0,blockedAt=0,routeAt=0,patrolAt=0,progressAt=0,debugAt=0,jumpAt=0,searchUntil=0,equipmentUntil=0,woundedAt=0,leapAt=0,jumpUntil=0,unreachableAt=0,supportUntil=0,guardAt=0,powerAt=0,escapeUntil=0,trafficUntil=0,trafficWaitUntil=0;
    uint16_t leapYaw=0;
    float defenseRoll=1;
    bool wasThreat=false,partialRoute=false,teamNavigation=false;
    openrune::net::EntityRef target{},trafficBlocker{};
    vec3 remembered{},progress{},waypoint{},jumpGoal{},supportGoal{},escapeGoal{},trafficGoal{};
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
