# Open Rune combat outcome checkpoint

October 26, 2026. This is the first committed-result slice for Open Rune's own
multiplayer protocol. The original Rune server protocol is not used. This is
an opt-in desktop engine and standalone network milestone, **not playable
multiplayer**. The installed Android application and normal saves were untouched.
The later [client duel checkpoint](NETWORK-CLIENT-DUEL.md) advances the protocol
to version 4 and supersedes this page's "next gate" status.

## What is implemented

- Inner protocol **3** and DTLS ALPN `open-rune-net/3` reject the previous
  experimental version. A new 62-byte, epoch-bound record describes a server
  decision for a kick or shield bash: monotonic result ID, server tick, optional
  player action ID, attacker/target entity and life identities, miss/block/hit,
  resulting target health and whether a bash caused stagger. Misses have no
  target. These are explicit, bounded fields; no engine pointers or script
  objects go over the wire.
- The opt-in server adapter can publish results on the reliable Control lane;
  the desktop fixture drives that API. A failed enqueue does not consume the
  result ID; the caller must retry. The client accepts
  in-order results once per authenticated session. A malformed result, gap or
  undrained 64-result queue marks the stream faulted and requests a full world
  snapshot. The game must disconnect or explicitly recover that peer; a
  snapshot repairs persistent state, not every transient cue.
- The opt-in engine authority observes the actual kick/bash impact callbacks,
  including misses. It assigns stable entity references and records post-impact
  health and stagger, associating player-initiated moves with admitted action
  IDs when available. NPC moves use action ID zero. The observer is attached
  only while the authority exists; ordinary single-player behavior is unchanged.
  Contacting actors reserve their references before damage scripts run, so a
  target marked for deletion by a hit retains its result identity.
- The desktop fixture loads the existing DM-Hildir arena with two server-owned
  pawns, invokes the impact observer without applying synthetic damage, and
  verifies that two authenticated client sessions each receive exactly one
  identical committed record. This validates the observer/transport path. It
  does **not** validate collision fairness or actual kick/bash hits under latency.

## Evidence and next gate

The standalone 10/10 CTest groups pass, including seeded loss, duplication and
reordering, the independent Python wire oracle and DTLS loopback. A separate
64-result queue overflow test faults and clears the stream. The replication
fuzzer completed 29,877 runs in a 10-second session without a finding. The
opt-in desktop engine build and isolated-save two-client probe pass. The normal
Android engine-core target (networking off) and ARM64 network-library cross-build pass.
No Android runtime, two-process game session, real duel or 32-player result has
been measured.

Next, the client game adapter must apply validated outcomes once without
rerunning damage scripts, reconcile them with snapshots and display corrections.
Actor spawn and snapshot dependencies must be resolved before applying an
outcome for a newly seen target; the current adapter does not guarantee this.
Then the server must drive real kick/bash impact fixtures through the network
observer (blocked and unblocked, player and NPC), followed by two-process
gameplay and fairness tests. The current result record deliberately does not
encode a client physics impulse: authoritative movement comes from snapshots.
Shield health and cooldown state still need explicit replication before the
full combat contract is complete.
