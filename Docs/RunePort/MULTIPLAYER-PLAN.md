# Open Rune multiplayer audit and networking plan

November 1 deployment: matching protocol/ALPN 22 Android and Windows builds are now updated; native rules 20/snapshot 6. Actual OPPO-to-Windows private join at native resolution/4× AA and Android Network settings checks pass. No public match remains running. See [installed results and limits](NETWORK-PREDICTION-SMOOTHING.md#installed-follow-up); the earlier packaging-pending statement below is historical.

November 1 movement checkpoint: [bounded falling prediction and adaptive remote smoothing](NETWORK-PREDICTION-SMOOTHING.md), protocol/ALPN **22**, native rules **20**, snapshot **6**. Mac Release/sanitizer 15/15, native physics oracle and impaired rendered/recovery/rejoin checks pass. Walking and ordinary falling are predicted; unsupported movement remains authoritative. Current APK/Windows deployment and matching discovery-runtime packaging remain pending. Older milestone version/scope statements below are historical.

November 1 implementation sequence: [diagnostics and measured reliability follow-up](MULTIPLAYER-DIAGNOSTICS.md). This supersedes older milestone scope statements for tags/diagnostics; broader prediction, adaptive smoothing and release packaging remain staged work.

Public discovery follow-up: [HyperDHT integration](HYPERDHT-DISCOVERY.md) now connects public hosting, Find games and authenticated joining on Android. The earlier future-directory proposal below is historical. Both hosting directions now pass between laptop Wi-Fi and the OPPO cellular hotspot, with distinct public internet addresses. Other NAT combinations, blocked UDP, human internet combat and long endurance remain unvalidated.


Current checkpoint: [private multiplayer completion](PRIVATE-MULTIPLAYER-STATUS.md), protocol/ALPN **17**, native rules **17**, snapshot **6**. Four private game modes and their supported map variants are implemented; public discovery remains outside this milestone. Older checkpoint versions below are historical.

Latest mode checkpoint: [Team Deathmatch](NETWORK-TEAM-DEATHMATCH.md), protocol/ALPN 12 and native rules 12. Earlier versions below are historical.

Latest Deathmatch checkpoint: [four players, scoring, match limits and map rotation](NETWORK-DEATHMATCH.md). Protocol/ALPN 11 and native rules 11 supersede earlier checkpoints below.

Latest October 26 checkpoint: [interactive private multiplayer](NETWORK-INTERACTIVE.md)
adds rendered clients, Android join/leave/respawn menus and protocol 10. Earlier
non-playable/protocol-version statements below describe historical milestones.
See that checkpoint for exact runtime validation and remaining limits.
The [responsiveness follow-up](NETWORK-RESPONSIVENESS.md) adds grounded prediction,
pipelined small snapshots and the user-selected 33 ms defense window. Full attack
rewind and the human fairness gate remain unfinished.

October 25, 2026. Design proposal based on the working tree at HEAD `8d20833c`, including its uncommitted changes at that time. The audit itself made no networking or device changes. **Decision updated October 26:** Open Rune multiplayer will use the new authenticated protocol and an Open Rune server so its custom combat rules work for every participant. Original Rune server compatibility is deferred as a separate classic-rules mode; it is not a dependency of the playable Open Rune path. See [the original-binary assessment](ORIGINAL-MULTIPLAYER-PORT-ASSESSMENT.md).

Build an original, portable networking implementation for Open Rune, with authoritative combat, responsive controls and bounded CPU, memory and bandwidth costs. Target 32 participants eventually; prove two-player melee first. Original Rune server and protocol compatibility are outside the initial scope. A new protocol does not by itself make existing gameplay safe to run on multiple machines.

Updated after source review of Valve GameNetworkingSockets and Source SDK, ENet, published melee networking material, and AI-assisted GitHub projects. [Research evidence and limitations](MULTIPLAYER-RESEARCH.md) records the inspected revisions and distinguishes observed implementation from our proposals.

Implementation subsequently authorized: the standalone `Network/` foundation implements an experimental inner packet contract, bounded delivery lanes and a deterministic impairment harness. October 26 adds authenticated DTLS sessions and remote clock mapping, with test-only IPv4/IPv6 secure loopback exchanges. Read [NETWORK-PROTOCOL.md](NETWORK-PROTOCOL.md), [NETWORK-VALIDATION.md](NETWORK-VALIDATION.md) and [NETWORK-AUTH-CLOCKS.md](NETWORK-AUTH-CLOCKS.md). The next October 26 milestone adds input/action admission, explicit snapshot installation and full/delta world transfer; see [NETWORK-REPLICATION.md](NETWORK-REPLICATION.md). Complete congestion control, admission/provisioning services and game integration remain unfinished; the full phase-zero/phase-one gates below are not declared complete.

The October 26 checkpoint recorded a user-requested pause. Work resumed when the
user chose Open Rune combat over connecting to unmodified Rune servers. An opt-in
engine boundary exists, with a desktop in-process fixture and saved test results;
the resumed decision does not make that fixture playable multiplayer. Read
[NETWORK-ENGINE-CHECKPOINT.md](NETWORK-ENGINE-CHECKPOINT.md).
The later [join and lifecycle checkpoint](NETWORK-JOIN-LIFECYCLE.md) covers
content agreement, actor archetypes, reliable engine decisions, naturally timed
kick/bash contact and a moving two-process fixture. It remains an isolated test
path, not a playable match or Android networking release.
The source audit table below describes the original audit, before that adapter;
it is not a claim that every referenced function remains unchanged.

## What exists today

| Area | Source evidence | Consequence |
| --- | --- | --- |
| Game network driver | `SurrealEngine/Packages/Engine/Subsystems/UNetDriver.h:5`, `Network/UNetConnection.h:5` and `Network/UActorChannel.h:5` under `Packages/Engine` contain shell classes. `USurrealNetworkDevice.cpp` handles configuration. | There is no functioning game replication stack here to optimize. Most of the proposed networking would be new implementation. |
| Lower-level sockets | `SurrealEngine/Packages/IpDrv/UUdpLink.cpp:44` leaves receive events unfinished; binary read/write are stubs. IPv4 socket creation and text sending exist. | Socket scaffolding is not usable multiplayer transport. A new portable socket adapter should be independent of the script link actors. |
| Session startup | `SurrealEngine/Engine.cpp:1266` and `:1382` force standalone mode. `LoginPlayer` at `:1530` creates a pawn and assigns it to the local viewport. | Separate server, client and standalone startup; multiple connections must own distinct pawns without creating multiple local viewports. |
| Script networking metadata | `SurrealEngine/Packages/Core/UFunction.cpp:18` loads network flags and a replication offset. `SurrealEngine/VM/Frame.cpp:301` dispatches calls locally; the inspected path has no network routing. | Reading a replication declaration does not implement it. Use a deliberate gameplay adapter instead of assuming the VM already synchronizes calls. |
| Original movement design | Recovered `Engine/PlayerPawn.uc:568`, `:856` and `:945` contain `ServerMove`, correction/replay and saved movement logic, including old-input redundancy and timestamp checks. | The original design already contains useful prediction concepts. Treat it as behavior/reference material; write the new protocol and adapters ourselves. |
| Player count | Recovered `Engine/GameInfo.uc:580` clamps MaxPlayers to 32; `GameReplicationInfo.uc:34` has a 32-entry player array. | 32 is a sensible initial design ceiling, not demonstrated port capacity. Individual modes and UI still require validation. |
| Simulation clock | `SurrealEngine/Engine.cpp:415` derives game elapsed time from the render loop; `:419` freezes it for the menu. | Multiplayer needs a fixed simulation clock independent of presentation. Opening a player's menu cannot pause the shared match. |
| Object identity | `SurrealEngine/Packages/Engine/Resources/Level/ULevel.cpp:130` changes actor indices when compacting the actor array. | Never use actor array indices or pointers as persistent network identities. |
| Melee | `SurrealEngine/Native/NRune.cpp:570` samples authored attack frames for weapon sweeps. Recovered `Engine/Weapon.uc:1733` dispatches swept joint contacts and damage. | Historical hit testing needs weapon poses, target joint geometry and shield state, not just player positions. |
| New combat features | `SurrealEngine/RuneKick.cpp` and `RuneBash.cpp` perform damage, recoil, cooldowns and animation callbacks locally without a multiplayer authority boundary. Their state includes native fields on `UPawn`. | These need explicit authoritative state and event replication; reflecting original script properties would miss them. |
| Headless support | `SurrealEngine/GameApp.cpp:58` provides a package/native validation path. | This is useful test infrastructure, but it is not a continuously running dedicated server. |

Recovered scripts above are under `Tools/RuneRecovery/recovered-scripts/`. Their embedded text is not a complete reconstructed original native networking implementation, and has not all been matched against compiled bytecode. Confirm any behavior we reuse with focused VM/runtime fixtures. This audit establishes code structure, not multiplayer performance or compatibility.

## Scope and implementation boundaries

**Current product rule:** An Open Rune match requires an Open Rune server that
understands the same gameplay rules and feature set. Kick, shield bash, their
shield-block response, recoil, stagger and cooldowns are authoritative server
decisions. Clients send intent and render predicted presentation; they do not
decide damage, displacement or stun. A peer with mismatched combat rules or
collision/gameplay content must receive a clear join failure. Cosmetic textures
may differ when they do not change gameplay data.

Unmodified Rune Classic servers cannot enforce those new moves. A possible
future classic-compatibility mode would need the exact old wire protocol and
would disable Open Rune-only combat unless a separately compatible server
explicitly supported it. Do not make a client-only kick or bash appear to work
against an original server. Preserve the original-protocol research privately
as reference evidence; do not copy generated pseudocode into the new network
implementation.

Write our own C++20 packet format, bounded serializer, connection state machine, message delivery, replication, prediction and combat arbitration. Keep the networking core independent of Android, SDL, Vulkan and UnrealScript. Reference published algorithms without importing old engine or mod networking code.

Use OS sockets and a vetted implementation of cryptography and a standard authenticated handshake. Do not invent encryption or key exchange. The exact security dependency is a phase-zero decision: it must build for Android, Windows, Linux and macOS, remain suitable for later iOS, and fit our distribution requirements. Small dependencies do not compromise ownership of the game protocol; a literal zero-dependency cryptographic implementation would be a different and unsuitable scope.

Initial scope is LAN/direct-connect deathmatch, then dedicated internet hosting and 32-player validation. Campaign co-op, host migration, voice chat, automatic mod downloads, old-server compatibility and a public matchmaking service are later projects. Easy internet joining requires NAT traversal and often a relay service; a custom UDP protocol does not remove that requirement.

## Server browser and hosting roadmap (accepted October 27)

Priority update: start with temporary player-hosted matches and direct/LAN
joining, with no required public directory or permanently hosted service. A
player chooses Host, shares connection details or a private invitation, and
plays while their device runs the authority. Ending the host ends the match;
automatic host migration is outside this milestone. Other players leaving must
not end the match. The current private invitation workflow remains the
implemented connection path; integrated Host UI and flexible joining are work
still to do.

Provide a standalone headless server option using the same authority for anyone
who wants to run a match without playing. It can be launched just for a game
night; dedicated does not mean permanent. Package and supervise the existing
server loop, including settings, logs, shutdown and platform support. Initially
keep authority and the playing client in separate processes because of engine
global state. Android hosting needs a separate-process/service lifecycle and
resource-budget validation; do not claim it follows automatically from desktop
hosting.

An optional future discovery service would be independent of Rune Classic's
master servers. It is not a prerequisite for either hosting option.

- **Find games:** public listings show server name, map, mode, players/capacity,
  compatible protocol/rules/content versions, and client-measured ping.
- **Host game:** private or public matches using the same authoritative rules as
  dedicated servers. Initially run the dedicated host as a separate process.
- **LAN games:** local discovery without an Internet directory.
- **Direct connect and Favorites:** connect to a known address even if the
  directory is unavailable, with an authenticated server identity/join flow.
- **Join invitation:** private, nonadvertised matches; invitation secrets never
  appear in public listings, logs, or the directory.

A small HTTPS directory accepts bounded, authenticated server registrations and
periodic heartbeats, verifies endpoint ownership/reachability, expires stale
entries, and rate-limits advertisements and queries. Clients fetch listings,
check compatibility, and ping/connect directly to the game server. Treat all
advertised text and endpoints as untrusted. Directory tokens are separate from
match invitation credentials. Public joining needs a reviewed authentication
flow; making today's private invitation secret public is not that flow.

The directory carries no combat packets and adds no hop to gameplay. Existing
matches, LAN and direct connection survive a directory outage. Make the directory
address configurable and publish a self-hostable service so community operators
can supply alternatives. A browser is discovery, not skill matchmaking.

Deliver player hosting, LAN/direct joining and the optional headless launcher
first. Add public-directory hosting only if an operator becomes available.
Advertising a home/tablet host does not make it reachable through home or
carrier NAT. Add endpoint negotiation, NAT traversal and an optional relay
later; disclose when a relay is used. Relay hosting has operational and bandwidth
costs, unlike a small directory. Do not promise universal one-click home hosting
until tested across restrictive networks. Valve's
[P2P connectivity documentation](https://github.com/ValveSoftware/GameNetworkingSockets/blob/master/README_P2P.md)
is reference material for these distinctions, not a dependency decision.

First acceptance: a player can host and play, another player can directly join,
leave and reconnect without terminating the host; the headless variant runs the
same match rules without a rendered client. Validate LAN first and document
Internet reachability requirements. A reachable address/forwarded game port or
an external connectivity solution is still needed across restrictive networks;
an invitation alone cannot bypass NAT.

Later directory acceptance: stale listings expire; incompatible servers are explained; malicious
listings cannot execute commands; private games remain private; direct/LAN play
works with the directory offline; a reachable dedicated server can be joined
from outside its LAN. Later NAT/relay acceptance is a separate milestone.

## Architecture

| Proposed module | Responsibility |
| --- | --- |
| `NetCore` | Explicit byte encoding, protocol identifiers, counters, bounded readers/writers and packet validation. No engine objects. |
| `NetTransport` | IPv4/IPv6 socket adapters, authenticated sessions, acknowledgements, delivery queues, pacing, timeouts and congestion response. |
| `NetSession` | Handshake, match rules/content identity, joins, disconnects, reconnects and map epochs. |
| `NetReplication` | Stable entity IDs, spawn/despawn, snapshots, acknowledged baselines, interest and per-peer budgets. |
| `NetPrediction` | Local input history, movement replay, correction and remote interpolation. |
| `RuneNetAdapter` | Explicit mapping to Rune pawns, equipment, animation, match rules and supported script callbacks. |
| `CombatHistory` | Bounded collision histories, attack timelines, defense timing and one authoritative outcome per interaction. |
| `NetTest` | Seeded network impairment, scripted clients, packet replay, fuzzing and metrics. |

The engine simulation thread owns gameplay objects. A network I/O worker may handle sockets and packet authentication through bounded queues; it must never call the VM or touch live actors. Do not create a thread per connection. Keep blocking DNS, connection setup and file work outside simulation ticks.

Separate fixed simulation steps from visual interpolation and presentation. If presentation blocks the simulation clock, introduce a bounded handoff of immutable render state; a second thread must not read changing actor/VM data directly. Audit render-time pose queries and object/resource lifetimes before enabling concurrent rendering. This integration is more substantial than adding a fixed-step accumulator.

A player-hosted match and a dedicated server use the same authority implementation. The local host uses the same command validation and combat resolution path. A local host still has a latency advantage; the architecture cannot eliminate that physical difference. Remote client and server run as separate processes initially because the engine has substantial global state.

## Proposed protocol

These are starting design choices, to be frozen in a versioned wire specification and golden packet fixtures before integration.

- Use UDP with a conservative initial maximum of 1,200 bytes per datagram, including our headers and authentication overhead. Account for encapsulation overhead and path limits; lower the limit if needed. Do not rely on IP fragmentation. Initial world state is a bounded, paced transfer of chunks; never one enormous gameplay datagram.
- Encode fixed-width integers and explicit byte order; never transmit C++ structs, pointers, platform enums or raw object memory. Validate lengths, counts, ranges, finite numbers and entity references before applying anything.
- Header fields identify protocol version, connection, packet number and authenticated payload length. Prefer bounded selective acknowledgement ranges, with maximum count, age and parser work frozen in phase zero. Use monotonically increasing packet numbers with a defined exhaustion policy and unique cryptographic nonces. Authenticate acknowledgement metadata too. Retransmitted message segments receive fresh packet numbers; a late original packet must still deduplicate safely after apparent loss. Bound reported ACK delay and reject acknowledgements for unsent packets.
- Track packet receipt separately from message delivery and snapshot installation. A received packet does not prove that a fragmented snapshot was fully decoded or installed. Delta baselines must reference a snapshot the receiver explicitly reports as installed and retains.
- Handshake progression: address validation, authenticated key establishment, protocol/content/rules agreement, initial state transfer, ready acknowledgement, then gameplay. Bound pending handshakes, allocate expensive state only after validation, and limit response amplification. Invite secrets or verified server identity establish trust; encryption alone does not authenticate the intended server.
- Authenticate packets and reject replays. Apply per-connection limits to commands, reliable queues, fragments, entities and parser work. Unknown or incompatible required versions fail with a readable reason. Malformed traffic cannot invoke arbitrary VM functions or console commands.

| Message class | Delivery behavior |
| --- | --- |
| Movement and view input | Sequenced commands with a small redundant history; bounded-age inputs expire instead of forming an ever-growing resend queue. |
| Attack, block edges, kick, bash, use and throw | Unique action IDs and original input times, redundantly carried until acknowledged or expired. Apply each action at most once; never replay a stale attack seconds later. A complete held-button state repairs lost release events. |
| World snapshots | Newer state supersedes older state; delta compression against retained, acknowledged baselines. Fall back to a fresh baseline after loss or resynchronization. |
| Spawn, despawn, equipment ownership, match and committed combat events | Reliable delivery with stable IDs, idempotent application and explicit state revisions/dependencies. Each logical lane progresses independently. |
| Initial state and map transition | Bounded reliable chunks with transfer ID, map epoch, deadlines and lower priority than live combat. |
| Cosmetic effects | Optional and expendable. They cannot determine damage or inventory. |

Reliability must not make a lost chat or loading message block fresh movement. Packet ACKs can help schedule messages, but reliable messages retain their own delivery identity and deduplication history. Handle reordered spawn/despawn and ownership messages with entity generations and revisions; bounded buffering or resynchronization handles missing dependencies.

Start with three scheduling lanes: expiring gameplay inputs/snapshots, reliable committed outcomes/session control, and reliable bulk state. Delivery ordering is independent between lanes; fresh unreliable gameplay must not wait for a missing reliable message. A lane is not an entity. The implemented replication milestone adds a fourth unordered, expiring snapshot-fragment lane: replaceable Gameplay dropped independent fragments, while reliable Bulk would retain obsolete transfers. Its shared congestion budget is tested; see NETWORK-REPLICATION.md. Reliable outcomes have explicit causal revisions, not assumed arrival order across lanes. This adopts the isolation lesson from Valve and ENet without copying their wire formats.

Aggregate messages produced in the same simulation step, then flush eligible combat/input traffic without adding a batching timer. This never bypasses congestion limits. Bound queue bytes, message count and age per lane; expire stale actions and replace superseded state before serialization. Schedule by deadlines and message importance within a shared connection budget. Cap urgent traffic so it cannot indefinitely starve state repair; pause bulk work or fail a transfer explicitly under sustained overload. Never silently turn an oversized unreliable update into reliable traffic. Record queued bytes, oldest-message age, retransmission bytes and drop reason.

Pace sends rather than bursting once per rendered frame. Measure RTT, RTT variation, loss and queue delay; use bounded bytes in flight and a documented congestion controller with conservative startup, loss/timeout backoff and controlled recovery. Prioritize control and combat, reduce expendable updates under pressure, and drop superseded snapshots. A fixed bandwidth cap alone is not congestion control. UDP stability and fragmentation requirements follow [IETF RFC 8085](https://www.rfc-editor.org/rfc/rfc8085.html); delivery design can draw on [Glenn Fiedler's message reliability explanation](https://gafferongames.com/post/reliable_ordered_messages/).

Use the NewReno-style controller and loss-recovery model described by [RFC 9002](https://www.rfc-editor.org/rfc/rfc9002.html) as the initial design reference. Phase zero must specify our adaptation: RTT sampling, loss versus reordering, probe timeout, recovery, idle restart and byte accounting. This is not a claim of QUIC compatibility or an implemented controller. All gameplay payloads, including unreliable snapshots and retransmissions, consume the shared congestion budget. Do not infer a complete congestion algorithm from a token bucket. The simulator must verify recovery and coexistence with competing traffic before internet deployment.

Security fixtures must cover process restart, reconnect, key/nonce separation, packet-number boundaries, replay-window arithmetic and forged ACKs. Successful authentication precedes advancing replay state. Reconnect creates a fresh authenticated session; it cannot revive stale actions or reuse a key/nonce pair. Verify secure randomness on each platform and fail closed when unavailable. Review any selected handshake as a complete protocol, including trust provisioning and restart behavior, not just encryption calls.

## Simulation and replication

Start with a proposed 60 Hz authoritative simulation and 60 Hz input stream, with approximately 30 Hz snapshots and prompt combat events. These are experiment settings, not established optima. Profile 60 versus 120 Hz only where the combat fixture demonstrates a reason; screen refresh remains independent at the player's selected rate. Collision sweeps and timestamped action edges preserve timing within ticks where supported.

Use a monotonic integer server tick and bounded fractional input time. Synchronize clocks with server measurements, clamp drift and time claims, and bound how much simulation time each peer can request. Packet timestamps are claims, not trusted evidence. A bad or asymmetric connection cannot be perfectly reconstructed from RTT alone. Handle overload with a bounded catch-up policy and explicit telemetry, without silently changing the match's simulation rate.

Predict the local player's movement and permitted action presentation. On correction, restore the acknowledged authoritative movement state and replay only unacknowledged commands. Extract a replay-safe movement path: replay must not award pickups, duplicate damage, fire script triggers or replay sound/rumble. Do not attempt deterministic rollback of the entire UnrealScript world. Cross-architecture floating-point agreement must be measured; server corrections remain necessary. [Prediction and reconciliation reference](https://www.gabrielgambetta.com/client-side-prediction-server-reconciliation.html).

Remote entities use a small adaptive interpolation buffer and bounded extrapolation during short gaps. Smooth visual corrections without moving collision through walls. Body and torso animation IDs, start times, rates, transitions and equipment attachments must agree with combat state. Record the interpolation delay used by each peer and constrain it server-side; it matters for historical contact evaluation. [Entity interpolation reference](https://www.gabrielgambetta.com/entity-interpolation.html).

Replicate attack ID, intent/start time, wind-up/active/recovery phase and cancellation promptly, independently of the next routine snapshot. Periodic state repairs lost event messages. Never speed through or skip wind-up merely to conceal a late update without measuring the defender's lost reaction time. Turning toward a previously off-screen opponent must reveal the correct attack phase; gameplay pose updates cannot be throttled just because that opponent was not rendered.

Assign network IDs independently of actor-array indices, using an entity generation and map epoch to reject stale references. Replicate explicit schemas for players, weapons, shields, gameplay-relevant limbs, projectiles, pickups, movers and match state. Send animation parameters instead of all bones each frame. Retain enough authoritative pose data locally for collision history. Blood and decorative debris can stay client-local; severed limbs that can be picked up or used as weapons cannot.

Use quantized transforms, change masks, dormancy for settled objects and spatial relevance with margins and hysteresis. Always include relevant combatants, incoming threats and their equipment even outside the camera. Do not equate rendering visibility with gameplay relevance. A 32-player pileup remains an all-relevant worst case and must fit budgets. Content manifests verify gameplay packages and collision data; optional cosmetic texture packs can differ where they do not affect gameplay.

The adapter runs game rules, damage, AI, inventory, cooldowns and match results on the server. Clients present authoritative actors and predict only supported local behavior. Explicitly suppress duplicated gameplay callbacks in client animation/state paths. Audit nested script calls as well as top-level events. Keep the original standalone path and save format intact during development; a separate multiplayer session cannot quick-load the host's campaign save or pause when someone opens a menu.

## Melee fairness

The server decides hit, block, damage, severing, knockback and stagger. Clients submit inputs, never final damage claims. Local movement and attack/block presentation respond without waiting for a round trip, but a predicted animation is not a confirmed result. Final health, death, inventory and strong impact feedback come from committed outcomes.

Maintain a short ring of immutable collision history: root transforms, body/torso animation phases, relevant joint volumes, shield pose and durability, weapon trajectory, attack ID, defense activation, life generation and moving blockers. Historical queries must operate on that history, not temporarily mutate live actors and trigger script callbacks. Store sampled collision shapes where pose reconstruction would depend on mutable dynamic-joint state. Static map collision can be shared.

Prototype two adjudication policies before selecting one:

1. **Conservative authority baseline.** Resolve valid input on the server timeline with no historical attack reach. This gives a clear correctness reference and quantifies the responsiveness cost.
2. **Bounded historical contact with defense preference.** Evaluate an approved attack trajectory against a constrained historical view, with the defender's valid shield timing checked under a separately documented defensive rule. For closely timed eligible defense, prefer the block. Enforce range, line of sight, shield facing, attack phase and cooldowns; a valid block still uses the existing small recoil for kicks and bashes.

For the second prototype, begin experiments with a 150 ms retained history, a maximum 100 ms historical query allowance and a 0 versus roughly 33 ms pending-outcome window. Retention, rewind allowance and finalization delay are different controls. They are candidate limits, not a promise to accept every input within 100 ms. Larger history does not grant a player more rewind.

These values are not validated by the external research. Ubisoft's historical 100 ms animation concealment and 33 ms attack-input delay are different mechanisms, chosen for For Honor's combat balance. Do not import either as an Open Rune default. Keep immediate local response as the baseline. If defenders consistently lose essential wind-up, separately compare zero, one and two simulation ticks of scheduled authoritative attack-start lead time, with an explicit local presentation policy; do not stack that experiment with hit-finalization delay unnoticed. Measure each delay separately and reject policies that make controls feel sluggish or visually contradict authoritative attack reach.

Defense preference is a bounded near-simultaneous tie rule to evaluate, not automatic acceptance of any late block. Specify eligibility before testing, including shield facing, activation interval and the commitment watermark. Measure whether it makes attacking ineffective as well as whether it prevents apparently unfair hits. Valve's inspected HL2 multiplayer bludgeon path rewinds around an instantaneous trace/hull test; Rune's continuous weapon sweeps and reactive shields require their own timeline specification.

The key unresolved policy is which attack view and defensive time can be combined consistently. Write a timeline specification with counterexamples before implementation. Do not independently rewind attacker and defender to convenient poses. Recheck dynamic cover and current life/attack generations. A late command that would invalidate already committed damage, a death or a weapon transfer must be rejected or handled by an explicitly designed rollback policy; version one will not undo arbitrary script side effects. Choose a finalization watermark and publish exact late-input behavior in the match rules. A pending hit may delay dependent authoritative stagger or death, which is part of the responsiveness cost to measure.

Resolve interactions once using attack ID, target life generation and contact identity. Define simultaneous strikes explicitly: allow a trade when both valid contacts precede interruption/death; never let container iteration order pick the winner. Shield durability and breakage must commit consistently with the corresponding hit. Repeated packets cannot apply another hit, bash or pickup.

Timestamp abuse, deliberate buffering and reordered commands must not gain reach, cooldown resets or a guaranteed retrospective block. The local host follows the same rules. Long-latency connections will still have disadvantages; neither higher tick rate nor rewinding makes two different views identical. [Lag compensation tradeoff reference](https://www.gabrielgambetta.com/lag-compensation.html).

## Implementation sequence and completion gates

The first **committed combat-result slice** carries bounded kick/bash decisions
over the reliable Control lane and observes their engine impact callbacks.
The follow-up adds life-safe client health application and a two-process
loopback duel using the real impact/damage scripts. See
[the current checkpoint](NETWORK-CLIENT-DUEL.md). Next,
complete the contract for rejected/cancelled actions, shield health, recoil and
cooldown state, and recovery after late or lost events. The client must apply
validated outcomes once without rerunning damage scripts. Independent wire
fixtures are necessary but do not satisfy the two-process or melee-playability
gates below.

| Phase | Deliverable | Gate before advancing |
| --- | --- | --- |
| 0. Contract and harness | Wire schema, sequence/ACK semantics, lane/queue limits, congestion adaptation, identity and trust model, security dependency selection, seeded packet simulator, combat timelines, counters and trace format. | Independently checked byte fixtures and expected timelines cover loss, replay/restart, expiry, resync, malformed data, visible wind-up and conflicting combat decisions. No gameplay implementation depends on an unspecified trust or timing rule. |
| 1. Portable transport | Two standalone processes exchange authenticated typed messages, clock samples and snapshots using our transport. | Android and macOS round trips; Windows/Linux build and codec fixtures when available; protocol bytes agree across platforms. Fuzz/sanitizer tests and long loss/reconnect runs have bounded memory and queues. Lack of a test platform is recorded, not called a pass. |
| 2. Multiplayer simulation boundary | Continuous headless server, fixed tick, independent render loop, per-player input sources, roles, stable IDs and explicit Rune adapters. | Two processes load the same existing arena; join, move, leave, pause menus and reconnect without a local viewport dependency on the server. Original single-player mechanics/save fixtures still pass. |
| 3. Two-player melee | Prediction, weapon/shield contact history, attack/block/kick/bash timing, damage, death, respawn, throw and pickup. | Selected fairness policy passes automated timeline cases and actual human duels under controlled network conditions. Every disputed hit has an inspectable reason. Do not expand the game-mode scope while this gate fails. |
| 4. Complete deathmatch | Scores, match rules, equipment ownership, late joins, map change, content mismatch handling and Android suspend/resume. | Repeated complete matches and map changes do not duplicate actors/items, strand players, accept stale-map packets or corrupt session state. |
| 5. Scale and internet operation | 4, 8, 16 then 32 clients, relevance/bandwidth optimization, dedicated hosting and operational diagnostics. | Active moving/attacking clients, crowded fights and reconnect storms meet the published budgets on stated hardware. A 32-connection idle test is insufficient. Reachability/relay work follows a separate deployment plan. |

## Test conditions and performance targets

Automate repeated input scenarios so validation does not depend on watching every duel in real time. Use seeded replays for transport behavior and logged authoritative outcomes; do not assume the full engine is bit-for-bit deterministic across platforms. Human tests judge whether discrepancies are understandable and whether the controls feel responsive.

Test a focused network matrix: 0, 40, 80 and 150 ms RTT; symmetric and asymmetric paths; 0, 10 and 30 ms jitter; 0, 1 and 3 percent random loss; short burst loss; duplication and reordering. Use harsher loss and multi-second stalls as recovery tests, not promised good-play conditions. Test clock drift, suspend/resume, expired actions, packet-number edge cases, snapshot baseline eviction, slow loading, server overload and disconnect during map travel.

Add bottleneck changes, competing traffic, ACK loss/compression, false loss followed by late delivery, and queue saturation. Saturate the bulk lane while attacking: input queue age must remain bounded, committed outcomes must remain deliverable, and buffers must not grow without limit. Test reconnect/server restart with captured old packets, malformed ACK ranges, invalid numbers and truncated fragments. Use explicit adversarial fixtures in addition to random fuzzing.

Combat scenarios include attack versus raised shield, last-moment shield raise, shield break, side/rear hits, dodge out of range, moving cover, simultaneous lethal hits, kick/bash block and stagger immunity, thrown weapon contact and two players claiming one pickup. Repeat representative cases with different render rates while keeping simulation rate fixed; this is a timing check, not a graphics-settings matrix.

Repeat buffered combos versus late button presses, attacks first seen after turning the camera, and a shield command arriving just before/after the commitment watermark. Measure visible wind-up from the defender's first displayed attack cue to the authoritative contact point mapped onto that presentation timeline. Record unavailable cues and late phase jumps instead of treating them as zero-latency success. Compare effective warning across latency conditions, not only agreement on final damage.

Initial engineering targets, all unmeasured:

- Two-player local attack/block presentation occurs on the next eligible rendered frame, without waiting for network acknowledgement. Measure total input-to-display separately from network confirmation.
- At 60 simulation ticks per second, target server p99 tick work below 12 ms under the declared load, leaving margin inside the 16.67 ms interval. Measure scheduling delay and overrun bursts as well as processing time.
- Begin with a transport/replication-only client CPU target below 1 ms p95 on the OPPO in the crowded fixture. Record prediction and collision-history costs separately and in the total frame budget; this does not promise the whole game reaches 144 FPS.
- Provisional steady-state per-client budgets: 20 KB/s upstream and 80 KB/s downstream, including protocol overhead but excluding initial loads and relay encapsulation. At 32 clients the downstream allocation is about 2.56 MB/s server egress before IP/link overhead. Measure before tightening; these are budgets, not benchmark results.
- No duplicate committed damage, pickups, scores or spawns under the defined impairment tests. Recoverable loss converges to authoritative state; unrecoverable sessions disconnect with a clear reason and bounded resource use.
- Use a four-hour active-client soak and repeated map changes/reconnects for an initial stability gate. It supplements fuzzing, lifetime checks and human tests; it does not prove all defects absent.

Log input sequence and mapped time, server tick, attack ID, chosen history time, shield phase, collision result, commit time, correction size and rejection reason. Display RTT, jitter/loss, simulation overruns and bandwidth in a developer panel. Compare both clients' views with this timeline when assessing fairness.

AI can assist implementation and test generation, but passing tests generated from the same mistaken assumption is insufficient. Freeze reviewed contracts and independently calculated packet/timeline fixtures first. Use an independent minimal decoder/model, mutation tests that deliberately break invariants, sanitizer/fuzz runs and reproducible impairment seeds. Any future reference-transport benchmark must use equivalent payloads, send rates and conditions; it measures transport behavior, not Rune combat fairness. GitHub source and CI definitions are useful precedents, not substitutes for running our own cross-platform and active-client tests.

Future Android graphics checks use native resolution and 4x AA with the user's current refresh/FPS settings preserved. Measure actual presentation separately from network tick performance. Preserve campaign saves, the installed application identity and unrelated device configuration.

## Decision

Proceed first with the protocol contract and executable test harness, then prove the engine simulation boundary and a two-player melee slice. Transport correctness and combat fairness are separate gates. A lean custom implementation is feasible, but the primary engineering risk is integrating authoritative, replay-safe timing with Rune's script and animation-driven combat. Do not promise 32-player stability or a delivery date until that two-player slice establishes the cost and behavior.
