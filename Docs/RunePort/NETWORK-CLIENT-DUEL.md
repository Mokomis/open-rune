# Client combat application and two-process duel

October 26, 2026. This follows the [first combat-outcome checkpoint](NETWORK-COMBAT-OUTCOMES.md).
This page records the original protocol 4 duel. The later
[join and lifecycle checkpoint](NETWORK-JOIN-LIFECYCLE.md) adds match agreement,
equipment actors, natural impacts and movement.
Open Rune multiplayer remains an opt-in experiment, not a playable mode. Normal
Android builds still have `OPENRUNE_ENABLE_NETWORK=OFF`; no APK was installed and
normal game saves were not used.

## Implemented

- Protocol **4** and DTLS ALPN `open-rune-net/4` add a nonzero life generation
  to every snapshot entity. The snapshot payload version is **2**. The previous
  experimental clients are intentionally incompatible. A full 512-entity world
  is 36,894 bytes in 58 fragments; a single changed entity plus snapshot header
  is 102 bytes. The decoder rejects zero or regressing lives.
- `RuneNetworkReplica::applyCombat` validates a confirmed result against the
  installed attacker and target entity references and life generations. It
  writes target health directly without invoking `JointDamaged`, physics or
  another damage script. Duplicate results are consumed once. A result for a
  future life waits for its snapshot; a newer snapshot/life supersedes a late
  result. A confirmed health value is retained over older snapshots until a
  snapshot at or after its server tick replaces it.
- A test-only two-process fixture runs the real Rune engine in each process and
  sends authenticated datagrams over IPv4 loopback. The server loads DM-Hildir,
  creates two owned Ragnar pawns, receives kick and bash intents, invokes their
  actual impact callbacks and damage scripts against the second pawn, and sends
  confirmed outcomes. The headless client installs two known pawn replicas and
  applies those outcomes. The fixture triggers animation impact callbacks at
  controlled times for deterministic tests; it does not validate natural
  animation timing or a human-playable input/render loop.

## Observed result

Two separate runs passed. On each, the server recorded a kick reducing the
target to **93** health and a bash reducing it to **90**. The client received
both results in order and converged to **90** health. The server and client
logs from the first run are [here](evidence/network-process-duel/server.log)
and [here](evidence/network-process-duel/client.log). The test uses separate
temporary save directories, an ephemeral loopback UDP port and a public fixture
key; it never listens on a non-loopback interface.

The full standalone networking suite passed **10/10** groups, including the
independent replication wire oracle and DTLS loopback. The opt-in desktop
engine probe passed client overlay and stale-life checks. The normal Android
engine core and ARM64 standalone network library cross-builds passed; no
Android runtime networking test was done.

## Remaining work

This fixture knows only two Ragnar pawn archetypes and strips equipment links
from its snapshots. Production joining still needs content/rules agreement,
authenticated session provisioning, actor spawn/despawn and equipment mapping,
continuous server/client game loops, movement prediction/correction, real
animation timing, client presentation of stagger and feedback, reconnect/map
travel, and latency/fairness tests. A server-driven impact test is evidence of
the real damage code and client state path, not proof that combat feels fair
under latency or that 32 players can join.

Run the focused test with:

```sh
python3 Tools/RuneValidation/network_process_duel.py \
  --binary build/SurrealEngine.app/Contents/MacOS/SurrealEngine \
  --game /path/to/owned/rune-classic \
  --output /path/to/private/test-output
```
