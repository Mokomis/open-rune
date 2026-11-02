# Open Rune network foundation

Current integrated implementation: [private multiplayer completion](../Docs/RunePort/PRIVATE-MULTIPLAYER-STATUS.md), protocol/ALPN **17**, native rules **17**, raw snapshot **6**. The earlier checkpoint versions below are historical.

Latest mode checkpoint: [Team Deathmatch](../Docs/RunePort/NETWORK-TEAM-DEATHMATCH.md), protocol/ALPN 12 and native rules 12. Earlier versions below are historical.

Latest Deathmatch checkpoint: [four players, scoring, match limits and map rotation](../Docs/RunePort/NETWORK-DEATHMATCH.md). Protocol/ALPN 11 and native rules 11 supersede earlier checkpoints below.

Standalone C++20 protocol and deterministic delivery-test library. It does not
link the renderer, VM or game assets. Networking is disabled in the default game
build. The root `OPENRUNE_ENABLE_NETWORK=ON` option enables an experimental
engine adapter, dedicated server and interactive client. After the October 26 pause, the user chose to
continue with Open Rune's own server and custom combat rules. See the
[engine checkpoint](../Docs/RunePort/NETWORK-ENGINE-CHECKPOINT.md) for evidence
and limits. See the [interactive checkpoint](../Docs/RunePort/NETWORK-INTERACTIVE.md)
for the earlier two-player checkpoint; the Deathmatch link above describes the current scope.
See [the exact experimental contract](../Docs/RunePort/NETWORK-PROTOCOL.md)
and [the broader plan](../Docs/RunePort/MULTIPLAYER-PLAN.md).

October 26: `SecureSession` adds authenticated DTLS connections and bounded
remote-clock mapping. `ReplicationPeer` adds input/action admission and full/delta
world snapshots; see [replication](../Docs/RunePort/NETWORK-REPLICATION.md).
See [authentication and clock behavior](../Docs/RunePort/NETWORK-AUTH-CLOCKS.md).
The [combat-result checkpoint](../Docs/RunePort/NETWORK-COMBAT-OUTCOMES.md)
added reliable kick/bash results. The subsequent
[client duel checkpoint](../Docs/RunePort/NETWORK-CLIENT-DUEL.md) adds snapshot
life generations and a two-process engine fixture. Current protocol and ALPN are 10; [prediction and defense timing](../Docs/RunePort/NETWORK-RESPONSIVENESS.md) describe the latest engine integration.
Configuration downloads the hash-pinned official Mbed TLS 3.6.7 release;
dependency notices are in `thirdparty-notices/` and copied to the build folder.

## Build and test

From the repository root:

```sh
cmake -S Network -B build-net -G Ninja -DCMAKE_BUILD_TYPE=Debug -DOPENRUNE_NET_SANITIZE=ON
cmake --build build-net
ctest --test-dir build-net --output-on-failure -j2
python3 Network/tests/mutation_smoke.py
```

The sanitizer option requires a compiler with AddressSanitizer/UBSan support.
It uses `-O1` with frame pointers so the virtual-time soak remains practical
while retaining memory/undefined-behavior instrumentation.
For a normal release build, omit it and use `-DCMAKE_BUILD_TYPE=Release`.
CTest includes contract cases, seeded network impairments, 100 simulation seeds,
a separate Python wire oracle and two-process IPv4/IPv6 UDP and DTLS exchanges.
The DTLS processes use independent clock origins. Ten CTest groups include fixed-tick simulation and
wrong credentials, tampering/replay, reconnects, drift, stale-clock recovery,
input/action admission and full/delta snapshot recovery.
Test assertions also run in Release builds.
The simulated soak runs faster than wall time; it is not a four-hour real match.

Android cross-build, with an installed NDK:

```sh
cmake -S Network -B build-net-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$RUNE_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DCMAKE_BUILD_TYPE=Release
cmake --build build-net-android
```

This builds a native library and standalone test executable, not an APK. Building
does not prove Android runtime behavior. Windows/Linux/iOS remain untested.

## Fuzzing

With a Clang distribution that includes libFuzzer:

```sh
cmake -S Network -B build-net-fuzz -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug \
  -DOPENRUNE_NET_SANITIZE=ON -DOPENRUNE_NET_FUZZ=ON -DBUILD_TESTING=OFF
cmake --build build-net-fuzz
mkdir -p build-net-fuzz/corpus
build-net-fuzz/net_fuzz build-net-fuzz/corpus -max_total_time=60 -max_len=1101
```

The target checks strict decoding/re-encoding and exercises bounded session
operations with generated valid packets. The random generator in the simulator
is deliberately deterministic and must never generate keys or session secrets.

`net_security_fuzz` fuzzes the pre-authentication DTLS parser and clock samples.
For a valid ClientHello seed, after building the ordinary test target:

```sh
mkdir -p build-net-fuzz/security-corpus
build-net/net_security_tests --fuzz-seed build-net-fuzz/security-corpus/client-hello
build-net-fuzz/net_security_fuzz build-net-fuzz/security-corpus -max_total_time=60 -max_len=1201
```

The seed contains public handshake bytes with a public test identity. It contains
no invitation secret. Never use test fixture keys for a real session.

## Integration boundary

`Protocol` handles bounded canonical bytes. `Transport` models four independent
delivery lanes, retransmission, packet replay rejection, expiry, pacing and a
bounded experimental congestion response. Time is supplied by the caller in
milliseconds; the simulator gives both endpoints the same clock.

Use `SecureSession::receiveDatagram` for untrusted datagrams, then drive `step`
with the local monotonic clock. Enqueue/drain are gated on authenticated session
agreement and a fresh clock mapping. `receiveTrusted` remains a lower-level
interface for **already authenticated plaintext**; never wire it directly to a
UDP callback. `ReplicationPeer` adds bounded input/action schemas, admission
feedback, full/delta snapshots, fragment recovery and explicit installation.
Read [NETWORK-REPLICATION.md](../Docs/RunePort/NETWORK-REPLICATION.md) for the
contract and caller responsibilities. The first kick/bash outcome transport and
engine observer exist; client gameplay application, complete actor adapters,
public listener and invitation UI remain unimplemented.

`net_udp_peer` is a test-only socket adapter restricted to IPv4/IPv6 loopback.
Its legacy mode uses a fixed session/shared clock. Its `secure` mode uses a
public test PSK, fresh DTLS keys/session ID and independent clocks. It is not
a production credential-provisioning path. The game does not link it.

The `State` record is replaceable opaque state. `InputSender` supplies redundant
action history with unique IDs, original timestamps and expiry; arbitrary attack
edges alone are not safe in this lane. `Chunk` is an independently ordered record, not
yet a complete transfer/reassembly protocol. A transport ACK means retained
delivery data, not an installed world snapshot or a committed combat outcome.


Replication validation:

```sh
build-net/net_replication_tests
python3 Network/tests/replication_wire.py build-net/net_replication_tests build-net-fuzz/replication-corpus
build-net-fuzz/net_replication_fuzz build-net-fuzz/replication-corpus -max_total_time=60 -max_len=40000
python3 Network/tests/replication_mutations.py build-net-release
```

Protocol version 8 and ALPN `open-rune-net/8` intentionally reject earlier
experimental builds. Snapshot installation, action admission and committed
combat results remain distinct from packet acknowledgement. See
[`NETWORK-JOIN-LIFECYCLE.md`](../Docs/RunePort/NETWORK-JOIN-LIFECYCLE.md)
for the match and actor additions.
