# Open Rune authentication and clock synchronization

October 26, 2026. Implemented in the standalone `Network/` library. The engine,
campaign and APK are not linked to it. This extends the [initial foundation](NETWORK-VALIDATION.md)
and retains its experimental congestion-control limitations.

Subsequent replication work advances the wire contract and ALPN to version 2;
see [NETWORK-REPLICATION.md](NETWORK-REPLICATION.md). The results below record
the authentication milestone before that extension.

## Authenticated datagrams

`SecureChannel` uses the official **Mbed TLS 3.6.7** release, downloaded with the
SHA-256 pinned in `Network/CMakeLists.txt`. The selected protocol is **DTLS 1.2**
with **ECDHE-PSK / ChaCha20-Poly1305**, plus the required ALPN `open-rune-net/1`.
There is no cipher fallback, custom encryption or custom key-exchange algorithm.
The dependency's release archive and notices are recorded in
[thirdparty-notices](../../Network/thirdparty-notices/README.md).

Each peer is configured with an identity and a separate 32-byte random invitation
key. `generateInvitationKey()` uses Mbed TLS's system-seeded random generator;
initialization or generation failure throws rather than supplying a fallback.
Empty identities, oversized identities and an all-zero key are rejected. The
identity and secret must be provisioned privately outside this connection. This
is proof of possession of an invitation key, not an account service, password
login, one-use token system or permission to perform arbitrary game actions.
Do not share one invitation key among all players. Revoke/rotate credentials by
destroying the corresponding session and provisioning a fresh key through the
future admission layer. There is no credential UI or public listener yet.

Mbed TLS supplies DTLS address-verification cookies, ephemeral key exchange,
record encryption/authentication and record replay protection. The server's
cookie binding is supplied by the caller from the actual peer address/port,
never a claimed address inside a packet. Anti-replay remains enabled. Session
tickets and renegotiation are disabled. Each reconnect creates a new channel;
no previous record keys or session state are resumed.

Handshake driving is nonblocking, with local monotonic timers, a 250–2,000 ms
library retransmission interval and a 10-second overall deadline. Late application
records encountered before key establishment are discarded; they cannot abort
a new handshake or enter the delivery core. A failed channel releases queued
traffic and cannot be reused. Normal packet loss/tampering is handled according
to DTLS behavior; successful authentication is required before plaintext reaches
the session protocol.

Inbound ciphertext, outbound ciphertext and pending plaintext each have a
64-record cap. Ciphertext is limited to 1,200 bytes and session plaintext to
1,105 bytes. The adapter bounds processing per call and returns backpressure
when queues are full. A tested maximum-size record fits the datagram limit.
These are application bounds, not a claim about a complete server's memory
budget. Mbed TLS also has its own bounded buffers. The future listener still
needs a global pending-peer limit and admission policy before allocating these
per-peer contexts. Cookies alone do not provide that policy.

The pinned default PSA implementation has process-global state. Library entry
points are serialized across channels by a mutex, while each channel/session
still requires one owner. No engine objects or script callbacks are accessed.
Parallel performance for 32 clients is unmeasured.

## Session agreement and wire envelope

Only authenticated plaintext enters `SecureSession::handle`. Its envelope is
ASCII `ORS1`, one type byte, then a type-specific body; all integers are unsigned
big-endian. Unknown types or malformed session identity messages close the
session. Clock messages are length/rate checked before use.

| Type | Body | Behavior |
| --- | --- | --- |
| 1 | session ID u64, epoch u32 | Server generates a fresh nonzero random identity after authentication and repeats this offer until confirmed. |
| 2 | session ID u64, epoch u32 | Client confirms exactly that identity/epoch. Duplicate confirmation is harmless; a conflicting identity fails. |
| 3 | probe ID u64 | Requests the peer's receive/send timestamps. The sender retains its own send time locally. |
| 4 | probe ID u64, receive time u64, send time u64 | Supplies a clock sample only for the outstanding probe. |
| 5 | existing `ORN1` packet | Sent and delivered only while session agreement and a fresh clock estimate exist. |

The original inner packet format is unchanged. A received message's remote
creation time is mapped into local monotonic time before the delivery core checks
expiry. Local retry/pacing clocks remain local; they are never stepped to match
a peer. During synchronization loss, deadlines advance without consuming packet
numbers or pretending withheld traffic was transmitted. Pending stale state
expires normally. Reliable delivery retains its original bounded queues and
timeout behavior.

Session agreement has a 15-second startup deadline. Initial clock acquisition
must complete within 20 seconds from startup; after becoming usable, a session
fails if it cannot regain a usable clock within 20 seconds of last usability.
Neither enqueue nor drain can bypass a stale-clock gate. Reconnect is explicit
construction of fresh session state, not replay of the previous input queue.

## Clock estimator and limits

Both directions make authenticated probes, so each endpoint can map the other's
clock. There is one outstanding probe, at most one new probe every 250 ms, a
2-second response deadline and a bounded history of eight samples. Replies are
limited to one per 200 ms. A response must name the outstanding ID; an unrelated
ID cannot consume the valid request. A matching ID is single-use even when its
sample is invalid. DTLS keys/session identity prevent old-session responses from
being imported into a reconnect that restarts its probe numbering.

Given local send/receive times `t0,t3` and remote receive/send times `t1,t2`:

```
network RTT = (t3 - t0) - (t2 - t1)
remote-minus-local offset = ((t1 - t0) + (t2 - t3)) / 2
```

The implementation rejects backwards local time, invalid remote ordering,
remote processing intervals over 250 ms, processing longer than the total
exchange, network RTT over 500 ms and out-of-range integer timestamps. No
floating-point clock values are accepted. The bounded timestamp domain keeps
signed differences and sums away from overflow.

Three consistent samples are required initially. The lowest-RTT sample from the
recent window supplies the target offset; established estimates slew rather
than jump. Uncertainty includes half the RTT, integer rounding, pending slew
and a 1 ms/second drift allowance for sample age. Abrupt offset changes outside
the previous/new uncertainty envelope are rejected. An estimate becomes unusable
after five seconds without a valid sample, requiring fresh acquisition. Samples
older than five seconds are removed from the selection window.

Mapping rejects a timestamp too far in the future. A future value within the
uncertainty bound is clamped to local now; it never grants future simulation
time. This is not yet authorization to rewind combat or accept an attack. The
later action-history layer must apply its own age, cooldown, commitment and
authority rules.

Path asymmetry cannot be inferred from RTT alone. The explicit 100/20 ms path
test estimates an offset 40 ms away from the known true offset and reports an
uncertainty bound that contains it. Authentication verifies who sent a clock
claim, not whether a compromised peer is truthful. The authoritative server
must retain its own clock and validate gameplay commands independently.

## Tests and fixes

The seven CTest groups pass in macOS ARM64 sanitizer and Release builds, covering
the original codec/delivery suite and the new authentication/clock paths.
The focused authentication/clock executable completed 16,238 checks; the inner
contract executable completed 1,495. Each new security mutation was caught.
Five repeated runs of the authentication and secure-loopback groups passed.
Final fuzzing completed 408,257 handshake/clock cases and 44,199 protocol/session
cases without a reported sanitizer failure or violated invariant.
The Android ARM64/API 28 library and executables cross-build with the existing
NDK. ADB reported no connected device, so Android execution is not claimed.

New coverage includes:

- Valid invitation, observed cookie exchange, lost handshake flights, wrong key
  and wrong identity; data is unavailable before authentication.
- Tampered ciphertext, duplicate encrypted application records, captured packets
  before and after reconnect, fresh session IDs and maximum record size.
- Send/receive queue limits, silent-peer handshake timeout and backwards clocks.
- Exact positive/negative offset fixtures, reply-ID spoofing and replay, invalid
  processing times, excessive RTT, integer limits, drift and asymmetric paths.
- Authenticated delivery of 100 reliable records with 3% simulated loss, jitter,
  independent clock origins and 1,000 ppm simulated drift. A six-second outage
  makes the clock stale, blocks delivery, then recovers after fresh samples.
- Real two-process IPv4 and IPv6 DTLS loopback exchanges, 100 reliable records
  each direction. The processes initialize their own clock origins; the secure
  client does not use the legacy shared-origin argument. Keys in this fixture
  are deliberately public and restricted to loopback tests.

Testing and review found and fixed an old-application-record handshake abort,
a missing clock-sample-age drift allowance, and false transmission accounting
while clock synchronization was unavailable. Dependency review also added
process-once PSA initialization and serialized library access. One restart
test also inadvertently advanced its handshake clock by the entire timeout;
the fixture now uses a consistent local clock.

Security mutation tests deliberately remove the future-time check, outstanding
probe-ID check, DTLS replay protection and stale-clock delivery gate. Each must
be detected by the corresponding test. This supplements the earlier four
codec/delivery mutations.

The security fuzz target exercises the pre-authentication DTLS parser and clock
sample validation, with library sanitizer/coverage instrumentation. It does not
cover every authenticated handshake state or constitute a cryptographic audit.
Finite fuzzing and successful loopback tests do not establish internet-service
or 32-player readiness. Compact results, dependency identity and source hashes
are retained in [evidence/network-auth-clocks/](evidence/network-auth-clocks/).

## Reproduction and next boundary

Use [Network/README.md](../../Network/README.md) for configure/build/CTest and
fuzz commands. Additional checks:

```sh
python3 Network/tests/security_mutations.py build-net-release
ctest --test-dir build-net -R 'authentication|dtls' --repeat until-fail:5 --output-on-failure
```

The next work is the gameplay-facing input/action history, snapshot installation
and transfer schemas, and the engine's fixed authoritative simulation boundary.
Congestion/recovery and public-host admission remain separate completion gates.
No APK, campaign assets, saves, refresh settings or gameplay behavior changed.

Primary dependency references: [official 3.6.7 release](https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-3.6.7),
[DTLS timer/cookie guidance](https://mbed-tls.readthedocs.io/en/latest/kb/how-to/dtls-tutorial/),
and [pinned SSL API](https://github.com/Mbed-TLS/mbedtls/blob/mbedtls-3.6.7/include/mbedtls/ssl.h).
