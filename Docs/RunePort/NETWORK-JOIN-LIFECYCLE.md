# Match agreement and actor lifecycle checkpoint

October 26, 2026. This is an opt-in desktop test milestone for Open Rune's
own multiplayer protocol. It is not a playable mode. Normal Android builds
still have `OPENRUNE_ENABLE_NETWORK=OFF`; no multiplayer APK was installed.

## Implemented

- Experimental protocol **8** and DTLS ALPN `open-rune-net/8`. Authenticated
  peers exchange an exact match contract before either side handles snapshots
  or gameplay input. It includes map name, 60 Hz simulation, player limit,
  rules revision, a SHA-256 digest of the installed `System/*.u` gameplay
  packages and a digest of the selected map package. Cosmetic texture packs
  are outside this check. A different client map digest blocks the join in
  the standalone secure-session test. The C++ combat rules revision is
  currently maintained manually; changing gameplay code requires updating it.
- Reliable spawn/despawn records carry generation, actor kind, package, class,
  and optional animation-proxy class. A snapshot waits until its referenced
  actors exist. The two-process test now creates two Ragnar characters, two
  handaxes and a Viking shield on the client from the server's records. The
  shield's ownership link is checked after installation. Retired replicas
  leave the level actor list without running destruction scripts; old
  snapshots cannot move them.
- Network admission is followed by a separate engine decision: Started,
  ExecutionRejected, or Cancelled. Decisions are retained until the reliable
  control lane accepts them. Interrupted kick/bash moves cancel their pending
  action association. A Started decision still does not claim a contact;
  the later `CombatOutcome` commits hit, block or miss.
- Combat and actor records stay in the authority queue until every relevant
  peer has accepted them into its reliable transport. The two-client desktop
  probe verifies both receive the actor records and combat result.
- Multiplayer replicas use the same interpolating Rune skeleton renderer as
  single-player characters. Snapshot payload version **3** carries separate,
  signed body and upper-body animation phases and rates, plus loop flags.
  Presentation interpolates both clocks between snapshots and preserves the
  source pose on a sequence transition. The desktop probe checks Ragnar's
  two animation channels, a GoblinWarrior's phase, and a signed tween-in.
  A full 512-entity snapshot is now **38,942 bytes in 61 fragments**.
  The authority discovers visible authoritative pawns of any class, then
  their equipment; hidden state is carried for actors already known to the
  client. Installing a later snapshot begins at the previous pose, avoiding
  a forward snap followed by a backward jump on the next render frame. In a
  two-client loopback test, each headless client called `present(alpha)` over
  **1,150 times** at a simulated 120 Hz between roughly 30 Hz snapshots;
  **129** and **214** intermediate moving poses matched the interpolation
  calculation. The future
  interactive client loop must call it on each real render frame; this test
  does not measure an on-screen multiplayer animation on Android.
- Client combat dependencies use a 64-result queue. A missing future-life
  snapshot requests a full resynchronization after 10 seconds and becomes a
  fault after 20 seconds, rather than growing an unbounded backlog.

## Observed tests

- Standalone networking suite: **10/10** groups passed, including independent
  wire fixtures, secure loopback, match mismatch and a bounded soak.
- The normal Android engine core (`OPENRUNE_ENABLE_NETWORK=OFF`) and the
  standalone ARM64 networking library cross-build both passed. Neither is an
  Android multiplayer runtime or APK test.
- Desktop engine probe: **480** fixed 60 Hz ticks, **360** installed world
  snapshots, both owned pawns moved, one committed combat result reached both
  clients, and an old snapshot was safe after replica removal. It also checks
  body/torso and monster interpolation and bounded combat dependency handling.
- Separate server/client processes on IPv4 loopback: kick and shield bash
  contacts arrived from Rune's normal animation callbacks. Server health went
  **100 → 93 → 90**, client matched **90**, and both Started decisions arrived.
  The server applied continuous movement and the client received changing
  position snapshots. One repeat with **50 ms one-way delay and 3% packet
  loss** also passed. Logs: [server](evidence/network-process-duel/natural-moving-server.log),
  [client](evidence/network-process-duel/natural-moving-client.log). These
  are scripted, deterministic inputs with a public test-only key and an
  ephemeral loopback port; no human combat or presentation was measured.
- A second remote client independently owned and moved the other pawn in a
  three-process fixture with **50 ms one-way delay and 3% packet loss**. Both
  clients received the two combat results, five actor records, changing world
  snapshots and intermediate rendered positions. Logs: [server](evidence/network-process-duel/dual-interpolation-server.log),
  [first client](evidence/network-process-duel/dual-interpolation-client.log),
  [second client](evidence/network-process-duel/dual-interpolation-client-two.log).

Run the moving fixture with isolated saves:

```sh
python3 Tools/RuneValidation/network_process_duel.py --natural --two-clients \
  --latency-ms 50 --loss-percent 3 \
  --binary build/SurrealEngine.app/Contents/MacOS/SurrealEngine \
  --game /path/to/owned/rune-classic \
  --output /path/to/private/test-output
```

## Remaining before a playable match

The fixture scripts both remote clients' inputs. It lacks a continuous
interactive client/server game loop, invitation provisioning, a non-loopback
listener, movement prediction/correction, death/respawn and pickup ownership,
scores/map travel, bounded client presentation dependencies, and Android
suspend/reconnect handling. Combat fairness needs measured wind-up and shield
timelines under latency with real users. Neither this run nor the fixed 60 Hz
server loop demonstrates 32-player capacity or a client FPS target.
