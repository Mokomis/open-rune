# Open Rune experimental protocol and harness

Current integrated implementation: [private multiplayer completion](PRIVATE-MULTIPLAYER-STATUS.md), protocol/ALPN **17**, native rules **17**, raw snapshot **6**. The earlier checkpoint versions below are historical.

Latest mode checkpoint: [Team Deathmatch](NETWORK-TEAM-DEATHMATCH.md), protocol/ALPN 12 and native rules 12. Earlier versions below are historical.

Current protocol/ALPN: **11**. [Four-player Deathmatch and map epochs](NETWORK-DEATHMATCH.md) supersede the historical versions below. Snapshot payload stays at version 5.

Historical foundation contract. Version 2 added the unordered expiring snapshot
lane and typed replication; version 3 added committed kick/bash results;
version 4 adds snapshot life generations and client result application. The
current experimental protocol is **10**, adding match agreement, actor lifecycle
records, reliable engine action decisions and independent signed body/torso
animation phases, equipment attachment joints, respawn, graceful close and processed movement checkpoints. See [responsiveness](NETWORK-RESPONSIVENESS.md) for snapshot version 5 and the native defense-window policy. See
[the interactive checkpoint](NETWORK-INTERACTIVE.md) and
[NETWORK-REPLICATION.md](NETWORK-REPLICATION.md),
[NETWORK-COMBAT-OUTCOMES.md](NETWORK-COMBAT-OUTCOMES.md) and
[NETWORK-CLIENT-DUEL.md](NETWORK-CLIENT-DUEL.md) and
[NETWORK-JOIN-LIFECYCLE.md](NETWORK-JOIN-LIFECYCLE.md).

October 25, 2026. First implemented foundation from [the multiplayer plan](MULTIPLAYER-PLAN.md).
This contract describes the code in `Network/`, not a released multiplayer protocol.
October 26 update: the inner packet format below is unchanged. The new
[DTLS session and clock mapping](NETWORK-AUTH-CLOCKS.md) supersede this document's
original authentication/shared-clock limitations. The original foundation and
its remaining delivery/congestion limitations are retained below for context.
The game and Android application are not linked to it. Phase zero as a whole is
not complete: authentication integration, clock policy and combat timelines
remain separate gates. No multiplayer performance claim follows from these tests.

## Inner packet format, version 1

All integers are unsigned, big-endian. No native structs or floating-point values
are serialized. The maximum plaintext packet is 1,100 bytes, provisionally
reserving 100 bytes within a 1,200-byte datagram for security framing. The secure
adapter must validate actual overhead before that becomes a deployed limit.

| Offset | Bytes | Meaning |
| --- | ---: | --- |
| 0 | 4 | ASCII `ORN1` |
| 4 | 2 | Protocol version, exactly 1 |
| 6 | 2 | Reserved, must be zero |
| 8 | 8 | Nonzero session identifier, supplied by the future authenticated handshake |
| 16 | 4 | Nonzero map/session epoch |
| 20 | 8 | Packet number, 1 through `UINT64_MAX-1`; never wraps |
| 28 | 1 | ACK range count, 0–8 |
| 29 | 1 | Message count, 0 or 1 in this first version |
| 30 | 2 | Exact bytes after the 32-byte header |

Each ACK range is two 64-bit inclusive endpoints, first then last. Both are
nonzero and at most `UINT64_MAX-1`, with first <= last. Ranges descend, never
overlap and are not adjacent; adjacent ranges must be merged. ACKs for packet
numbers the endpoint has not emitted are rejected. The range count bounds parse
work, even if a range describes a large interval; processing iterates the bounded
sent-packet map rather than every number in the range. There is no peer-reported
ACK-delay field in this version.

If present, the message follows all ACK ranges:

| Relative offset | Bytes | Meaning |
| --- | ---: | --- |
| 0 | 1 | Kind: 1 state, 2 event, 3 chunk |
| 1 | 1 | Lane: 0 gameplay, 1 control, 2 bulk |
| 2 | 2 | Reserved zero |
| 4 | 8 | Lane-local message identity, 1 through `UINT64_MAX-1` |
| 12 | 8 | Creation time in the shared test clock, milliseconds; excludes `UINT64_MAX` |
| 20 | 2 | State lifetime 1–1,000 ms; exactly zero for reliable event/chunk |
| 22 | 2 | Payload length 0–768 bytes, followed by exactly that many bytes |

Only state/gameplay, event/control and chunk/bulk pairings are valid. Unknown
versions, kinds, lanes, reserved bits, trailing bytes and incorrect lengths fail
closed. Failed encode/decode leaves the caller's output unchanged. A valid codec
packet can still fail session checks; syntax is not authority.

Creation times currently assume a shared monotonic test clock. A future clock
adapter must map remote input times into a bounded server timeline before use;
this implementation is not suitable for unsynchronized real peers. Backwards
local time and times without 60 seconds of unsigned arithmetic headroom are
rejected. Future creation times cannot mutate receive or ACK state.

## Receipt, ordering and resource limits

- Each direction has its own monotonically increasing packet sequence. A
  1,024-packet replay window accepts unseen reordered packets within its window.
  Zero, exhausted, duplicate and too-old numbers never apply data again.
- Retained data packets generate up to eight recent ACK ranges. Pure ACK packets
  do not elicit ACKs. Repeated already-received data prompts another ACK, bounded
  by the 10 ms ACK-only interval. Data sends may piggyback ACKs.
- Retransmission uses the same reliable message identity with a fresh packet
  number. Late originals cannot reapply an event. In this initial implementation,
  timed-out packet tracking is discarded: an ACK for that old attempt may not
  retire its message, so a fresh attempt/ACK can still be needed.
- Control and bulk each allow 64 pending sends and a 64-ID receive window.
  Reliable delivery is ordered independently per lane. A missing control record
  cannot block gameplay or bulk. Records beyond the receive window receive no
  data ACK; the sender retries after the receiver drains earlier records.
- Gameplay has one queued send and one pending receive. Newer state replaces
  older state; expiry is checked before send, on receive and when drained.
  Newer expired state invalidates older pending state. This is not action-edge
  delivery, snapshot installation acknowledgement or delta-baseline management.
- Pending records are at most 129 per direction; at most 256 data packets are
  tracked in flight. Each payload is at most 768 bytes. Container overhead is
  bounded too, but exact memory cost is platform dependent and unbenchmarked.
- Reliable records are never silently discarded for age. At 30 seconds without
  completion, polling marks the session failed; application delivery stops. The
  owner must then destroy/reconnect the session. No automatic replay into a new
  session is allowed. A different session or epoch rejects captured old packets.
- `drain()` transfers data ownership to the application. Retained reliable data
  may already be ACKed before the application drains it. No ACK means a combat
  decision has committed or a fragmented world snapshot has been installed.

One owner calls all methods. No thread safety or shared actor access is implied.
The network simulator has a separate 4,096-datagram limit and a fixed PRNG;
neither is a production network or security component.

## Experimental scheduling and congestion response

The current weighted cycle is gameplay, control, gameplay, bulk, skipping lanes
without eligible data. This gives reliable repair a turn even during continuous
state generation. Gameplay expires instead of creating a stale backlog. The
two reliable lanes each have a provisional 1,800-byte in-flight cap, including
the conservative envelope reservation. This limits how much of the path a
reliable burst can occupy before the next gameplay update arrives. It is an
application scheduling cap in addition to the shared congestion window, not
a substitute for congestion control or a measured final bandwidth allocation.
The constrained fixture showed a substantial improvement but still drops many
expired state updates; low-latency internet gameplay is not yet established. The
first contract sends one message per packet; multi-message aggregation is a
future efficiency change requiring a new contract/test revision.

All data attempts, including unreliable state and retransmissions, reserve
plaintext bytes plus 100 envelope bytes in the congestion budget. ACK-only
traffic is rate-limited separately. Initial congestion window is 12,000 bytes;
minimum is 2,400 and maximum 307,200. A weighted RTT estimate starts at 100 ms.
The current retry timer is four times that estimate, clamped to 200–2,000 ms.

Timeout halves the window once per recovery flight; acknowledged packets sent
after that flight permit growth. Growth starts with acknowledged byte counts,
then uses approximately one 1,200-byte packet per acknowledged window. Pacing
spaces data attempts by bytes × RTT / window, with a minimum 1 ms interval and
no accumulated idle burst credit. This is a bounded experimental NewReno-style
baseline, **not a complete RFC 9002 controller**. RTT variance, ACK-delay
correction, packet/time-threshold loss, exponential probe backoff, persistent
congestion, app-limited growth and path changes still need specification and
tests before internet use. Do not promote this baseline to production because
the simulator passes.

## Original authentication boundary (superseded October 26)

No new cryptography is implemented. Only `receiveTrusted` mutates session state;
its caller must eventually provide authenticated plaintext from a vetted secure
datagram layer. The only socket adapter is a test executable bound exclusively
to IPv4/IPv6 loopback; there is no public unauthenticated listener. Its fixed
session identifier and shared machine-clock origin are test fixtures.
Session/epoch checks and the replay window do not
authenticate a sender.

Mbed TLS DTLS is the next dependency candidate to validate. Its [official DTLS
tutorial](https://mbed-tls.readthedocs.io/en/latest/kb/how-to/dtls-tutorial/)
documents handshake timers and stateless address-verification cookies. Keep
cookie verification and anti-replay enabled. Select/pin a supported release,
review its license/configuration, build on our targets, and test wrong identity,
wrong key, tampering, restart and handshake loss before connecting it. Trust
provisioning, fresh session/key material, secure randomness and connection limits
must be explicit. No dependency has been imported or cryptographic behavior
claimed tested in this milestone.

## Verification and reproduction

Commands are in [Network/README.md](../../Network/README.md). CTest separates:

1. Contract boundaries, atomic decode failure, replay arithmetic, stale state,
   duplicate event suppression, fresh retry packet numbers, ACK loss, unsent
   ACKs, session/epoch changes and full receive-window backpressure.
2. Reproducible two-way simulated links with asymmetric delay, jitter, loss,
   duplication, bandwidth serialization and a burst outage. Each scenario
   delivers 400 ordered reliable records alongside expendable state updates.
3. One hundred seeded impaired scenarios. This is a virtual-time foundation
   soak, not four hours of real clients or a game benchmark.
4. Python `struct` encoding independent of the C++ codec: one golden packet,
   1,000 randomized valid packets and malformed fixtures.
5. Separate native processes exchange 100 ordered reliable records in each
   direction over actual IPv4 and IPv6 loopback UDP sockets. This establishes
   local datagram integration, not authentication, internet reachability or
   synchronization of independent machine clocks.

The fuzz target checks canonical decode/encode and bounded stateful operations.
Mutation smoke tests deliberately break magic validation, duplicate rejection,
queued-state expiry and reliable-lane credit limits in temporary source copies;
the contract/impairment suites must catch each. The state-delivery regression
floors in the burst fixture are not gameplay latency targets. Assertions remain
active in optimized builds.

Execution results and remaining work are recorded in
[NETWORK-VALIDATION.md](NETWORK-VALIDATION.md). Desktop runtime evidence and
Android cross-compilation are reported separately. No FPS, campaign or
32-player capacity claim is made.
