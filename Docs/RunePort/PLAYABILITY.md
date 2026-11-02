# Open Rune — Campaign playability checkpoint — October 24, 2026

## Skeleton placement and dwarf mech combat

The original `USkelModel::SetAnimFrame` adds the serialized PositionOffset to
actor Location and PrePivot before applying actor/model rotations and DrawScale.
See `evidence/Engine.SetAnimFrame.cpp:114–138` and
`evidence/Engine.SkelModelSerialize.cpp:28–33`. The retained REA evidence IDs are
`ev_304592576f3bf480a9e7918feb99a4ed47b908ce9e28210f310e29553d8ee540` and
`ev_f1b299bb1ebb695ff453426f0e19c73a9b4e43aba3ab29d467b7aee775a7df90`.
These identify the original placement independently of the internal FCoords
sign convention, because PositionOffset and Location are in the same sum.

The adapter previously used UEViewer's rotated, negated mesh-origin convention
for ordinary actors. MechaDwarf's (0,0,-50) offset consequently displaced its
body sideways and left its hands too high. Its AI and attack callbacks ran,
but repeated arena encounters produced no damage in either direction.

`GetRuneMeshToWorld` now applies the original world-space offset consistently
for rendering, joint queries, attached-child placement, joint collision and
exact-frame weapon sweeps. Carried items retain their existing local conversion
inside the corrected parent joint frame. Generic meshes retain their transform.
Root conjugation and joint decoding are unchanged; this does not establish
complete original skeleton or carried-item parity.

The mech's Idle geometry changed from Z=1.353..93.702 to -48.647..43.702
relative to its actor; its collision half-height is 47. The torso's erroneous
~50-unit sideways displacement is also removed. Stored frame bounds are
reported for diagnosis, not used as an exact animation oracle.

Device encounters use DM-Hildir PlayerStart2, seed1, 180 frames of ordinary
forward input and repeated ordinary axe attacks, without health restoration:

| Enemy | Enemy health | Player minimum health | Short displayed FPS |
| --- | --- | --- | --- |
| DwarfMech | 150 →120 | -5 | 119.97 |
| SnowBeast | 300 →60 | -5 | 119.97 |
| GoblinWarrior | 70 →-20 | 60 | 119.97 |

All completed 1800 frames at 2520×1680 and 4x MSAA, Original mode, with no script or
native errors. The SurfaceFlinger samples include the end of attacks and the
resulting death/idle states; they do not prove sustained combat120FPS. Hardware
refresh was120Hz. The tablet was plugged in. These are spawned encounter
fixtures, not campaign encounter completion. StoneGuard's 9999 health and refusal
to take body damage are original script behavior; its lack of attacks in the
initial arena still needs a suitable encounter before drawing conclusions.

Desktop and ARM headless integration checks pass, including model-placement
invariance under three actor rotations/two scales, joint attachment/detachment,
springs/accelerators, original camera/wait/water helpers, Polyobj motion and
particle callbacks. Android DeepUnder3 rope climbing/release and isolated
save/exit/reload fixtures also pass after the change. Actual Android screen taps
recover a thrown axe and consume food; those tests also use native4x.

Evidence: `evidence/playability/enemy-transform-fixed/`,
`transform-touch-pickup/`, `transform-campaign-regression/`,
`combat-transform-headless.log`, and `combat-transform-arm.log`.
`mech-before-results.json` records the same seed/start/approach before correction.

## Natural ship progression

The original SailingShip camera/boat sequence reaches SinkingShip without
skipping, triggering an exit manually, or teleporting the player. Each map loads
once; SinkingShip enters PlayerSwimming and releases its cinematic camera.
Real Forward/Jump touch input visibly changes the player's position afterward.
The timed observer initially expired shortly before the exit; continuing the
same process recorded the natural transition. That first timeout was not a game
crash or a successful complete run.

Native4x displayed samples were116.91FPS at the ship entry and 118.80FPS around
the transition/swimming. These initial samples did not prove full traversal. The subsequent
UNDERWATER-CONTROLS.md follow-up now verifies ordinary wreck traversal and
the original SinkingShip →SinkingShip2 cinematic/exit. See `evidence/playability/ship-progression.json` and its
retained logs/screenshots. Previously verified RagnarVillage →RagnarVillage2
also remains a natural cinematic transition. The October 24 joint-blocking
follow-up now demonstrates the intervening village hall pickup, Ulf defeat and
alarm through real Android controls; see JOINT-BLOCKING.md.

## Remaining campaign scope

Short entry checks load maps, execute their opening scripts and measure displayed
frames. They do not prove enemy encounters, puzzles, exits, save/load in each
level, or completion of the full campaign. The route checklist remains the
source of those outstanding traversal requirements. Continue gameplay blockers
before further graphics tuning, keeping native4x for device tests.

### Later entries and longer opening checks

All 24 entry checks from Goblin2 through Asgard completed without script/native
exceptions. They include the reachable Thor, mountain, dwarf and Loki maps;
individual states, presentation timing and warnings are recorded in
`evidence/playability/campaign-later-entries-{a,b}/results.json`.
All verified 2520×1680, Native, Original and 4x MSAA. Most samples measured 117–120FPS;
Dwarf1wwheel measured 104.09FPS and DwarfMap2 measured 115.11FPS. These performance
gaps remain recorded. No clock, thermal, charging or unrelated device settings
were changed to force a result.

Longer 65-second observations of TrialPit and THORmap5B reached PlayerWalking
from Scripting without intervention. Their samples measured 109.58 and 117.80FPS,
respectively. Diagnostic reach warnings identified idle PHYS_None goblins/dark
vikings seeking navigation points, plus a crucified Wolfgar. Original Reachable
also rejects dry PHYS_None for those non-pawn targets; these logs did not prove
a broken cinematic. Both opening sequences completed. See
`campaign-navigation-diagnostic/` and `../Engine.Reachability-notes.md` in evidence.

Known compatibility warnings are retained, not counted as fatal script errors:
LANGUAGE/ISADDON console queries, WeaponSwipeBlue generic-particle update and
BubbleSystemOneShot relative-emitter transform. These remain implementation
limitations. Entry success must not be read as full visual/gameplay parity.

### Close-range reachability correction

Original `actorReachable` accepts a pawn within
`self.CollisionRadius + min(self.MeleeRange, 1.5*self.CollisionRadius) + target.CollisionRadius`
after pain/water-capability/visibility guards and before destination placement
or physics dispatch. The port omitted that early return, so even a nearby pawn
could be rejected while the caller was stationary, falling or interpolating.
The Rune-only early return is now implemented. Dry PHYS_None still rejects
more distant/non-pawn targets, matching the original; that intentional result
no longer produces a misleading unimplemented warning.

Desktop and ARM tests invoke original native 520 through the VM. They cover three
physics states, inclusive radius/low-melee boundaries, full 3D distance, a real
blocking wall, pain zones, swimming capability, non-pawn/null targets and
unchanged caller position. Evidence is in `reach-close-{desktop,arm}.log`.
This is a compatibility correction, not proof that it caused the observed
TrialPit/THORmap5B warnings. Full reachability parity remains incomplete: original
water-first dispatch, dry-swimming walk/jump fallback and target-placement/nav
filter differences are documented in `Engine.Reachability-notes.md`.

Visual spot checks confirmed visible world/characters in the waterwheel and
Asgard entries. Asgard also shows dark geometry near the top of the view; that
artifact remains unresolved. Directly loading an ending map does not reproduce
its preceding campaign state or establish ending-sequence correctness.

### Final installed build

Final APK SHA-256: `e486d8db8ff94b7bf6114422077e8c52c31bb1ccc01fa4db387e4a0340050a19`; installed bytes match.
Package `org.runeclassic.port`, UID 10300 and refresh rule
`persist.opdrr.42800e61=3` are preserved. Four normal Save0/Save10 file and backup
hashes match the checkpoint preceding these playability tests. Tests use isolated
save directories; original assets and normal slots are not included in Git.

Final APK reciprocal-damage assertions pass for DwarfMech, DarkViking and
GoblinWarrior. Their short native4x displayed samples measured 119.59,119.58 and
119.17FPS. See `enemy-reach-final/` and `installed-build.json`. Exact health and
AI states are retained; these remain bounded arena cases rather than level wins.

Final APK TrialPit smoke test also completed its opening and restored
PlayerWalking, with 119.62 displayed FPS at native4x. The intentional dry-idle
reachability warnings are gone; console-query and weapon-swipe limitations remain.
See `campaign-final-reach/`.
