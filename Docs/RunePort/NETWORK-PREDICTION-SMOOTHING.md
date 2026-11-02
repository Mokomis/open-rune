# Movement prediction and adaptive smoothing

November 1, 2026. Mac implementation and validation, followed by the authorized
Windows/tablet deployment recorded below. This follows [diagnostics and snapshot reassembly](MULTIPLAYER-DIAGNOSTICS.md).
Earlier milestone version numbers elsewhere in the plan are historical.

## Installed follow-up

Both platforms now use protocol/ALPN 22, with native rules 20 and snapshot 6.
The Windows default executable and versioned server-browser launcher are updated;
no public server was started. Its 15 network tests, five local discovery tests,
native combat checks and 192-case falling oracle pass. Changed Windows source
and its previous executable/resources were backed up before replacement. The
Windows executable SHA-256 is
`f60b07b663c7dc89ed4fb1a83fad609745d7eba4f3626ed86efa903af1b7fe1e`.

The installed OPPO APK SHA-256 is
`b1a3f8da02c5446becf785647987e32c9653e23beee32b82861255de0abe5a32`.
The installed package bytes match the built APK. It retains the original signing
certificate and optional Turnip driver payload. Every pre-existing preference
and ordinary save file matches its pre-install hash, including after the checks.
The actual Network settings page displays Adaptive and diagnostics Off, and was
visually inspected. Its temporary save/configuration paths are isolated.

The OPPO joined an isolated Windows Team Deathmatch host with a normal bot.
The short 120-frame native 2520×1680, 4× MSAA profile reports 106.59 engine FPS,
9.38 ms mean and 10.02 ms p95 frame time. Adaptive smoothing was active, with a
51 ms target and 35.67 ms buffered at shutdown. This verifies a matching join and
render path, not sustained combat FPS or displayed cadence. The temporary host
and bot exited; the tablet was returned to its normal menu. Both discovery
runtimes and the default Android build cache now match version 22.

Evidence, previous APK/settings/save backup, source hashes and build/install logs
are in `outputs/movement-deploy-20261101` outside Git. Windows source updates
include the canonical Mac muted blood palette. No commits or pushes were made.
The remaining sections describe the preceding Mac-only implementation milestone.

## Current compatibility

The native packet protocol and DTLS ALPN are **22**, native gameplay rules remain
**20**, and snapshot schema remains **6**. The 31-byte movement checkpoint now
distinguishes unsupported, walking and falling modes; its existing parameter
fields have mode-specific meanings. Old clients must not interpret falling as
walking. `ProtocolAlpn` centralizes the offer and verification string; the
discovery source advertises the same protocol. Deploy matching hosts, clients
and discovery runtimes together.

At the first Mac-only checkpoint, the cached Android discovery runtime still
contained protocol 21. The installed follow-up above rebuilt it and packaged a
matching APK. Keep this version guard for future builds.

## Local movement

Walking retains the existing read-only collision replay. Ordinary, dry falling
now replays the authority's acknowledged horizontal input, gravity, air control
and terminal speed. It uses the native falling half-step equation and cylinder
sweeps against real level collision. Jump initiation is still confirmed by the
host; prediction does not invoke local jump scripts.

Prediction never calls VM movement events, damage, pickups, `HitWall`, `Landed`
or edge-grab scripts. A predicted floor contact holds until the host confirms
the next movement mode. Dynamic contacts, water, flow, unusual gravity, ladders,
moving bases, crouch and scripted kick/bash/rush states fall back to authoritative
movement. This is deliberately not a complete prediction model for every move.

Replay remains bounded to 128 input samples, 64 steps and 350 ms of speculative
lead. The host's world checkpoint retires acknowledged inputs, not packet ACKs.
Same-mode correction eases within 64 units while walking and 96 while falling.
An authority-confirmed walking-to-falling transition can ease within the latter
bound; landing, new lives and unsupported modes reset. Presentation is swept
against collision before placement.

## Remote movement

`PresentationTimeline` retains up to 16 installed, authenticated worlds and 32
recent timing observations over two seconds. It maps fixed 60 Hz host ticks to
a monotonic visual clock. Arrival variation and observed update gaps determine
a 33–100 ms target delay. Constant RTT alone does not enlarge the buffer.

The clock adjusts at 90–110% speed, never rewinds, never extrapolates beyond a
received world and limits actual buffered host time to 100 ms. Larger gaps can
require a forward catch-up; this cannot make an arbitrary network outage smooth.
An outage or host gap over 500 ms resets history, and long render hitches
reanchor forward. Waiting/retry, map changes and toggling reset the timeline.

Only live remote pawn root motion uses this buffer. The owned pawn retains local
prediction. Skeletal animation keeps its existing fractional interpolation on
the latest worlds, preserving prompt attack/block feedback. Movers, corpses,
gibs, projectiles, items, health and inventory stay on their existing current
timeline. Equipment retains current ownership and joint attachment handling.
Teleports over 256 units snap. New lives begin on the current timeline and adopt
buffering when stationary; moving-brush support disables added root buffering.

Attack wind-ups, server hit decisions and the selected two-tick (about 33 ms)
late-block allowance are unchanged. Historical visual frames never undo damage.

## Settings and the empty-kick report

Network smoothing offers **Original / Adaptive**, default Adaptive. Android has
an actual Network settings page reached from Gameplay, Enhancements and Video's
Frame rate & refresh page. Network diagnostics remains Off by default. Desktop
settings and JNI registration use the same choices. Android Java compilation
and preference-model checks pass; real Android menu layout/input is not yet
tested for this change.

The movement fixture previously launched a `--rune-net-bot-stress` peer that
intentionally cycles attacks and kicks without requiring a nearby enemy. It now
uses `--rune-net-bot-motion`, a movement-only script. The stress option remains
available for deliberate combat stress testing. Normal bot policy was not
changed: 204 checks covering absent, hidden, dead and distant enemies produce
no empty offensive actions, and a nearby visible eligible target still permits
a kick. These checks do not prove every human-observed bot situation is fixed.

## Validation and practical limits

Evidence is outside Git at `outputs/movement-smoothing-20261101` relative to the
Rune project root. The final Mac executable SHA-256 is
`60b4f75d46874f6c3654913d8596091ac9051031a989eb5d5a91f35ef5a9311b`.

| Check | Actual result |
| --- | --- |
| Release network suite | 15/15 pass, including independent wire, authentication, clocks, prediction, presentation and impairment checks |
| Address/undefined-behavior sanitizer network suite | 15/15 pass |
| Native falling oracle | 192 cases against real `TickFalling`, zero measured position/velocity error; floor hold and replay side-effect guards pass |
| Native gameplay | Kick/bash, crossed guard, rage and normal-combat specials, rune activation, inventory, food, corpse/gib presentation pass |
| Block timing | 12 native cases pass, including late blocks and no committed-damage rollback |
| Adaptive safety | Remote-only pose, owned bypass, unchanged health, teleport and life guards pass |
| Synthetic presentation | Clean and jitter/loss profiles at 30, 60, 90, 120, 144, 165 and 240 Hz pass boundedness and no-rewind checks; loss-only 60/120 also pass |
| Rendered comparison | Original and Adaptive pass four-player TDM with 150 ms simulated RTT, ±15 ms jitter, 3% loss, 2% duplication |
| Rendered recovery | Same session recovers after a two-second simulated outage, entering and leaving the waiting state |
| Rejoin/map epoch | Four clients, three-second blackout, disconnect/rejoin and round/map reload pass with impaired traffic |
| Local discovery | 5/5 tests pass, including native/discovery protocol agreement; local DHT evidence only |
| Android source compatibility | ARM64 network library, all app/SDL Java and preference-model checks pass; not a full APK or device test |

The rendered Mac comparison uses a 1920×1200 native window, render scale 1,
4× MSAA and a 120 FPS cap. Observed engine FPS was 59.11 Adaptive and 59.81
Original in short 300-frame samples; p95 frame time was 19.41 and 19.18 ms.
This is not 120 displayed FPS, sustained Android evidence, or a rendering speed
improvement. The fixture uses original visual-effects settings rather than an
all-enhancements benchmark. Adaptive trace buffering never exceeded 100 ms.

At 120 Hz in the deterministic jitter/loss profile, per-frame distance-error
RMS fell from 0.6115 to 0.1051 units; hold frames fell from 269 to 12. These are
synthetic smoothness results, not human-playtest scores. In the rendered trace,
the first confirmed takeoff changed from a 62.62-unit frame jump before the
transition correction to 13.12 afterward; arrival timing differs between runs,
so this is a diagnostic observation rather than a universal bound.

The ordinary impaired stair route on DM-Hildir has zero reverse steps ascending
and descending, including a final run forced to 20 FPS. Body/camera relative
vertical jolts stay below 0.001 units in these traces. Broader map/character
human review, long endurance and other
internet/NAT conditions remain separate release work. No game assets or ordinary
saves were changed; tests use isolated settings and save directories.

## Design reference

[Glenn Fiedler's snapshot interpolation article](https://gafferongames.com/post/snapshot_interpolation/)
explains the jitter-buffer tradeoff and why unconstrained extrapolation fails
around collisions. This implementation is original project code, with explicit
memory/work/delay bounds and current authoritative combat state.
