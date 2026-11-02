# Open Rune input, action and world replication

Current integrated implementation: [private multiplayer completion](PRIVATE-MULTIPLAYER-STATUS.md), protocol/ALPN **17**, native rules **17**, raw snapshot **6**. The earlier checkpoint versions below are historical.

Latest mode checkpoint: [Team Deathmatch](NETWORK-TEAM-DEATHMATCH.md), protocol/ALPN 12 and native rules 12. Earlier versions below are historical.

Current protocol/ALPN: **11**. [Four-player Deathmatch and map epochs](NETWORK-DEATHMATCH.md) supersede the historical versions below. Snapshot payload stays at version 5.

Latest October 26 checkpoint: [interactive private multiplayer](NETWORK-INTERACTIVE.md)
adds rendered clients, Android join/leave/respawn menus and protocol 10. Earlier
non-playable/protocol-version statements below describe historical milestones.
See that checkpoint for exact runtime validation and remaining limits.
The [responsiveness follow-up](NETWORK-RESPONSIVENESS.md) adds grounded prediction,
pipelined small snapshots and the user-selected 33 ms defense window. Full attack
rewind and the human fairness gate remain unfinished.

October 26, 2026. Implemented in the standalone `Network/` library, on top of
`SecureSession`. This is a tested networking contract and harness, **not playable
multiplayer or engine integration**. The campaign, installed APK, saves and
rendering settings are unchanged.

Later on October 26, an opt-in engine boundary was added and multiplayer work
was then paused by the user. See [the checkpoint](NETWORK-ENGINE-CHECKPOINT.md).
The results below remain evidence of the standalone replication milestone.
The resumed combat-result slice advanced the protocol to version 3. The next
client duel slice advances it to version 4 with snapshot life generations; see
[its checkpoint](NETWORK-CLIENT-DUEL.md). Version 2 below is the historical
replication milestone.

## Connection and delivery

Inner protocol version **2**, DTLS ALPN **`open-rune-net/2`**. Older experimental
builds are intentionally incompatible. Authentication, fresh session identity,
world epoch and clock synchronization still precede replication. A reconnect or
map change requires new session and replication objects; old packets cannot
carry actions into the new world.

The fourth lane, `Snapshots`, is unordered and expires. This is a necessary split:
`Gameplay` replaces old state with the latest record, so putting independent
snapshot fragments there discards earlier pieces. `Bulk` waits for missing
reliable records, so it would retain obsolete snapshot transfers. Tests deliver
snapshot fragments in reverse order while preserving both pieces. Inputs and
admission feedback continue on `Gameplay`; reliable `Control`/`Bulk` remain
independent and available for future committed outcomes and assets.

Snapshot fragments share the connection's congestion and pacing limits. The old
1,800-byte per-lane limit often allowed only one large fragment in flight; the
new snapshot lane can use up to three quarters of the congestion window,
bounded between 1,800 and 9,600 bytes and always constrained by the global
window. Reliable lanes retain their previous limits. The congestion algorithm
is still the earlier experimental controller, not a completed internet transport.

## Input and action contract

One authenticated peer is bound by the future server adapter to one controlled
pawn. Clients send intent, never positions, hits, damage or inventory changes.
Input records contain a sequence, original local timestamp, life generation,
movement axes, view angles and complete held attack/block/crouch state. Axes are
bounded to -1000..1000 and pitch to +/-90 degrees. The engine must normalize
combined movement and advance simulation on the server tick; clients cannot
request a simulation duration.

A batch holds at most eight inputs and four actions. The sender retains 32 recent
inputs and up to 32 pending actions, keeping each action's original input context
even if newer input replaces the ordinary history. Actions cover attack, shield
raise/lower, kick, bash, use, throw and jump. Each has an independent monotonic
ID, input reference, timestamp and life generation. An action repeats with its
original identity until acknowledged or too old to send.

The receiver maps **nested input/action timestamps**, not only transport headers,
through the authenticated clock mapping. Commands older than 250 ms, future
claims outside the clock mapper's uncertainty, wrong lives, missing input context,
excessive sequence jumps and conflicting copies of an input are rejected.
Complete held state repairs lost releases; it becomes neutral after 250 ms of
silence. Life changes clear held state and queued actions without clearing action
deduplication. Life generations cannot regress or wrap in the same connection.

Action admission uses a 256-ID sliding decision window, bounded command/results
queues, and a burst-eight / twenty-per-second admission budget. IDs below the
window cannot become new actions after their original decision is evicted.
Duplicate requests return the original decision. These limits are defensive
prototype policy, **not the final Rune combat cooldowns**.

Three acknowledgements have distinct meanings:

1. Packet receipt: transport retained a message.
2. Action admission: the server accepted a command for its game adapter.
3. Snapshot installation: the client explicitly installed an entire world state.

Admission is not a committed hit, pickup or damage result. `highestInput` reports
accepted input, not a physics simulation/reconciliation watermark. The future
engine adapter must validate ownership, movement, cooldowns, inventory, attack
phase and collision and replicate authoritative committed outcomes separately.
`drainActions(now)` removes actions that expired while waiting for the adapter.

After one second without an admission decision, the sender exposes the action as
**unconfirmed**, stops retaining it as pending, and never invents a rejection.
The last 64 timed-out identities can still resolve if a late genuine decision
arrives. Result and notification queues are bounded to 64; the caller must drain
them regularly. Under a complete 250 ms blackout, an action may deliberately not
execute. Expiry cannot guarantee delivery, and delivery cannot justify a stale hit.

## World snapshots

The bounded schema supports up to **512 entities**: pawns, weapons, shields,
projectiles, pickups, movers and limbs. IDs include a generation. Entity records
carry health, flags, quantized position/velocity/orientation, body and torso
animation indices, animation phase/rate, ownership and equipped weapon/shield
references. They do not serialize pointers, UnrealScript objects or original
engine network channels.

Each entity is 68 bytes: position in eighths of a Rune unit, velocity in quarters
of a unit per second, angles/phase in 1/65536 turns/cycles, animation rate in
1/256 cycles per second. Positions are bounded to +/-2^27 quantized units.
The future adapter must reject nonfinite/out-of-range floats before conversion,
and establish a shared animation/asset manifest; numeric animation indices alone
are not a negotiated asset catalog.

A full 512-entity snapshot is **34,846 bytes / 55 fragments**. A snapshot carries
its ID, baseline ID and authoritative server tick. Deltas send changed complete
entity records plus exact-generation removals, against only an explicitly
installed baseline. One changed entity costs 98 bytes before fragment framing.
This is structural delta compression, not yet field masks or entropy compression.

Validation rejects unsorted/duplicate identities, broken generation references,
invalid equipment types, self references, owner cycles, generation rollback
against the baseline/current world, and unversioned type changes. Valid pawn to
weapon and weapon to owner references are allowed. A future server ID allocator
must retain generations across despawn/reuse; this layer cannot infer the history
of an ID absent from every retained snapshot.

Parts carry immutable snapshot metadata, a canonical index/count and up to 640
bytes. No IP fragmentation is required. The receiver holds at most one 40,000-byte
assembly, one decoded candidate and eight installed baselines. Conflicting copies,
metadata mismatches, impossible lengths and missing baselines fail closed.
Assemblies and unacknowledged sender transfers expire after ten seconds. The
larger deadline avoids repeatedly throwing away a nearly complete large world
under the harness's loss and congestion recovery; memory remains bounded.

A complete decoded candidate is **not automatically installed**. The caller must
atomically apply it, then call `commit(id)`. Only then does feedback report it as
installed. Received-fragment masks request missing pieces but never authorize a
delta baseline. If installation fails, call `resync()`; preserve the last installed
world and request a complete replacement. Do not hold a candidate pointer across
receiver mutations.

The sender keeps one immutable active transfer and eight recent snapshots,
coalescing newer world captures into one desired state. It resends missing parts,
then moves to the latest desired world after installation acknowledgement. If a
baseline is unavailable or the transfer times out, it sends a fresh full snapshot.
A client does not send game commands before installing its initial world; the
server requires an acknowledged initial snapshot before admitting them.

## Integration order

`ReplicationPeer` exclusively consumes the session's Gameplay/Snapshots lanes.
Drive `SecureSession::step(localMonotonicMs)`, then `ReplicationPeer::step()`;
send returned datagrams through the existing socket adapter. Drain admitted
commands with current server time, and explicitly commit client world candidates
after application. Capture authoritative world changes independently of display
refresh. Keep one owner thread per session. Do not mix another consumer of
`SecureSession::drain()` with this wrapper; unknown message types are rejected.

Next work is the fixed server-tick/engine boundary: owned pawn input sources,
quantization/asset-ID adapters, authoritative snapshot installation, committed
action results, movement prediction/reconciliation and remote interpolation.
No claim is made yet about melee fairness, multiplayer FPS, 32-player bandwidth,
interest management, player discovery or campaign networking.

## Verification

See `evidence/network-replication/` for the final test logs and source manifest.
The suite covers canonical bytes with an independent Python writer, malformed
and truncated messages, duplicate/stale/wrong-life actions, held-state expiry,
late admission replies, adapter stalls, rate limits, ID windows, missing fragments,
explicit installation, unavailable baselines, cache bounds, ownership/generation
checks and full/delta reconstruction.

The authenticated in-process simulation uses different peer clock origins,
10–44 ms delay each direction, 5% random loss, duplication/reordering and a 250 ms
bidirectional blackout, for ten deterministic network seeds. It checks a
512-entity bootstrap, subsequent changed world, no double admission, bounded
queues and terminal action reporting. Initial-world transfer time includes
handshake, clock acquisition and a deliberately large full snapshot; it is not a
measured gameplay latency. Test-only loopback processes still exercise the
underlying authenticated transport separately.

Android ARM64 cross-compilation is not Android runtime validation. No tablet
installation or live multiplayer test is part of this milestone. Windows, Linux
and iOS runtime behavior remains unverified.


Final local results: all **9/9 CTest suites** passed in both Release and
AddressSanitizer/UndefinedBehaviorSanitizer builds. The independent writer checked
**1,165 valid / 2,336 invalid** replication records, including a full 512-entity
world. The final fuzz runs completed **20,000 replication + 10,000 transport**
inputs without a reported failure; all six deliberate safety-rule mutations were
detected. Fuzzing is bounded evidence, not a proof of absence of bugs.

Across the ten impaired authenticated scenarios, **290 actions were submitted,
289 admitted once, and one unconfirmed** during the deliberate blackout. All
clients installed the final changed world. The 512-entity bootstrap took
**1.470–8.940 seconds of simulated time**, including setup. That remains an
optimization target before claiming fast large-world joins; current congestion
recovery and whole-record deltas have not been tuned for a real match.

The release loopback run exposed a harness shutdown race: one completed peer
could receive a socket refusal from a clock probe after the other process closed.
The harness now permits that refusal only after proving all 100 records received
and all outgoing records acknowledged; both processes must independently report
success. Asymmetric shutdown timing exercises this path.
