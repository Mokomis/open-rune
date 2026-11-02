#pragma once
#include "openrune/net/Simulation.h"
#include "openrune/net/Prediction.h"
#include "VM/ExpressionValue.h"
#include "RunePose.h"
#include <map>
#include <deque>
#include <string>
class Engine;
class UActor;
class UPlayerPawn;
class UPawn;
class USound;
class USkelModel;

// Engine-thread adapter. It never accepts client positions or invokes game code
// from a socket callback. Peer ownership is assigned by the server, not the wire.
class RuneNetworkAuthority
{
public:
    explicit RuneNetworkAuthority(Engine& game);
    ~RuneNetworkAuthority();
    bool bind(uint64_t peer,UPlayerPawn* pawn,uint32_t life);
    void unbind(uint64_t peer);
    bool input(uint64_t peer,const openrune::net::Input&);
    bool action(uint64_t peer,const openrune::net::AdmittedAction&,uint64_t now);
    bool capture(openrune::net::World&);
    void beginTick(uint64_t tick,uint64_t now=0);
    void recordSound(UActor*,int,USound*,vec3,float,float,float);
    std::vector<openrune::net::SoundCue> drainSounds();
    void synchronizeLives(); // Includes original script-driven Arena respawns.
    void commitContacts(); // After all peers' input/actions, before the next world tick.
    bool deferContact(UPawn* attacker,UPawn* target,bool blocked,UActor* weapon,std::function<void(bool)> apply,bool bash=false,bool charge=false);
    bool deferWeaponCall(UObject*,const NameString&,const ArrayView<ExpressionValue>&);
    size_t pendingContacts() const { return contacts.size(); }
    void prepareCombat(UPawn* attacker,UPawn* target);
    void recordCombat(UPawn* attacker,UPawn* target,bool bash,bool blocked,bool staggered);
    void recordWeapon(UPawn* attacker,UPawn* target,bool blocked);
    void cancelCombat(UPawn* attacker,openrune::net::CombatMove move);
    struct CancelledAction { uint64_t peer=0,id=0; };
    std::vector<CancelledAction> drainCancelledActions();
    const std::vector<openrune::net::CombatOutcome>& pendingCombat() const { return combat; }
    bool releaseCombatThrough(uint64_t id); // Only after every relevant peer accepted each result.
    const std::vector<openrune::net::ActorEvent>& pendingActors() const { return actorEvents; }
    bool releaseActorsThrough(uint64_t id);
    std::vector<openrune::net::ActorEvent> actorBaseline() const;
    uint64_t actorWatermark() const { return nextActorEventId-1; }
    uint64_t combatWatermark() const { return nextCombatId-1; }
    bool combatFaulted() const { return combatFault; }
    openrune::net::EntityRef identity(UActor*) const;
    uint32_t playerLife(uint64_t peer) const;
    std::optional<openrune::net::MovementAck> movementAck(uint64_t peer) const;
private:
    struct Player { UPlayerPawn* pawn; uint32_t life; uint64_t diedAt=0,pendingInput=0,physicsInput=0,lastAction=0,lastActionTime=0,lastJump=0,guardInput=0; uint16_t heldTicks=0; };
    Engine& game;
    std::map<uint64_t,Player> players;
    openrune::net::EntityRegistry registry;
    std::map<uint32_t,UActor*> actors;
    std::map<std::pair<uint32_t,uint8_t>,openrune::net::ActorAppearance> appearances;
    struct PendingCombatAction { uint64_t peer=0,id=0; openrune::net::CombatMove move=openrune::net::CombatMove::Kick; };
    std::map<UPawn*,PendingCombatAction> pendingCombatActions;
    std::vector<openrune::net::SoundCue> sounds;
    std::vector<openrune::net::CombatOutcome> combat;
    std::vector<CancelledAction> cancelledActions;
    std::vector<openrune::net::ActorEvent> actorEvents;
    uint64_t simulationTick=0, nextCombatId=1, nextActorEventId=1;
    struct Defense { uint64_t pressed,received; uint32_t life; bool raised; uint16_t yaw; UActor* shield; UActor* weapon; UActor* offhand; };
    std::map<UPawn*,std::deque<Defense>> defenses;
    struct Contact {
        UPawn* attacker; UPawn* target; UActor* weapon; UActor* shield;
        openrune::net::EntityRef attackerRef,targetRef;
        uint32_t attackerLife,targetLife;
        UActor* guardWeapon; UActor* guardOffhand;
        uint64_t at,tick; bool blocked,bash,charge,crossed; uint16_t targetYaw;
        vec3 from,to;
        std::function<void(bool)> apply;
    };
    std::deque<Contact> contacts;
    uint64_t wallTime=0,contactsCommitted=0,lateBlocks=0,contactsCancelled=0,contactWaitTotal=0,contactWaitMax=0;
    bool committingContact=false;
    void defense(UPawn*,bool raised,uint64_t pressed,uint64_t received,uint16_t yaw);
    bool combatFault=false;
    bool track(UActor*);
    uint32_t life(UPawn*) const;
};

// Explicitly bound, asset-compatible presentation actors. Installation validates
// the entire candidate before touching any actor. No VM events/physics on replicas.
class RuneNetworkReplica
{
public:
    enum class CombatApply { Applied, Superseded, Deferred, Rejected };
    enum class CombatQueue { Ready, Waiting, Resync, Fault };
    bool bind(openrune::net::EntityRef,UActor*);
    UActor* spawn(Engine&,const openrune::net::ActorEvent&);
    bool appearance(Engine&,const openrune::net::ActorEvent&);
    UActor* actor(openrune::net::EntityRef) const;
    // Slots survive character replacement; entity IDs and generations do not.
    UActor* scoredPawn(const openrune::net::PlayerScore& score) const { return actor(score.pawn); }
    bool unbind(openrune::net::EntityRef);
    bool referencesRetired(const openrune::net::Snapshot&) const;
    bool install(const openrune::net::Snapshot&);
    CombatApply applyCombat(const openrune::net::CombatOutcome&);
    bool queueCombat(const openrune::net::CombatOutcome&,uint64_t localNow);
    CombatQueue flushCombat(uint64_t localNow);
    std::vector<openrune::net::CombatOutcome> drainResolvedCombat();
    void present(double alpha);
    void presentAdaptive(const openrune::net::Snapshot&,const openrune::net::Snapshot&,double alpha,
        openrune::net::EntityRef owned,double localAlpha);
    bool anticipate(openrune::net::EntityRef,uint64_t action,openrune::net::ActionKind,const openrune::net::Input&,uint64_t now);
    void resolveAnticipation(const openrune::net::ActionResult&,uint64_t now);
    void presentAnticipation(uint64_t now);
    void clearAnticipation();

    void tickEffects(float dt);
    uint64_t installedId() const { return current.id; }
    uint64_t installedTick() const { return current.tick; }
    uint64_t appliedCombatId() const { return lastCombatId; }
private:
    void presentFrames(double,const openrune::net::Snapshot*,const openrune::net::Snapshot*,double,openrune::net::EntityRef);
    struct Anticipation {
        openrune::net::EntityRef ref,weapon,shield,offhand;
        uint32_t life=0;uint64_t action=0,started=0,snapshot=0,settle=0;
        openrune::net::ActionKind kind=openrune::net::ActionKind::Attack;
        uint16_t sequence=0;float tween=.1f,rate=1,guardStart=0;int health=0;
        const USkelModel* model=nullptr;
        std::vector<RuneJointPose> startPose;
    } anticipation;
    struct HealthOverlay { openrune::net::EntityRef ref; uint32_t life; uint64_t tick; int16_t health; };
    std::map<uint32_t,std::pair<openrune::net::EntityRef,UActor*>> actors;
    openrune::net::Snapshot previous,current;
    std::map<uint32_t,float> effectAges;
    std::map<uint32_t,std::pair<uint32_t,bool>> adaptiveLives;
    std::map<uint32_t,HealthOverlay> healthOverlays;
    struct PendingOutcome { openrune::net::CombatOutcome outcome; uint64_t since; };
    std::deque<PendingOutcome> pendingOutcomes;
    std::vector<openrune::net::CombatOutcome> resolvedOutcomes;
    uint64_t lastCombatId=0;
    std::deque<openrune::net::EntityRef> retired;
};
void RunRuneNetworkProbe(Engine&);
void RunRuneNetworkNativeProbe(Engine&);
void RunRuneNetworkProcessProbe(Engine&,const std::string& role,uint16_t port,unsigned oneWayLatencyMs=0,unsigned lossPercent=0);
openrune::net::MatchContract MakeRuneMatchContract(Engine&,const std::string& map,uint16_t maxPlayers,openrune::net::GameMode mode=openrune::net::GameMode::Deathmatch,uint8_t teams=2,uint8_t arenaTeamSize=1,bool limbsScore=false);

// Read-only collision adapter for predicted walking. Unsupported movement stays
// server-owned rather than replaying VM side effects on presentation actors.
bool StepRunePredictedMovement(Engine&,UPlayerPawn*,openrune::net::MovementPose&,
    const openrune::net::Input&,const openrune::net::MovementAck&,float);
void PresentRunePredictedMovement(UPlayerPawn*,const openrune::net::MovementPose&);
