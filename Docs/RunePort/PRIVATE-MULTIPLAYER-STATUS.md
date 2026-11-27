# Private multiplayer implementation and evidence

October 31: [bot defense, normal-combat leap/rush and Rune Power](COMBAT-AVAILABILITY.md), with protocol **21**. Update host and clients together. [Team movement and terrain recovery](TEAM-AI-TERRAIN.md) includes the accompanying fixes.

October 29 character selection: [36 characters / 37 skin combinations](CHARACTER-SELECTION.md), separate multiplayer/campaign choices, Android horizontal carousel and protocol 20. Mac validation and an Android build are documented there; no tablet installation is implied.

Latest Mac validation: [October 28 follow-up](#october-28-mac-validation-follow-up). Current source protocol/ALPN and native rules are **19**. Earlier protocol-17 matrix and installation statements below describe their milestone builds.

Later discovery checkpoint: [HyperDHT hosting and Android joining](HYPERDHT-DISCOVERY.md). The previous private-only scope below is historical: public discovery and unordered peer transport are now integrated and tested through the public DHT on one LAN and in both directions across laptop Wi-Fi and the OPPO cellular hotspot. Broader NAT coverage and human internet combat remain.


Newer follow-up: [Android connection recovery](ANDROID-CONNECTION-RECOVERY.md)
validates actual Wi-Fi loss, short/long background pauses, rejoin, retry cancel,
foreground host survival, clean stop and abrupt host-loss recovery. These bounded
LAN checks do not establish real internet connectivity or human combat feel.

Android UI follow-up: [Host/Join, map/mode selection and invitations](MULTIPLAYER-UI.md)
are now wired to a protocol 17 APK and an isolated foreground host process.
Installed on the OPPO: real touch hosting, invitation export/import, joining,
rejoining, leaving and stopping pass across LAN. One representative map per mode
was checked with the tablet and a laptop peer; the exhaustive laptop map evidence
below is unchanged. The [native performance follow-up](NATIVE-HOST-PERFORMANCE.md)
adds hidden-menu idling and a supported packed HDR buffer (about 7% controlled
rendering gain). Thermal limits still constrain sustained native 4x combat; this
is not Android capacity or endurance proof.

Implementation was completed and tested in an isolated worktree, then its 30-file source delta was integrated into the main checkout on October 28. Human playtesting remains outstanding. The implementation task did not push or publish its work; source publication is handled separately from this test record. No tablet work or paid services were used for that task.

## Coverage

**44 shipped maps, 14 rulesets, 157 map/variant combinations pass.** Each combination loads and spawns, exercises its key objective through original game scripts, completes a match and travels with authenticated clients. The [CSV matrix](evidence/private-multiplayer/map-variant-matrix.csv) records the exact milestone build and evidence for every combination. Objective fixtures deliberately position actors or invoke original death callbacks; these are not unassisted bot or human playthroughs.

| Mode / variant | Maps passed |
| --- | ---: |
| Deathmatch | 27 |
| Team Deathmatch, 2 / 3 / 4 teams | 27 / 25 / 24 |
| Arena, 1v1 / 2v2 / 3v3 / 4v4 | 9 / 7 / 4 / 2 |
| HeadBall, 2 teams, limbs off / on | 8 / 8 |
| HeadBall, 3 teams, limbs off / on | 4 / 4 |
| HeadBall, 4 teams, limbs off / on | 4 / 4 |

HeadBall checks include team pumps/countdown, a networked head throw, original goal scoring/consumption, optional limb scoring, kill scoring exclusion and restart. Arena checks include the physical queue, countdown, champion/challenger defeat and restart. DM/TDM cover deaths, respawns, scoring and travel. Separate tests pass score/round limits, friendly weapon/kick/bash protection, clean and abrupt leave, reconnect after travel, late join, a three-second blackout, and a headless host left empty for 58 seconds before both invitations rejoin.

## Implementation and verification

Added authoritative weapon selection and complete stowed inventory, reliable skeletal/skin changes, native weapon-contact feedback, stable player slots across pawn replacement, current-map reconnect, map-wide variant preflight, persistent empty hosting, and a laptop player-host/direct-join/headless launcher. Original game scripts remain authoritative. Snapshot packing, queued-fragment handling and obsolete-transfer replacement reduce traffic and prevent stale full-world retry loops. Class-default appearance is shared; only differences are transmitted.

- Normal network suite: **12/12**; ASan/UBSan: **12/12**.
- Final network fuzz run: **120506 cases in 61 seconds**, no sanitizer failure.
- Native probe: weapon families, inventory chain/stowing, appearance/restoration, dual wield, guards, bloodlust, Rune Power and real weapon-hit records pass.
- Two rendered laptop clients pass host/direct-join/map travel and bloodlust poses/actions. Earlier milestone captures verify dual poses and confirmed-hit feedback. Images were inspected; audio routing is exercised, but no human listening test was performed.
- Final executable SHA-256: `d4dd1f4e01cbe46c773adb098c5863989e5cbc5e179babf26e46fbcda1c33178`. The final build differs from the rendered-release build only in bot measurement counters. Matrix results accumulated across milestone builds; the evidence identifies each executable instead of implying every test used one final binary.

## Player limit and measured network behavior

**Enforced roster maximum: 16, including a player-host.** The original GameInfo rejected admission beyond 16; 17-slot startup now fails before issuing invitations. Counts 2, 4, 6, 8 and 16 were exercised, with 16-player loops in every mode. Arena remains limited to four fighters per side with additional players queued. Unsupported map variants are rejected before invitations.

Clean 16-player DM averaged 24.66 installed worlds/second including loading and travel; Arena and HeadBall measured 26.89 and 28.68. These are headless replication measurements, not rendered FPS or human playability certification.

Impaired stress injects **75 ms each way, ±15 ms jitter, 3% loss and 2% duplication**. Active-play counters exclude lobby/loading/intermission gaps:

| Mode / players | Active updates/s | Gaps under 250 ms | Longest active gap |
| --- | ---: | ---: | ---: |
| TDM / 4 | 23.83 | 99.64% | 0.839s |
| TDM / 8 | 15.26 | 98.23% | 2.242s |
| TDM / 16 | 6.42 | 89.04% | 4.572s |
| DM / 16 | 10.5 | 96.8% | 0.756s |

Sixteen is a functional ceiling for good direct connections, not a promise of smooth combat on lossy links. The launcher defaults to four. Prefer four to eight players when the connection is impaired; sixteen-player stress remains visibly limited by the measured update pauses. Human tests must assess feel, fairness and acceptable latency before a broader release.

## Preservation and handoff

The initial dirty checkout (including untracked multiplayer sources) was independently copied and hash-verified before edits. During the isolated task, all **8,645 source files**, the original HEAD/status and **284 original game-data files** matched their initial records. The integration applied only the verified 30-file delta; all 30 resulting files matched the handoff hashes immediately after application, before integration notes were updated. Tests used independent game data plus isolated saves/settings. Campaign saves and the existing Android installation were untouched.

Original worktree: `/Users/rwillmore/Documents/Codex/2026-10-28/finish-open-rune-s-private-multiplayer/work/open-rune-multiplayer`. Host instructions: [Tools/RuneMultiplayer/README.md](../../Tools/RuneMultiplayer/README.md). The [map matrix](evidence/private-multiplayer/map-variant-matrix.csv) and result summaries are copied here. Detailed passing and failed exploratory logs remain in the original worktree's `Docs/RunePort/evidence/private-multiplayer/`; failures are retained rather than relabeled. The handoff patch contains only this task's changes relative to the original dirty snapshot. It was hash-verified in a disposable copy and applied to this checkout on October 28.

Protocol/ALPN and native rules are 17; invitation format is 4. The installed Android build matches protocol 17 and passes the bounded cross-device UI/connection checks described above. macOS is validated; Linux is not runtime-tested and Windows hosting remains unsupported. The later HyperDHT checkpoint adds public discovery and confirms internet joining in both directions across one Wi-Fi/cellular-hotspot pair. Other NAT combinations, human multiplayer/audio testing and extended endurance remain unvalidated. Original Rune servers and campaign co-op remain outside scope; no owned relay service has been deployed.


## October 28 presentation follow-up (protocol 18)

The tablet playtest exposed missing multiplayer footsteps and other scripted sounds,
vanishing deaths, and abrupt stair camera height. The working build now relays bounded,
expiring server-authored sound cues and replicates corpse/debris actors with their
inherited skeletal model, signed scale, appearance and animation. PlayerPawn transfers
the death animation to a carcass immediately; that actor must remain after respawn.
Original default-weapon drop exclusions remain unchanged. Nonstarter equipped weapons
still follow the original drop rules. Corpses/debris are capped at 32/96 newly tracked
actors respectively; the authoritative game is not modified when a presentation cap is
reached. Dead actor disposal still follows the original scripts.

The camera smooths grounded vertical stair changes over 80 ms, resetting on a new life,
airborne movement, large vertical jumps or long frame gaps. This is presentation only;
it does not change collision or promise to eliminate all prediction corrections.

Validation: the native script/replica probe passes the immediate death-animation
handoff, nonstarter hammer drop, death sound capture, retained corpse, and nine gibs
created by the real ChunkUp script. Negative authored gib scales are preserved.
Two rendered desktop clients pass movement, combat, throw, death and respawn; the
reviewed capture shows the corpse and loose axe. Client audio accepted footsteps,
landing, kick, hit, throw, axe drop, death and respawn sounds. No human listening test
is claimed. Camera regression checks pass at 30/60/120/144/240 Hz. The three focused
network/prediction/wire tests pass. Native window rendering used 4x AA.

Protocol and ALPN are now 18, discovery topic protocol-18/v1; native rules remain 17
and invite format 4. Both endpoints must update together. The user's active public
match remains on its preserved protocol-17 executable. Android source builds have
passed, but this follow-up has not yet been installed or verified on the tablet.
Evidence: outputs/multiplayer-audio-stairs-20261028-191805 outside the source tree.

Latency clarification: cross-internet validation recorded approximately 33 ms between
world updates, with longest active gaps 112 ms and 78 ms. These are not RTT/ping or
input-to-display latency. No measured live-tablet RTT is recorded in those logs.

Deployment follow-up: the signed protocol-18 APK installed successfully using the
existing key, with MainActivity preferences byte-identical before/after. The Mac
public host restarted with three bots. Android Find games listed it at 3/4 and
joining succeeded (`OPEN_RUNE_CLIENT_JOINED owned=41 actors=40`). USB then disconnected
for the Kishi; automated controls stopped. Tablet audio/death/stair feel and sustained
FPS remain for live review; desktop fixture results are not substituted for that.


## October 28 combat playtest follow-up (protocol 19)

The food report was a multiplayer disconnect, not a confirmed Android process
crash. The host created EatenLegBone with an authored mesh variant outside its
model's range. Rendering already falls back to variant zero, but replication
rejected it and disconnected clients. Appearance encoding and replica defaults
now use that same fallback; malformed incoming appearance indices remain rejected.
The real eating script passes: health 50 -> 70, bone appearance installed, then
continued snapshots and death/gib presentation. Original assets are unchanged.

Shield bashes now play authored blunt swing and flesh/shield impact sounds;
VikingShield's original PlayHitSound function is empty. The existing authoritative
stagger, stun immunity, cooldown and shield-block rules are retained. A new bash
fixture verifies both attacker and target animation on encrypted rendered clients.
The unblocked target recoils 20 units and recovers after about 0.38 seconds. The
native torso/limb pose comparison has maximum matrix-component error 0.006;
head look-at offsets are separate local presentation, not claimed identical.

Original blood spray artwork is retained rather than the smaller procedural
streak replacement. Bounded, allowlisted native blood/mist and hit emitters now
replicate their lifecycle and position; individual particles remain client-local.
Emitters with per-particle or system script callbacks are excluded. Cosmetic
expiry hides the replica without deleting its network identity. Tests initialize
BloodSpray (15 particles), BloodMist (8), BloodSpurt (10), HitWood (15) and
HitMetal (10), then verify expiry and authoritative removal. Newly tracked
cosmetic emitters are capped at 32; no unbounded particle stream is sent.

Stair presentation previously swept diagonally into the riser after prediction
had correctly stepped up. Small reconciliations now try swept up/across/down
movement while preserving wall and ceiling collision. DM-Hildir's native geometry
fixture finds 143 valid rises: clipped presentation steps fell from 23 to zero.
This does not establish that all perceived stair jitter is eliminated.

Validation: network suite 12/12; discovery 4/4; native combat/food/effects/death
fixture; two desktop rendered bash clients with 50 ms delay each way, jitter and
1% loss; and Android plus desktop bash fixture. The signed protocol-19 APK was
installed using the existing key. Android rendered 2520x1680 at 4x AA with saved
world effects, observing both bash and stagger; its short 900-frame engine sample
averaged 107.01 FPS (p95 frame 9.71 ms). This is not sustained combat FPS or an
independent display-presentation measurement. MainActivity preferences remained
byte-identical. Protocol/ALPN and discovery topic are 19; native rules 17,
snapshot 6 and invitation format 4 remain unchanged.

The public Mac host was restarted with the matching executable and three scripted
stress clients. Their behavior is not tactical AI. USB disconnected after the
Android check. The updated public host subsequently confirmed player 4 joining
(pawn 43) alongside all three bots. No further controls were sent. Subjective
impact/audio/camera review remains. The reported black moving
platform/trapdoor is still unresolved: no speculative lighting change was applied.
The exact affected surface/view is needed to distinguish authored darkness from
incorrect moving-brush or shadow lighting.

Evidence outside Git: outputs/multiplayer-blood-stairs-20261028-194641 and
outputs/public-bot-game-combat-20261028-201634. Generated artwork, game assets,
private invitations, saved games and prior packs are not included in source.


## October 28 lift lighting and severed-limb follow-up (prepared)

The exact black lift is DM-Hildir Polyobj2. It reproduces in this engine's
Original and Remastered lighting, including offline play. Its BrushRaytraceKey
samples lighting 224 units above its initial position. Remastered moving brushes
now collect lights per face at their current transform; exposed corners keep a
partially retracted door from losing all light when its center enters a wall.
Stationary lightmaps are reused until their transform, contributing lights or
light values change. Original mode retains authored bake positions. No global
ambient boost or unlit material replacement is applied.

The gong fixture calls the original linked Gong.PlayInstrument event in an
isolated, bounded offline run. Its opened hatch has a separate black stationary
BSP wall: temporarily bypassing lighting during diagnosis reveals the underlying
stone texture. Moving-door lighting does not resolve that baked-world-lighting
area. It remains open work; do not claim the entire hatch is fixed.

A real live-player severing test identified a retained physics base: RagnarLArm
spawned with the amputee as its support actor. Drop started falling without
clearing that support. Rune LimbWeapon actors now release their base on entering
falling physics, including thrown heads/arms; normal landing logic is retained.
The native regression checks a living amputee, independent replicated motion,
render transform and movement of the former owner without carrying the limb.
It failed before the fix with base=Ragnar1 and passes after with base=none.
Wire protocol stays 19; native rules revision becomes 18, so host and clients
must update together. Public host still uses the preserved prior executable
until an explicitly approved match restart.

Mac and ARM builds pass. Laptop captures verify the lift and retracting door
lighting; Android installation, native-resolution 4x AA performance and live
match recheck are pending. Evidence is in the existing
outputs/multiplayer-blood-stairs-20261028-194641 directory. Ideas are recorded in
[IDEAS.md](IDEAS.md), separately from bug fixes.


### Installed follow-up and automatic-door response

The signed update is installed on Android. Native-resolution 2520x1680 / 4x AA
lift capture shows visible wooden planks. Its 900-frame fixed-view sample was
85.30 FPS. A separate Android + Mac bash/stagger check passed (mask 192), with
Android's 900-frame combat sample at 93.91 FPS and p95 10.90 ms. These are short,
different-scene engine measurements, not sustained or displayed FPS. MainActivity
preferences were byte-identical before/after testing. The user's Hardware 50%
default remains intact outside isolated native-resolution tests.

The user's additional door request is scoped to zero-delay TriggerControl Rune
Movers while opening. These now use linear movement instead of slow-start
smoothstep. Closing, the authored movement duration, DelayTime, timed hatches
and Polyobj lifts retain their previous rules. DM-Hildir Mover0 measured 1.94 to
10.13 units of motion by simulation frame 3 (50 ms); MoveTime remains 0.5 seconds.
The explicit --rune-net-door-probe checks the actual scripted opening against its
linear interpolation. Protocol and native rules are both 19 in the final build.
The public host and three scripted clients now run the preserved executable at
outputs/public-bot-game-doors-20261028-210658/SurrealEngine. See its live logs for
tablet admission; do not infer a join from merely seeing the server listing.

The tablet joined the final host through the real Find games UI as player 4 /
pawn 44 alongside all three bots. Control was released immediately afterward.
No commits or pushes were made in this follow-up.


## October 28 stair camera refinement

**Rejected and reverted after human playtest.** The user reported more pronounced jerk, specifically Ragnar moving against a relatively steady background. The experimental height/velocity, pivot and arm changes below have been removed; the signed rollback is installed. Settings remained byte-identical, and the tablet rejoined the running host through Find games as player 4 / pawn 53. Controls are released. The original camera tests pass. Prior stair collision fixes remain. The numerical results below are historical experiment evidence, not a successful stair-feel result. Investigate pawn presentation and snapshot correction before another smoothing change.

The prior height filter damped position but still produced a sudden camera-speed
change at each tread. A critically damped position/velocity filter now retains
the previous average climbing lag while smoothing those changes. A deterministic
24-unit/100-ms stair train at 120 Hz reduced peak frame-to-frame camera velocity
change from 284.90 to 81.38 units/sec, with maximum height lag 30.40 units. This
numerical reduction is not a claim of equivalent perceived smoothness.

Camera collision now starts at the filtered pivot after a safety trace from the
real anchor. Arm length retracts immediately for obstructions and returns smoothly
as stair edges stop blocking it. Look rotation, pawn movement, collision physics,
network prediction and server rules are unchanged. Scope is the interactive
multiplayer camera; this does not change campaign camera code.

Filter/obstruction-arm tests pass at 30/60/120/144/240 Hz, including respawn,
airborne, teleport and reset handling. The real DM-Hildir geometry probe passes
143 stair rises with zero presentation clips and 26,464 camera placement checks.
Mac and ARM builds pass. The signed Android update was installed with permission;
Android native 2520x1680 / 4x AA bash/stagger smoke check passed (mask 192);
its short 900-frame sample averaged 93.61 FPS, p95 10.94 ms. Preferences stayed
byte-identical. This is a combat/performance smoke check; the stair coverage above
is the laptop geometry replay, and human stair-feel review remains. The tablet rejoined through Find games as player 4 / pawn 124, with its saved
Hardware 50% mode and 120 FPS pacing restored; control was released.
Protocol/native rules stay 19; the public host does not require restart. Evidence:
outputs/stair-camera-20261028.


## October 28 local pawn stair presentation

The camera-only experiment above was rejected. A real rendered, authenticated
stair replay exposed a different defect: `replica.present()` first placed the
owned pawn on the older interpolated server timeline. `PresentRunePredictedMovement`
then swept from that point, which could lie inside a stair riser, instead of from
the previous resolved local frame. In the baseline 100-ms RTT run this produced
four backward movements during continuous forward input; the largest was 59.903
world units although the predicted target continued forward.

The local client now retains its resolved presentation origin across frames,
with life/map/large-discontinuity guards. Temporary prediction fallback uses a
65-ms decaying handoff offset rather than switching instantly between timelines.
Collision sweeps remain active. The mesh and camera share one 45-ms stair-height
response; the replica-only mesh offset passes through the common actor/joint
transform so carried weapons, shields and mesh shadow passes follow the body.
Server positions, speed, collision dimensions, animation phases, damage and
protocol/native rules 19 are unchanged. Airborne/dead actors bypass stair lag.
The previous camera-arm/pivot experiment remains reverted.

`Tools/RuneValidation/network_stairs.py` launches a separate Mac host and two
clients with isolated saves/configuration. It records positions before prediction,
after prediction, after collision, and after mesh/camera presentation. Two actual
DM-Hildir flights are covered, including ascending and descending 208/224 units.
It optionally adds 50 ms each direction, 8-ms jitter, 1% loss and 1% duplication.
`--rune-net-stair-smoothing=0` disables the vertical mesh compensation for visual
comparison; it does not undo the collision-origin/handoff fix.

Mac results (render scale 1, 4x AA; no Android runtime claim):

- Final first-flight visual run: zero backward movements in ascent and descent;
  maximum unobstructed body/camera vertical change 0.001 world unit per frame.
- Second flight with impaired networking: zero backward movements both ways;
  maximum unobstructed relative vertical change below 0.001 world unit.
- Forced 20-FPS impaired run: zero backward movements both ways. An earlier
  30-FPS impaired run also passed. A 120-FPS cap is not a measured 120-FPS claim.
- Frame review covered ascent/descent and attached axe placement. The geometry
  probe verifies mesh/equipment offsets leave collision and authority transforms
  unchanged. Existing camera convergence/reset and prediction-core checks pass.
- Camera obstruction is logged separately. Real geometry can still retract the
  camera; the relative-height metric deliberately excludes obstructed frames.

The earlier 143 "stair rises" mainly counted spawn/floor settling, not sustained
stair flights; do not use that number alone as stair smoothness evidence. A broader
navigation-point scan included origins not validated as pawn spawns and exposed seven
clips; its diagnostic log is retained, not reported as passing. The new rendered
routes and continuous-input reversals are the relevant regression coverage.
Screenshot serialization was changed to one buffered write to avoid manufacturing
large CPU stalls during capture. The saved replay uses recorded capture intervals,
not a fixed playback-rate assumption. It is visual evidence, not a benchmark.

Evidence: `outputs/stair-pawn-20261028/` (outside Git), including baseline traces,
final trace/result JSON, reviewed frames, timed video and build/test logs. The
user requested Mac-only testing and needs the tablet for other work. No tablet
commands, installation, device settings changes or public host restart occurred
in this follow-up. Android installation and human review remain pending.

ARM build and APK v3 signing passed. The versioned signed package is
`outputs/stair-pawn-20261028/open-rune-stair-presentation.apk`; it has not been
installed on the tablet. Package and Mac binary hashes are in `checkpoint.json`.


## October 28 Mac validation follow-up

User requested continued multiplayer testing while reserving the tablet for other
work. The current Mac build passes four rendered-client checks with one map per
mode: DM-Hildir (Deathmatch), DM-Team_ships (Team Deathmatch), AR-Champions (Arena),
and HB-Agora (HeadBall). Clients used native window pixels (1920x1200 captures),
scale 1 and 4x AA. Each link injected 75 ms each way, 15 ms jitter, 3% loss and
2% duplication. These are functional tests with four windows sharing one Mac,
not single-client FPS benchmarks or evidence of human melee fairness.

- DM: original suicide/kill scoring, frag limit, intermission, score reset and
  repeated same-map reloads preserve each authenticated session.
- TDM: friendly weapon/kick/bash protection, team score/winner, respawn and
  repeated same-map reloads pass.
- Arena: physical queue, countdown, successive challengers, wins/losses and
  original-script respawn pass. All four clients observed one moved authored
  brush and passed over 11,000 visual/collision transform checks apiece.
- HeadBall: pumps, countdown, use/throw input, consumed/scored head, disabled
  limb scoring, no kill points, respawn and next-match restart pass.
- Native script/replica regression: dual wield and crossed defense, bash sounds
  and target recoil/pose, bloodlust leap/charge, mode protection, Rune Power,
  weapon selection/stowing, appearance, food (health 70), blood/hit emitters,
  released severed arm, retained corpse and nine original gibs pass.
- Authority regression: 12 late-block/weapon-window cases pass with no damage
  rollback; predicted movement/collision and engine snapshot checks pass.
- Rebuilt network suite 12/12 and local HyperDHT suite 4/4 pass. The latter covers
  discovery/admission/datagrams/stale-host removal on a local test DHT, not a new
  cross-internet or NAT-pair test.

A separate 180-second, four-bot stress run combined movement/combat inputs, the
same impairments, a three-second blackout, abrupt player termination, slot
retirement/rejoin and same-map travel. Five matches completed; surviving and
rejoined clients continued installing worlds and exited normally. The intentionally
killed client is the expected -9 exit. This is recovery evidence, not smooth
play: world-update gaps reached 6.448 seconds including the forced blackout and
recovery. This run overlapped rendered fixture work on the same Mac; server rates
ranged 44.6–60 Hz under that shared load, so it is not a capacity benchmark.
RSS samples were retained; the short run does not establish absence of leaks.

### Test corrections

The first attempts exposed three stale fixture assumptions, retained as failed
runs in the evidence rather than relabeled as passes:

1. The fixed-tick DM scoring fixture began after two clients instead of waiting
   for its full four-player roster. Its test-only server gate now waits for all
   participants; normal match startup behavior is unchanged.
2. The harness waited 20 seconds for a host deliberately configured to outlive
   clients by 25 seconds. Its shutdown allowance is now 40 seconds.
3. Arena required an observed moving brush without enabling the current optional
   brush-movement fixture. The harness now explicitly enables that check.

The updated harness also isolates configuration as well as saves, supports native
window rendering and a one-map rotation, and records pass/fail plus executable
hash. DM and Arena were rerun successfully after correction. Normal combat,
protocol and server rules were not changed for these tests.

Evidence: `outputs/multiplayer-validation-20261028-next/` (outside Git). Final
passing rendered cases are `dm-final`, `tdm`, `arena-final`, and `headball`;
`recovery-stress`, `authority`, `native`, suite logs and `checkpoint.json` hold
the other results. TDM, HeadBall and Arena captures were visually reviewed.
Final executable SHA-256: `79e56e5cf562fc98e0e2c367668370997c04db306996057d9dd9116682a7dee7`.

No tablet access, installation, public-host restart, original asset/save changes,
commits or pushes. The latest stair/snow Android build remains ready but uninstalled.
Remaining gates: human combat/audio and stair-feel review on the tablet, the planned
four-hour active-client soak, and broader internet/NAT coverage. The known stationary
BSP darkness at the gong hatch is still unresolved and was not part of this pass.
