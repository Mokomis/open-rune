# Multiplayer responsiveness and defense window

October 26, 2026. Private, experimental two-player DM-Hildir milestone. This
continues the [interactive client](NETWORK-INTERACTIVE.md); it does not establish
complete multiplayer, campaign co-op, Internet hosting or 32-player readiness.

## Movement and update delivery

The local client now predicts grounded walking with read-only collision queries.
It shares the authority's acceleration/braking calculation, including wall
sliding and step traversal, without running scripts, Touch, pickups or damage.
The world checkpoint includes the exact processed input and its held-tick count.
Because Rune applies PlayerTick after physics, this acknowledges the previous
input whose acceleration actually produced the checkpoint. Packet receipt does
not acknowledge movement. Input history is bounded to 128 samples / one second;
replay is limited to 64 steps and 350 ms. Small corrections decay over 65 ms;
large corrections, death and life changes discard smoothing. Collision also
constrains the smoothed presentation position.

The 350 ms presentation budget was selected after the 150 ms asymmetric/jitter
fixture showed repeated fallback with the initial 200 ms budget. It grants no
authoritative movement, hit reach or command-age allowance. Outages stop
speculative travel at that limit. Jumping, falling, swimming, crouching, ladders,
moving bases, knockback and interrupted moves retain authoritative presentation;
this is not yet prediction for every locomotion mode. Remote actors retain the
existing independent fractional body/upper-body animation interpolation.

Small snapshots previously waited for an installation acknowledgment before
sending the next world, reducing their rate to roughly one per RTT. Worlds of
up to four fragments now pipeline at up to 30 Hz against an installed baseline,
after every fragment has been offered once. At most 16 snapshots can be ahead
of the installation acknowledgment, with 32 cached worlds. Full bootstrap and
large worlds retain selective retries so continuous replacement cannot starve
them. This is a targeted small-match improvement, not large-world throughput
validation.

Protocol/DTLS ALPN are **10**, snapshot payload **5**, native rules revision **10**.
The 31-byte header adds an optional 31-byte owned movement checkpoint. Entity
records remain 77 bytes; a full 512-entity world is 39,455 bytes without the
checkpoint or 39,486 with it, both 62 fragments. Earlier clients are rejected.

## Selected melee policy

The user selected a two-tick, approximately **33 ms defense window**. A real
server collision/animation contact first freezes its participants and contact
positions. The server keeps the full original wind-up, then waits at least two
simulation ticks and 33 wall-clock milliseconds before committing damage,
knockback/stagger and the associated hit effect. Scheduling stalls can delay
commitment further; they never extend the 33 ms late-defense admission window.
There is no attack rewind, arbitrary VM rollback or client-provided hit result.

This covers native kicks, shield bashes and the ordinary `Swinging` weapon
`DoWeaponSwipe` path. The latter copies arguments out of the VM stack and defers
its paired hit effect. Original sweep geometry, joint masks and swipe deduplication
select the contact. World/environment contacts and thrown weapons keep their
existing behavior. Normal weapon damage is still conveyed by authoritative
snapshots; reliable detailed combat outcomes currently cover kick/bash only.

A late block counts only when:

- Its authenticated, clock-mapped press is at or before contact, at most 100 ms
  old, and arrives within 33 ms after contact.
- The defense interval was active at contact. A later complete held-state release
  supersedes an earlier raise even if separate action delivery is reordered.
- The defender still has the same intact shield and life. Both the frozen
  contact facing and the block input facing must face the attacker.
- The contact still passes life/equipment/death checks and both frozen and current
  lines are clear of world geometry and moving cover.

The frontal shield cone is the existing kick/bash threshold (dot product >= .25).
For network weapon contacts it applies equally to an already raised shield and
an eligible late raise, avoiding extra coverage from deliberately delaying a
block. This is a multiplayer rule change from relying only on the original
shield mesh collision; it is covered by the native rules revision. A blocked
weapon hits the shield and can damage/break it. Blocked kicks/bash retain their
small recoil. Single-player combat does not enter this queue.

The queue is bounded to 128 contacts. Records are removed before invoking damage
scripts, preventing replay; stale action IDs/timestamps are rejected at the
engine boundary. Death, equipment changes or blocked cover cancel invalid
pending contacts. Sequential resolution can cancel a later contact after an
attacker dies; simultaneous trade policy is not separately implemented.
Clock mapping is an estimate, not proof of the physical button-press instant.
After commitment, a block never heals health, cancels death or reverses impulses.

Counterexamples covered by the focused engine fixture include an on-time late
block versus a press after contact, arrival after the grace deadline, wrong
facing, broken/transferred shield, death before commitment, release timestamps
on either side of contact, and repeated flushes. The ordinary weapon VM path
also verifies deferred body damage and shield damage after a late block.

## Validation and remaining boundary

Retained results are in `evidence/network-responsiveness/`. The standalone suite
includes prediction/checkpoint codecs, replay/life reset, display-rate
independence from 30–240 Hz, timing boundaries, overflow, snapshots, authentication,
clocks, seeded impairments, soak and independent wire oracles. The real-map
engine probe exercises 16 walking directions against geometry without actor
movement, health or actor-count side effects. A separate natural-animation duel
checks real kick/bash impacts over authenticated UDP with 50 ms one-way delay
and 3% loss.

`network_interactive.py` now supports bounded loopback-only delay, asymmetric
paths, jitter, random loss, duplication, burst outages and disconnect tests.
Private invitations are rewritten only in their temporary directory; packet
contents and credentials are never logged. `OPEN_RUNE_PREDICTION` and
`OPEN_RUNE_MELEE_WINDOW` report bounded replay/corrections and commitment timing.
`--rune-net-prediction=0` is a diagnostic comparison. Android exposes the same
diagnostic through `networkPrediction` only with `saveTest` isolation.

These are automated checks, not a completed human fairness playtest. Attack
animation prediction, full movement prediction, richer weapon result/feedback
replication, jitter-buffer presentation, scoring/rounds, inventory selection,
movers/world events, travel, reconnect/resume and larger matches remain future
work. Native 4x Android measurements and preservation checks belong in the
retained result ledger; do not infer sustained all-scene FPS from one view.

## Installed build and final results

APK SHA-256: `700ce8d46a1934c50e0e8d6f02bb988b8c14d633466589d59c7b94027d534075`.
The 12-group standalone suite passes in Release and with ASan/UBSan. The engine
probe passes 11 melee-window cases plus 2,044 moving and 836 blocked prediction
steps. The final natural duel at 50 ms one-way / 3% loss commits kick and bash
once, with both clients confirming health 100 → 93 → 90. Its contacts waited
33–34 ms (mean 33.5 ms). The final rendered 150 ms asymmetric test (100 ms up,
50 ms down, ±30 ms jitter, 3% loss and 2% duplication) passes movement, kick,
jump, throw, death and respawn: 2,258 / 2,257 frames and 675 / 738 installed worlds.
A 500 ms blackout recovered; a sustained blackout ended the match cleanly.
These are bounded fixtures, not a claim that every action survives arbitrary loss.

On OPPO OPD2515, the gameplay fixture rendered 4,668 frames, installed 1,600 worlds,
observed the kick and death, then used the actual Android Respawn button to restore
life 2 / health 100. It recorded 2,093 predicted frames and a maximum correction
of 21.87 Rune units. A USB system dialog obscured part of that run, so its timing
sample is not used as the comparable performance result. The final small defense
release-order/moving-base guards were subsequently checked by the desktop probe
and compiled into the installed Android build.

The final separate fixed-camera comparison retained native **2520×1680**, **4× AA**,
all selected effects (8103) and the **144 Hz** hardware policy:

| Mode | Displayed FPS | Sample | p95 interval | Longest interval | >50 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Prediction on | 89.11 | 13.05 s | 13.89 ms | 20.83 ms | 0 |
| Prediction off | 89.37 | 13.06 s | 13.89 ms | 20.83 ms | 0 |

Receive/install work measured 0.167 / 0.086 ms per frame; presentation work
0.290 / 0.177 ms; rendering 10.740 / 10.957 ms. These short sequential samples show
no material frame-rate penalty in this view, not guaranteed all-scene 60/120 FPS
or a prediction stress benchmark. The earlier renderer optimization remains in
place. Normal save hashes, saved preferences and Rune's refresh rule match the
pre-test copies. Tests use isolated save directories, and the app is returned
to its normal title menu with native 4x session overrides; saved scaling is retained.

## Human combat performance follow-up

During the later human blocking session, read-only SurfaceFlinger sampling
measured **62.21 displayed FPS over 12.91 seconds**, p95/p99 **27.79 ms**,
maximum **27.79 ms**, with no intervals over 50 ms. Native 2520x1680 / 4x AA
remained selected, and the panel remained at 144 Hz. The GPU was 93–98% busy;
its clock was 975 MHz initially and then 646 MHz, with a 646 MHz maximum cap
in the last samples. The earlier 89 FPS samples ran the GPU at 1225 MHz.
Android thermalservice reported severe status (3) during this later check.
This is evidence of GPU saturation with thermal clock limiting, not a precise
allocation of cost among effects. The earlier short fixed-camera results do
not establish sustained combat performance. No renderer settings, thermal
controls or player inputs were changed for this observation. The sparring
session remained under human control. See `human-combat-slow-display.json`
and its timestamp file in the evidence directory. Human fairness remains
unvalidated; this observation is about presentation performance.

## October 29 client attack and block responsiveness

Interactive clients now receive first and transmit after capturing current input,
before drawing the frame. Fresh action edges bypass the 16-ms held-input timer;
transport pacing and retries remain bounded. The deterministic 125-Hz client test
reduces worst capture-to-host-admission time from 17 ms to 1 ms with no added path
delay. This is dispatch latency, not a physical input-to-display measurement.

Local anticipation renders the authored 100-ms attack wind-up, shield raising and
crossed guard pose without running combat scripts or changing replicated state.
It does not predict a strike, interrupt recovery, add combo buffering, grant a
block or play an unconfirmed impact. Confirmation/rejection blends back over
80 ms; missing confirmation expires after 350 ms plus that blend. Damage, death,
equipment/life/model changes and session transitions invalidate the preview.
The full authoritative wind-up and the selected 33-ms late-block window remain.

Windows network (13/13), focused replication (253170 checks), native combat and
four-player impaired tests pass. The ARM64 native fixture passes too. The signed
APK is installed; a separate Windows-hosted Android session verified attack and
block/release previews and host decisions at native 2520x1680 / 4x AA. The short
900-frame engine sample averaged 99.15 FPS, not sustained combat/display timing.
Controller-path key events were used; no touch-layout changes or new touch test.
Preferences compare unchanged and normal startup is restored. The existing public
Windows host stays active/compatible; the new Windows executable is staged as
build-windows/SurrealEngine-Responsive.exe. Human feel remains a playtest judgment.
Evidence: C:/OpenRune/outputs/combat-responsiveness-20261029/RESULTS.txt.
Final correction: immediately received combat health outcomes also invalidate
anticipation before the next world snapshot. The specific native regression
passes on Windows and ARM64. The final signed APK was installed with permission,
its on-device hash verified, and normal launch restored. Render/control settings remain unchanged; the latest multiplayer character selection (20) was preserved.
Final APK SHA256: 647c74d41be40336ea8a989842ec8ab0d81de89bc073185b860fe8e8b1465052.