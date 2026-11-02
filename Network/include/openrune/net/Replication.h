#pragma once
#include "openrune/CharacterRoster.h"
#include "SecureSession.h"
#include <map>
#include <deque>
#include <functional>
#include <string>

namespace openrune::net
{
constexpr size_t MaxEntities = 512, SnapshotPartBytes = 640, MaxSnapshotBytes = 40960;
constexpr uint64_t InputMaxAge = 250, SnapshotTransferTimeout = 10000;
struct EntityRef
{
    uint32_t id = 0, generation = 0;
    bool operator==(const EntityRef&) const = default;
};
enum class EntityKind : uint8_t { Pawn = 1, Weapon, Shield, Projectile, Pickup, Mover, Limb, Objective, Corpse, Debris, Effect };
// Integer wire units: position 1/8 Rune unit, velocity 1/4 unit/sec,
// angles 1/65536 turn; signed animation phase 1/32767 cycle (negative for
// authored tween-in); animation rate 1/256 cycle/sec.
// Optional 19-byte native-combat extension; at most 64 pawns per world.
// Ordinary actors retain the compact 77-byte record. No client gameplay state.
struct NativeCombat
{
    EntityRef offhand;
    uint8_t flags=0; // left swing, crossed guard, bloodlust active, rush pose
    uint8_t guard=0, meter=0, phase=0; // blend /255, strength percent, native phase
    uint16_t age=0, cooldown=0; // milliseconds
    uint8_t ready=0; // leap=1, charge=2; server evaluated
    uint8_t runePower=0, runeCapacity=0; // original Rune Power, 0..200
    bool operator==(const NativeCombat&) const = default;
};
struct Entity
{
    EntityRef ref;
    EntityKind kind = EntityKind::Pawn;
    uint8_t flags = 0; // alive, grounded, blocking, body-loop, torso-loop, hidden
    int16_t health = 0;
    uint32_t life = 1; // Increments on respawn; prevents applying old combat outcomes.
    std::array<int32_t,3> position{};
    std::array<int16_t,3> velocity{};
    std::array<uint16_t,3> angles{};
    uint16_t bodyAnimation = 0, torsoAnimation = 0;
    int16_t animationPhase = 0;
    int16_t animationRate = 0;
    int16_t torsoPhase = 0;
    int16_t torsoRate = 0;
    EntityRef owner, weapon, shield;
    uint8_t attachmentJoint = 255; // 255=detached; otherwise an authored joint on owner.
    NativeCombat combat;
    bool operator==(const Entity&) const = default;
};
using World = std::vector<Entity>; // Sorted, unique numeric IDs; references resolve in this world.
bool validWorld(const World&);
// Paired with the exact world checkpoint, not merely packet receipt. The
// authority reports the input whose acceleration produced this position and
// the number of 60 Hz physics ticks spent holding it. Parameters are bounded
// wire units: speed 1/8, acceleration 1. Walking: friction 1/256, step 1/8.
// Falling: friction carries downward gravity magnitude /1, step terminal speed /8.
enum MovementMode : uint8_t { MovementUnsupported=0,MovementWalking=1,MovementFalling=2 };
struct MovementAck
{
    EntityRef ref;
    uint32_t life=0;
    uint64_t input=0;
    uint16_t heldTicks=0;
    uint8_t mode=MovementUnsupported;
    uint16_t speed=0, acceleration=0, friction=0, step=0;
    bool operator==(const MovementAck&) const = default;
};
bool validMovementAck(const std::optional<MovementAck>&,const World&);
struct Snapshot
{
    uint64_t id = 0, baseline = 0, tick = 0;
    World world;
    std::optional<MovementAck> movement = {};
    bool operator==(const Snapshot&) const = default;
};
bool encodeSnapshot(const Snapshot&, const Snapshot* baseline, Bytes&);
bool decodeSnapshot(std::span<const uint8_t>, const Snapshot* baseline, Snapshot&);
struct SnapshotPart
{
    uint64_t id = 0, baseline = 0, tick = 0;
    uint32_t total = 0;
    uint8_t index = 0, count = 0;
    Bytes bytes;
};
struct SnapshotFeedback
{
    uint64_t installed = 0, receiving = 0, receivedMask = 0;
    // receiving!=0 + receivedMask==0 invalidates that exact transfer.
    // requestFull says whether the installed baseline must also be replaced.
    bool requestFull = false;
};
struct Input
{
    uint64_t sequence = 0, createdMs = 0;
    uint32_t life = 0;
    int16_t forward = 0, strafe = 0, pitch = 0;
    uint16_t yaw = 0, held = 0; // attack=1, block=2, crouch=4; complete state each sample
    bool operator==(const Input&) const = default;
};
enum class ActionKind : uint8_t { Attack = 1, RaiseShield, LowerShield, Kick, Bash, Use, Throw, Jump, Respawn, BloodLeap, ShieldCharge, Powerup, SelectFists, SelectSword, SelectHammer, SelectAxe };
struct Action
{
    uint64_t id = 0, input = 0, createdMs = 0;
    uint32_t life = 0;
    ActionKind kind = ActionKind::Attack;
    bool operator==(const Action&) const = default;
};
// Admission is a receipt. Started/ExecutionRejected report the later engine decision.
// A Started kick/bash still needs a committed CombatOutcome for its contact.
enum class Admission : uint8_t { Admitted = 1, Expired, WrongLife, MissingInput, RateLimited, Busy, TooOld,
    Started, ExecutionRejected, Cancelled };
struct ActionResult
{
    uint64_t id = 0;
    Admission status = Admission::Admitted;
    bool operator==(const ActionResult&) const = default;
};
struct InputBatch { std::vector<Input> inputs; std::vector<Action> actions; SnapshotFeedback snapshots; };
struct InputFeedback { uint64_t highestInput = 0; std::vector<ActionResult> actions; };
bool encodeInputs(uint32_t epoch, const InputBatch&, Bytes&);
bool decodeInputs(uint32_t epoch, std::span<const uint8_t>, InputBatch&);
bool encodeFeedback(uint32_t epoch, const InputFeedback&, Bytes&);
bool decodeFeedback(uint32_t epoch, std::span<const uint8_t>, InputFeedback&);
bool encodeActionDecision(uint32_t epoch, const ActionResult&, Bytes&);
bool decodeActionDecision(uint32_t epoch, std::span<const uint8_t>, ActionResult&);
enum class CloseReason : uint8_t { Leave=1, MatchEnded=2 };
bool encodeClose(uint32_t epoch,CloseReason,Bytes&);
bool decodeClose(uint32_t epoch,std::span<const uint8_t>,CloseReason&);
bool encodePart(uint32_t epoch, const SnapshotPart&, Bytes&);
bool decodePart(uint32_t epoch, std::span<const uint8_t>, SnapshotPart&);

// Authenticated join contract. Hashes cover gameplay packages and the selected
// map, excluding cosmetic texture replacements. The caller computes the hashes.
enum class GameMode : uint8_t { Deathmatch=1, TeamDeathmatch=2, Arena=3, HeadBall=4 };
constexpr bool teamMode(GameMode mode) { return mode==GameMode::TeamDeathmatch || mode==GameMode::HeadBall; }
struct MatchContract
{
    uint16_t tickHz = 60, maxPlayers = 2;
    uint32_t rulesRevision = 1;
    std::string map;
    std::array<uint8_t,32> gameplayHash{}, mapHash{};
    GameMode mode=GameMode::Deathmatch;
    uint8_t teams=2, arenaTeamSize=1, limbsScore=0;
    bool operator==(const MatchContract&) const = default;
};
bool encodeMatchContract(uint32_t epoch, const MatchContract&, Bytes&);
bool decodeMatchContract(uint32_t epoch, std::span<const uint8_t>, MatchContract&);
// Reliable server-owned match state. Player IDs are stable invitation slots;
// a map epoch separates snapshots, actions and scores across travel.
enum class MatchPhase : uint8_t { Waiting=1, Playing, Intermission };
struct PlayerScore {
    uint32_t player=0;
    int32_t frags=0;
    uint16_t deaths=0;
    uint8_t team=255;
    uint8_t queue=255; // Arena queue position, 1..16; 255 means not queued.
    EntityRef pawn; // Current avatar; absent while this invitation slot is vacant.
    bool operator==(const PlayerScore&) const = default;
};
struct MatchStatus {
    MatchPhase phase=MatchPhase::Waiting;
    uint32_t secondsLeft=0, fragLimit=0, winner=0; // winner=0 means a tie/no winner
    std::vector<PlayerScore> scores;
    GameMode mode=GameMode::Deathmatch;
    uint8_t winningTeam=255;
    std::array<int32_t,4> teamScores{};
    uint8_t teams=2;
    uint8_t arenaPhase=0, arenaCountdown=0; // Original Waiting/Fight/Pre/Post states.
    uint32_t arenaRound=0;
    bool headBallStarted=false;
    uint8_t headBallCountdown=0;
    std::array<uint8_t,4> readyTeams{};
    bool operator==(const MatchStatus&) const = default;
};
bool encodeMatchStatus(uint32_t epoch,const MatchStatus&,Bytes&);
bool decodeMatchStatus(uint32_t epoch,std::span<const uint8_t>,MatchStatus&);
struct MapTravel { uint32_t epoch=0; MatchContract match; };
bool encodeMapTravel(uint32_t sessionEpoch,const MapTravel&,Bytes&);
bool decodeMapTravel(uint32_t sessionEpoch,std::span<const uint8_t>,MapTravel&);
enum class ActorOperation : uint8_t { Spawn = 1, Despawn = 2, Appearance = 3 };
struct SkinRef { std::string package; uint32_t object=0; bool operator==(const SkinRef&) const = default; };
struct ActorAppearance {
    SkinRef skeleton, substitute;
    int16_t scale=1024;
    uint8_t drawType=2;
    uint8_t mesh=0, firstGroup=0;
    std::array<int32_t,16> groupFlags{};
    std::array<uint8_t,50> jointFlags{};
    std::array<SkinRef,8> skins{};
    bool operator==(const ActorAppearance&) const = default;
};
struct ActorEvent
{
    uint64_t id = 0;
    EntityRef ref;
    ActorOperation operation = ActorOperation::Spawn;
    EntityKind kind = EntityKind::Pawn;
    std::string package, actorClass; // Empty for Despawn.
    std::string proxyPackage, proxyClass; // Optional local animation proxy archetype.
    std::string mapActor; // Authored mover in the agreed map; never spawn a brush-less copy.
    ActorAppearance appearance;
    bool operator==(const ActorEvent&) const = default;
};
bool encodeActorEvent(uint32_t epoch, const ActorEvent&, Bytes&);
bool decodeActorEvent(uint32_t epoch, std::span<const uint8_t>, ActorEvent&);

// A committed server decision, distinct from input admission and presentation.
// Movement remains authoritative in snapshots; this record does not prescribe
// client-side physics or cause a client to run damage scripts.
enum class CombatMove : uint8_t { Kick = 1, Bash = 2, Weapon = 3 };
enum class CombatContact : uint8_t { Miss = 1, Blocked = 2, Hit = 3 };
struct CombatOutcome
{
    uint64_t id = 0, tick = 0, action = 0; // action=0 for server-controlled actors
    EntityRef attacker, target; // target is absent only for Miss
    uint32_t attackerLife = 0, targetLife = 0;
    CombatMove move = CombatMove::Kick;
    CombatContact contact = CombatContact::Miss;
    int16_t targetHealth = 0; // authoritative post-contact value; zero for Miss
    bool staggered = false; // bash hit only; movement state follows in snapshots
    bool operator==(const CombatOutcome&) const = default;
};
bool encodeCombatOutcome(uint32_t epoch, const CombatOutcome&, Bytes&);
bool decodeCombatOutcome(uint32_t epoch, std::span<const uint8_t>, CombatOutcome&);

// Disposable server-authored sound cues. They ride in expiring feedback, never
// the reliable combat lane. Asset references resolve only to local sound exports.
struct SoundCue {
    uint64_t id=0;
    EntityRef source;
    uint32_t emitter=0, object=0;
    std::string package;
    std::array<int32_t,3> position{};
    uint16_t volume=256, radius=1500, pitch=256;
    uint8_t channel=0; // Original slot*2 plus no-override bit.
    bool operator==(const SoundCue&) const = default;
};
bool encodeSoundFeedback(uint32_t,const InputFeedback&,const std::vector<SoundCue>&,Bytes&);
bool decodeSoundFeedback(uint32_t,std::span<const uint8_t>,InputFeedback&,std::vector<SoundCue>&);

// Intent only: admission does not mean a hit, pickup or damage has committed.
// The engine adapter must bind one authenticated peer to its owned pawn and validate
// gameplay cooldowns, collision, inventory and allowed simulation time separately.
class InputSender
{
public:
    std::optional<uint64_t> capture(Input, uint64_t now);
    std::optional<uint64_t> act(ActionKind, uint64_t now);
    InputBatch batch(uint64_t now, SnapshotFeedback = {});
    void receive(const InputFeedback&);
    void receiveDecision(ActionResult);
    std::vector<ActionResult> drainResults();
    std::vector<uint64_t> drainUnconfirmed(); // Receipt is unknown, not a rejection.
    size_t pending() const { return actions.size(); }
    uint64_t newestAction() const { return nextAction-1; }
private:
    uint64_t nextInput = 1, nextAction = 1, lastNow = 0;
    std::deque<Input> history;
    struct Pending { Action action; Input context; };
    std::map<uint64_t,Pending> actions;
    std::deque<ActionResult> results;
    std::deque<uint64_t> unconfirmed;
    std::map<uint64_t,bool> retired;
    std::map<uint64_t,Admission> reported;
};
struct AdmittedAction { Action action; Input input; uint64_t localTime = 0; };
class InputReceiver
{
public:
    using MapTime = std::function<std::optional<uint64_t>(uint64_t)>;
    explicit InputReceiver(uint32_t life = 1);
    void setLife(uint32_t life); // Does not reset action deduplication.
    bool receive(const InputBatch&, uint64_t now, const MapTime&);
    Input held(uint64_t now) const; // Neutral after expiry or life change.
    InputFeedback feedback() const;
    std::vector<AdmittedAction> drainActions(uint64_t now);
    bool resolve(uint64_t id, Admission result); // Engine-thread decision for an admitted action.
private:
    uint32_t life;
    uint64_t highestInput = 0, highestAction = 0, lastNow = 0, refillAt = 0;
    unsigned tokens = 8;
    std::optional<Input> current;
    std::map<uint64_t,Input> observed;
    std::map<uint64_t,ActionResult> decisions;
    std::deque<ActionResult> recent;
    std::vector<AdmittedAction> admitted;
    void remember(ActionResult);
};
class SnapshotSender
{
public:
    struct Stats {
        uint64_t captures=0,transfers=0,parts=0,retries=0,replacements=0,encodedBytes=0;
        size_t maximumBytes=0,latestBytes=0;
    };
    explicit SnapshotSender(bool compress=true): compress(compress) {}
    bool capture(World, uint64_t tick, std::optional<MovementAck> movement = {});
    bool receive(const SnapshotFeedback&);
    std::optional<SnapshotPart> poll(uint64_t now,bool transmissionQueueEmpty=true,uint64_t retryMs=100);
    uint64_t activeId() const { return active ? active->id : 0; }
    uint64_t baselineId() const { return installed; }
    const Stats& stats() const { return counters; }
private:
    Stats counters;
    World desired;
    std::optional<MovementAck> desiredMovement;
    uint64_t desiredTick = 0, nextId = 1, installed = 0, lastNow = 0, started = 0;
    bool dirty = false, full = true, compress = true;
    std::optional<Snapshot> active;
    Bytes encoded;
    uint64_t receivedMask = 0;
    std::array<uint64_t,64> nextSend{};
    size_t cursor = 0;
    std::map<uint64_t,Snapshot> cache;
};
class SnapshotReceiver
{
public:
    static constexpr size_t MaxAssemblies=4;
    bool receive(const SnapshotPart&, uint64_t now);
    const Snapshot* candidate() const { return pending ? &*pending : nullptr; }
    bool commit(uint64_t id); // Call only AFTER the engine installs this exact candidate atomically.
    const Snapshot* installed() const;
    SnapshotFeedback feedback(uint64_t now);
    void resync(); // Preserve installed world but request a full replacement.
    void discardObsolete(); // Lifecycle overtook candidate; installed baseline is still valid.
    size_t bufferedTransfers() const { return assemblies.size(); }
private:
    struct Assembly { SnapshotPart meta; uint64_t at = 0, mask = 0; Bytes bytes; };
    uint64_t latest = 0, lastNow = 0, rejectedTransfer = 0;
    bool needFull = true;
    std::map<uint64_t,Assembly> assemblies;
    std::optional<Snapshot> pending;
    std::map<uint64_t,Snapshot> cache;
    void expireAssemblies(uint64_t now);
};
// Owns the replication use of Gameplay/Snapshots for one SecureSession. Call after
// session.step(now), then drain game-facing results / explicitly commit snapshots.
// New session + new replication objects are mandatory for reconnect/map epoch change.
bool encodeCharacterChoice(uint32_t epoch, CharacterChoice, Bytes&);
bool decodeCharacterChoice(uint32_t epoch, std::span<const uint8_t>, CharacterChoice&);

class ReplicationPeer
{
public:
    ReplicationPeer(SecureSession&, MatchContract, CharacterChoice choice = {});
    const std::optional<CharacterChoice>& characterChoice() const { return receivedCharacter; }
    void step(bool sendInputs = true);
    void flushInputs();
    bool publishStatus(const MatchStatus&);
    void publishSound(SoundCue);
    std::vector<SoundCue> drainSounds();
    const std::optional<MatchStatus>& status() const { return receivedStatus; }
    // Enqueue travel before resetting; the same authenticated connection stays open.
    bool travel(const MatchContract&);
    const std::optional<MapTravel>& requestedTravel() const { return travelRequest; }
    bool acceptTravel(const MatchContract& locallyVerified);
    uint32_t mapEpoch() const { return replicationEpoch; }
    // The server supplies monotonically increasing IDs after committing the
    // result. A failed publish must be retried with the same ID, never skipped.
    bool publishCombat(const CombatOutcome&);
    uint64_t lastCombatPublished() const { return lastCombatSent; }
    std::vector<CombatOutcome> drainCombat(); // Client only; already authenticated.
    bool combatOverflowed() const { return combatOverflow; } // Caller must resync/disconnect.
    bool publishActor(const ActorEvent&);
    uint64_t lastActorPublished() const { return lastActorSent; }
    std::vector<ActorEvent> drainActors();
    bool actorOverflowed() const { return actorOverflow; }
    bool decideAction(uint64_t id,Admission decision); // Retains engine decision until reliable enqueue.
    bool matchAgreed() const { return matchState==MatchState::Agreed; }
    bool matchMismatch() const { return matchState==MatchState::Mismatch; }
    void close(CloseReason reason) { if(!closing)closing=reason; }
    std::optional<CloseReason> remoteClosed() const { return receivedClose; }
    InputSender inputs;
    InputReceiver commands;
    SnapshotSender snapshots;
    SnapshotReceiver world;
    uint64_t rejectedMessages() const { return rejected; }
private:
    enum class MatchState { Awaiting, Agreed, Mismatch };
    SecureSession& session;
    MatchContract match;
    uint32_t replicationEpoch;
    std::optional<MatchStatus> receivedStatus;
    std::optional<MapTravel> travelRequest;
    void resetMap(MatchContract,uint32_t);
    MatchState matchState=MatchState::Awaiting;
    bool matchSent=false, characterSent=false;
    CharacterChoice localCharacter;
    std::optional<CharacterChoice> receivedCharacter;
    uint64_t nextFeedback = 0, lastActive = 0, rejected = 0, lastActionOffered = 0;
    uint64_t lastCombatSent = 0, lastCombatReceived = 0, lastCombatTick = 0;
    uint64_t lastActorSent = 0, lastActorReceived = 0;
    struct PendingSound { SoundCue cue; uint64_t at; unsigned sends=0; };
    std::deque<PendingSound> pendingSounds;
    std::deque<SoundCue> receivedSounds;
    uint64_t nextSoundId=1, lastSoundReceived=0, nextSoundFeedback=0;
    std::deque<CombatOutcome> combatResults;
    std::deque<ActorEvent> actorEvents;
    std::deque<ActionResult> pendingDecisions;
    bool combatOverflow = false, actorOverflow = false;
    std::optional<CloseReason> closing,receivedClose;
    bool closeSent=false;
};
}
