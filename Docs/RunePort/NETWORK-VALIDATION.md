# Open Rune network foundation validation

October 25, 2026. Standalone implementation in `Network/`; no game code is linked
to it. See [protocol and limitations](NETWORK-PROTOCOL.md) and
[reproduction commands](../../Network/README.md).

This is the historical foundation snapshot. The October 26
[authentication/clock milestone](NETWORK-AUTH-CLOCKS.md) supersedes its statements
that those components are absent. Its source hashes describe the earlier revision.

## Implemented

- Canonical big-endian packet codec with bounded lengths/counts and atomic failure.
- Packet replay window and selective receipt ranges, independent reliable message
  identities, retry deduplication, ordered control/bulk lanes and expiring state.
- Bounded send/receive windows, reliable backpressure, pacing, experimental
  congestion response and caps on reliable bytes in flight.
- A seeded bidirectional impairment simulator with serialized bandwidth limits.
- Independent Python packet fixtures, targeted contract cases, stateful fuzzing
  and mutation checks.
- A test-only socket executable restricted to IPv4/IPv6 loopback. Two processes
  exchange 100 reliable records each way using a shared machine-clock origin.

This is an inner protocol and test-transport foundation. It is not a secure
internet service, game replication system or completed multiplayer mode.

## Bugs and regressions found during this work

1. The first ACK range incorrectly rejected the highest permitted packet number
   because a sentinel was treated like a preceding range. The explicit unsigned
   boundary fixture caught it; first-range validation now has no sentinel gap.
2. A state record could expire while waiting for application consumption. It is
   now checked again against the caller's latest supplied clock when drained.
3. Unrestricted reliable traffic could occupy the path and let most fresh state
   expire even though every reliable record eventually arrived. Separate
   1,800-byte in-flight caps on control and bulk improved the constrained
   fixture. The impairment suite now includes state-delivery regression floors.
4. The UDP test used a clock sample taken before reading arriving datagrams.
   That could misclassify a newly arrived timestamp as future. It now samples
   the shared local clock after receive. This does not solve remote clock sync.
5. The expanded 100-seed unoptimized sanitizer run exceeded its 120-second test
   deadline. Sanitizer builds now use `-O1` with frame pointers, retaining both
   memory and undefined-behavior instrumentation. A timeout was not counted as a
   pass, and the final suite was rerun with the documented configuration.

## Evidence scope

Final macOS runs passed all five CTest groups in both the ASan/UBSan (`-O1`)
and optimized Release configurations. The final stateful fuzz run completed
64,564 executions without a reported crash or invariant violation. An earlier
decoder-only run completed 9,666,365 executions before the stateful extension;
those are different targets and should not be combined as one final-code claim.
Five consecutive sanitizer loopback checks also passed. Android ARM64 cross-build
passed for the static library, contract tests and UDP test executable.

The independent wire oracle checks 1,001 valid and 100 invalid packets. The
contract executable reports 1,492 assertions, independent of `NDEBUG`. The
impairment suite repeats each profile with the same seed and checks identical
delivery digests; the soak exercises 100 additional seeds. These are automated
virtual-time fixtures rather than human gameplay or real internet measurements.

The following results use seed 12345, continuous replaceable state for four
seconds and a burst of reliable events/bulk records. There are 500 generated
state updates and 400 reliable records across both directions. Reliable records
are drained beyond the four-second state-generation interval until completion.
One-way base delays are asymmetric (the return direction uses half the listed
delay); jitter adds 0 through the listed value independently to each datagram.

| Base delay / jitter | Random loss | Directional link limit | Reliable delivered | Fresh state delivered | Retries |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0 / 0 ms | 0% | 200,000 B/s | 400 | 500 | 0 |
| 20 / 10 ms | 1% | 80,000 B/s | 400 | 485 | 5 |
| 50 / 30 ms | 3% | 40,000 B/s | 400 | 225 | 11 |
| 100 / 80 ms, plus 400 ms outage | 10% | 25,000 B/s | 400 | 43 | 48 |

The 3% fixture originally delivered only 18 fresh state updates. The improvement
to 225 is useful but still not an acceptable gameplay-latency claim. These
fixtures intentionally combine loading traffic with gameplay; the next transport
work must improve congestion/queue behavior and measure per-direction latency,
not simply increase state lifetime to hide stale delivery. Random packet drops
are injected before the simulated bandwidth serializer. No real router, mobile
radio or competing external flow was measured.

Four deliberate mutations are detected: accepting bad packet magic, accepting
duplicate packet numbers, delivering expired queued state, and removing reliable
in-flight caps. The script compiles temporary copies and leaves sources intact.
Fuzz runs exercise parser round trips and session queue bounds; finite successful
fuzzing does not establish security or absence of defects.

Compact raw results and source hashes are stored in
[evidence/network-foundation/](evidence/network-foundation/). Local build trees
contain full CTest/fuzzer output and are ignored by Git.

## Platform and integration boundaries

macOS ARM64 executes the native tests, sanitizer/fuzzer runs and two-process UDP
checks. Android ARM64/API 28 cross-compiles the library and test executables
with the existing NDK; no executable was deployed to the tablet. Windows,
Linux and iOS runtime/build behavior remains unverified.

No APK was rebuilt or installed. No game assets, saves, settings, refresh policy
or running campaign state were touched. Existing unrelated working-tree changes
were preserved. No external networking source was imported and nothing was
submitted upstream.

## Remaining gates

1. Pin/build/review a vetted authenticated datagram dependency, then test wrong
   identity/key, tampering, restart, replay and impaired handshakes. Mbed TLS
   DTLS is the candidate; no authentication is implemented here.
2. Complete the congestion/recovery contract and tests: RTT variance, probe
   backoff, ACK-loss behavior, persistent congestion and competing-flow fairness.
   The current controller is explicitly experimental.
3. Add remote clock synchronization, expiring action-history schemas, installed
   snapshot acknowledgements and bounded transfer reassembly. The current opaque
   state/event/chunk records do not supply those gameplay semantics.
4. Establish the engine's fixed simulation/authority boundary and a two-player
   movement/combat slice before evaluating 32 active players.

These are engineering follow-ons within the existing plan, not user decisions
required to finish the current protocol/harness milestone.
