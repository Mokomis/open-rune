# Multiplayer diagnostics and receive ordering — November 1, 2026

This is the first implementation pass of the recommended multiplayer reliability
sequence. Protocol/rules remain **21**. Server authority, full attack wind-ups,
the **33 ms** defensive window and normal movement rules are unchanged.

## Name and team tags

Team overlays now resolve `PlayerScore.pawn`, including its generation, rather
than constructing an entity reference from the persistent invitation slot.
Character selection and joining an already-running match can replace that pawn.
Vacant/retired references resolve to no actor. Existing visibility, range,
line-of-sight and health checks remain. Green and gold teams now have their own
colors instead of using blue.

The native fixture checks a slot pointing to a different live entity, a retired
generation and a vacant slot. The rendered fixture uses selected nondefault
characters and verifies the tag was actually drawn for a remapped pawn.

## Optional diagnostics

**Network diagnostics** defaults Off in Video and Enhancements on Android and
Windows. It uses the shared engine overlay; it has no single-player gameplay
effect. Direct desktop clients and headless servers accept
`--rune-net-diagnostics`. The private-session launcher also accepts `--diagnostics`.

The client shows:

- Mean/p95 engine loop time for rendered frames, including pacing; this is not
  independently measured display presentation or isolated GPU time.
- Smoothed authenticated data-ACK round-trip time, with no startup default
  reported as a measurement. It includes ACK scheduling and application polling.
- An exponentially smoothed change between consecutive RTT samples, labeled
  **variation**. It is not a direct one-way jitter or physical input-latency test.
- Installed-world rate, largest recent gap and age since the last installation.
- Accepted/duplicate inner transport traffic in KiB/s, excluding DTLS, UDP/IP,
  discovery and DHT framing. It is not total network-interface bandwidth.
- TX timeout/data-send counts, duplicates, outstanding bytes and congestion window.
  A timed-out transmission is **not proof of packet loss**. A window can contain
  timeouts of packets sent in a prior window, so no timeout percentage is displayed.

Client timings retain at most 1,024 samples over two seconds, with reports at
most once a second. Map travel, a handshake retry and toggling diagnostics clear
the local measurement window. Logging never includes invitation credentials.

Headless server diagnostics report local simulation-tick mean/p95/max and per-peer
transport/snapshot counters every two seconds. These tick CPU times are **local
host measurements**, not invented remote-client telemetry. They are not transmitted
over a new protocol field. Snapshot counters include encoded size, maximum size,
parts, selective retries and intentional replacement of an older transfer.

## Bounded snapshot reassembly

The previous receiver retained only one partial snapshot. If the first fragment
of snapshot B arrived before the final fragment of snapshot A, it discarded A
even if A could have been decoded and installed immediately afterward.

The receiver now retains at most **four** partial transfers (at most 160 KiB of
encoded payload), with independent expiry. Old fragments cannot evict newer
transfers when the window is full. Only validated, monotonically newer snapshots
can become candidates. Installation removes older partial transfers while keeping
newer useful fragments. Conflicting duplicates, malformed payloads, missing
baselines, stale ticks and lifecycle resynchronization retain fail-closed behavior.
The sender, wire format, congestion policy and snapshot rate are unchanged.

The new interleaving regression failed with the previous receiver. It passes with
the bounded window. Additional cases cover capacity, stale-fragment eviction,
independent expiry, commit ordering and existing corrupted/duplicate fragments.

## Measurements and scope

Mac eight-player scripted TDM comparisons used DM-Hildir with 75 ms delay each
direction, ±15 ms jitter, 3% random loss and 2% duplication. Both completed without
client/host errors. Baseline active update rates were roughly 13–19/s; the buffered
receiver measured roughly 14–20/s. The samples differ in startup timing and do
**not** establish a general throughput gain. Some worst-case gaps worsened.
The receive change is retained for the reproduced ordering defect, not advertised
as the solution to congestion or large-match smoothness.

In that run, snapshot sizes peaked near 1.6 KiB and typical host ticks took about
4 ms. These observations do not justify dropping cosmetic actors, increasing the
tick rate or reducing visual quality. Larger active worlds need separate evidence.

Rendered tests use one native-window-pixel, 4x MSAA Mac client and three headless
peers. They check tag drawing and diagnostics against a real authenticated host.
This is not Android sustained-FPS or human combat-fairness evidence.

Exact results, hashes, captures and failed exploratory attempts live outside Git
in `outputs/multiplayer-diagnostics-20261101`. The first tag harness incorrectly
combined character changes with an older kick-specific team fixture. That host
failed; its initial overly permissive result is invalid. The corrected harness
uses a dedicated tag-placement fixture and requires the host to remain alive.
An output named `rendered-impaired` was initially run without the impairment
flag; its own JSON correctly says `impaired: false`. It is a clean run only.

The combined reconnect fixture also exposed its five-second residual startup
allowance after waiting for a slot to retire. The test now reserves at least
30 seconds for the returning client and extends its owned host lifetime and
matching shutdown allowance. This
changes the test allowance, not production connection timing or admission policy.

Final checks:

- Release networking suite **14/14**, including the new diagnostics regression.
- ASan/UBSan networking suite **14/14**, including bounded fragment reassembly.
- Native engine combat/replica fixture: **430 snapshots / 4,093 ticks**, including
  current-pawn/generation tag resolution and existing combat/death/gib checks.
- Clean and impaired rendered native-window/4x tests: remapped pawn tags drawn,
  real ACK RTT and transport/engine measurements published, host remains alive.
- Eight-player impaired scripted TDM passes for both comparison builds. Aggregate
  active install rate is 16.17/s versus 16.85/s; worst active gap is 658 ms versus
  1,479 ms. This is not sufficient evidence of a general performance improvement.
- Corrected four-player impaired blackout/leave/rejoin/travel check passes, all
  six owned process exits zero. The match reloads DM-Hildir; the returning player
  installs a replacement world in the waiting round after the other peers leave.
- Android ARM64 networking static library and all Android/SDL Java sources compile.
  Menu-model migration/persistence checks pass. This is not a complete signed APK,
  Android graphics test or installation.
- Private headless launcher starts with diagnostics and exits cleanly; temporary
  invitations/configuration/saves are removed after the check.

The inspected impaired Mac capture is 1920x1200 at scale 1 / 4x MSAA. Its 300-frame
sample averages about 60 FPS, with p95 18.49 ms. A 120-FPS cap does not establish
120 displayed FPS, and these measurements are not a tablet performance claim.

## Following work

1. Use the diagnostics to separate actual congestion, reassembly stalls and
   client scheduling before changing packet budgets or actor update priorities.
2. Extend movement prediction only with coverage for each locomotion mode,
   moving bases, collision and interruption. Unsupported modes still use authority.
3. Evaluate a bounded adaptive presentation buffer with low-latency and jitter
   comparisons; do not hide poor delivery by indefinitely increasing delay.
4. Package the existing headless host more conveniently, then add repeatable
   client/server releases and clear compatibility guidance. A fresh build is not
   an installed or published release.

The Windows public match remains stopped. No Windows-machine update, tablet installation,
public discovery session, commit or push are implied by these laptop checks.
