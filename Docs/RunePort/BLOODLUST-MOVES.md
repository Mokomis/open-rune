## Leap, camera boom and longer rush candidate — 2026-11-08

Candidate dev.6/build6, gameplay revision34. **Not deployed**; tablet remains
build5/rules33. Windows live match was stopped at the user's request. Do not
restart it automatically.

Open-ground leap launch speed now spans the supported landing arc instead of
reaching a short distance cap and zeroing XY speed halfway through flight.
The vertical650 impulse stays unchanged. Wall/ledge approaches retain the quick
800 speed and rely on normal swept collision, with a universal600 safety cap.
The previous1.2-second midair brake is removed; existing move recovery remains.
Low ceilings still physically interrupt an ascent. Campaign leap remains off.

Shield rush now travels at1150 units/s (was900), up to800 units (was560).
Preparation, shield/wall collision, damage and knockdown policy are unchanged.
Charging pawns and replicas draw continuous shield-edge wind arcs/slipstreams;
no effect actors, dynamic lights or physics queries are added. A bounded quad
budget and existing visual-effects switch apply to player and bot presentation.

Mac bloodlust runtime suite passes:800 clear-ground travel,800 over a grate lip,
stepped terrain, walls, shield blocks, campaign AI rush/no-leap, and saved-move
cancellation. Native multiplayer combat regression passes495 snapshots/8159ticks.
Two rendered clients pass leap/charge replication with40ms delay each direction,
8ms jitter,1% loss and1% duplication. Native fullscreen replay requests4xMSAA.

DM-Skorir camera boom now anticipates grounded stair floors and eases obstruction
release while clamping every frame to its actual safe collision length. Native
2816x1762 replay (requested4xMSAA) reduced largest camera frame displacement from
54.79 to20.06 units. This is route-specific measured evidence, not an all-map
smoothness or sustained-FPS claim. Body/camera height alignment remains intact.

Historical ledge replay assertions, when re-enabled, failed with both the original
baseline and this candidate. Their existing campaign-only harness scope was
restored; no fresh ledge-grab coverage is claimed. Evidence and continuation:
`~/OpenRuneArtifacts/LeapSmoothing-20261108/continuation.json` and
`~/OpenRuneArtifacts/SkorirStairs-20261108/native-comparison.json`.

# Latest scope: campaign rush only; multiplayer leap and rush

November 8 client presentation: giant-leap takeoff now bridges the transition from
local movement prediction to authoritative special motion, retaining the previous
visual velocity for the handoff frame and easing the bounded offset over 65 ms.
The existing collision sweep resolves the visual continuation. Landing transitions
are bridged too; death, knockdown, teleport and life/map changes reset normally.
Ordinary walking handoffs retain their prior behavior. Gameplay physics, range,
damage and compatibility are unchanged (wire25 / gameplay33).

A single native-resolution / 4x-AA Mac client, headless peer and real encrypted
host were exercised with 60 ms delay each way, jitter and 1% packet loss. The old
transition dropped25.454 world units in one17ms frame; the final change continued
upward2.86 units in a28ms sampled frame instead. This is a bounded takeoff result,
not a general FPS or all-latency claim. Existing native combat495snapshots/8163ticks
passes. Versiondev.5/build5, original-certificate APK SHA
74f7bac0934d0e72b21b6c88adf0dc4751844491d8c6f4b81626b7eb7d4e9a88
is installed on OPD2515, with saves/preferences unchanged at installation. Tablet
rejoined live DM-Skorir. Windows host remains running its compatibledev.4 binary;
no host restart was needed. Evidence: LeapSmoothing-20261108 in Mac artifacts.

Follow-up reports under investigation: camera stair traversal on DM-Skorir and
horizontal leap interruption halfway through an arc. These are separate from the
corrected takeoff handoff; do not mark them fixed based on the takeoff test.


November 7: giant leap is now prohibited for both Ragnar and enemies in the
single-player campaign, regardless of rage or the normal-combat option. The shared
readiness/activation gate removes the leap cue and rejects direct leap requests;
ordinary jump/climbing and the separate two-handed aerial slam are unchanged.
Shield rush remains available in campaign without rage. A real autonomous campaign
enemy decision test starts its rush before earning any rage and confirms no leap.

Multiplayer bots use both moves without rage in Deathmatch, Team Deathmatch, Arena
and HeadBall planner tests. A shield no longer prevents choosing leap: rush closes
a shorter gap/counters an observed guard, while leap closes a longer gap. Full
travel/landing terrain checks and friendly corridor checks gate those decisions;
a pending second leap tap is cancelled if its observed enemy dies/disappears.
Native tests exercise ordinary authority-admitted jump/double-tap/block gestures,
not forced action bypasses or secretly supplied meter. The bounded mode checks use
one shared map fixture with each mode's planner status, not full map coverage or
new public online matches. Existing boss/script and simultaneous-attacker limits
remain. No new campaign leap toggle can bypass the restriction.

Release 0.1.0-dev.3/build 3, gameplay revision 32(shared kick timing), wire protocol 25.
This supersedes the earlier notes that made giant leap available in campaign.

# November 7: shield-rush ragdoll reaction

A committed shield-rush hit knocks an eligible living opponent backward into an
articulated ragdoll, followed by bounded automatic recovery. This works in Original,
Cinematic and Chaos physics, without changing the saved ambient physics setting.
Original mode runs only the active combat ragdoll; it does not automatically adopt
corpses or activate loose-prop physics. Idle simulation time continues so recovery
protection timers do not freeze when no combat body remains.

A raised shield retains its lower damage and stops the charge, but its holder is
still knocked down. This does not change the special shield protection for kicks.
Rush bypasses the ordinary knockdown re-entry grace; its existing move cooldown
remains. Boss/scripted/heavy-target, valid-contact and friendly-fire protections
remain; if a body cannot be created, the existing recoil is retained. The host
commits the reaction after the existing late-block window and sends normal physical
pose snapshots to clients. No client predicts knockdowns or applies its own impulse.

Native Windows tests: unblocked rush in all three modes, real backwards travel,
physical snapshot roundtrip/client presentation, living recovery, unchanged ambient
mode, and a raised-shield impact with reduced damage and recovery. Existing single-
player rage/charge/shield/wall/enemy/save tests also pass. The old stationary-target
fixture now actually holds its target still instead of letting its AI jump/slam
away during the wind-up; this changes only the diagnostic fixture, not enemy AI.

# November 7: normal combat is now the default

Giant leap and shield rush are enabled without bloodlust in campaign, desktop/
Android hosting and headless server defaults. Normal-combat mode never spends
bloodlust, even if rage is already active. The shared four-second cooldown,
shield/equipment, animation, movement and enemy-eligibility gates remain.

Combat moves are labeled **Giant leap & shield rush**; **Leap & rush availability**
defaults to **Normal combat**, with **Bloodlust only** retaining the legacy behavior.
Existing Android/Windows preferences receive this default once through the
normalMovesDefaultV2 marker; subsequent explicit opt-outs are kept. Other options
and saves are not reset. New host configurations default on. Hosts always serialize
an explicit `--rune-normal-bloodlust-moves=1` or `=0`; a headless server defaults on,
accepts the historical bare enable flag, and uses `=0` for the old rage requirement.

Because meter consumption changes simulation, gameplay compatibility revision is
31 (previously30), wire protocol remains25. Release identity is0.1.0-dev.2/build2.
Client/host updates must be coordinated; no running match is changed automatically.
Native combat tests cover normal combat with empty AND full bloodlust, both moves,
real bot gestures and authority admission, retained cooldowns, and legacy opt-out.

The older notes below describe the original rage-only implementation.

# Bloodlust leap and shield charge

October 27, 2026. Native moves for Ragnar and selected Dark Vikings; [private multiplayer integration](NETWORK-NATIVE-COMBAT.md) now uses server authority.
Gameplay and Enhancements / Combat share **Bloodlust moves: Off / On** (default On).

## Controls and availability

- Double-tap Jump within 0.32 seconds for a giant leap. Movement input selects
  direction; stationary input uses Ragnar's facing. The first tap remains an
  immediate ordinary jump. Controller A and keyboard Space use the same binding.
- Hold Block and press Attack for a shield charge. A live, visible, attached
  shield is required; a shield stored while dual wielding cannot charge.
- Reaching full strength activates the original bloodlust state. Moves remain
  available during that state while at least 50% of the maximum meter remains.
  Each accepted move costs 50% and starts a shared four-second cooldown. Meter
  decay and damage absorption continue normally. Falling below 100% does not
  instantly remove the opportunity; spending or losing the required 50% does.
- Available touch controls pulse amber: **Leap · 2×**, and **Hold Block** with
  **+ Attack**. Charge cues require a shield. Cues clear during cooldown,
  unavailable states, pause and layout editing. Controller connection continues
  to hide touch actions while retaining Menu. No layout or opacity is reset.

## Movement, collision and enemies

The leap launches at 800 horizontal / 650 vertical units per second, compared
with Ragnar's normal 425 vertical jump. It uses existing falling collision and
has a 600-unit horizontal bound, with support/hazard checks at the intended
landing. Ceilings and walls shorten it. Selected enemies aim a shorter leap
near their opponent instead of overshooting by the full player distance.

The charge has a fixed facing and a 0.52-second player preparation (0.68 for enemies),
then moves up to 560 units at 900 units/second. Each enemy can be hit once, with
15 base blunt damage and a 100-unit swept shove. Raised facing shields reduce
this to 3 base damage / 8 units and stop the charge; large enemies and bosses
stop it with only small recoil. Walls stop it through swept movement. Damage
can spawn actors, so contact scanning does not retain invalidated iterators.
Existing damage rules can further modify these base amounts. Normal attacks,
kicks and bashes cannot overlap an active special move.

Preparation first plays a quick 0.24-second stow, then blends into a two-handed
shield brace with a lowered head. Forward movement starts only after that
preparation. During the run the original leg cycle continues beneath the brace;
a 0.40-second recovery draws the weapon again. The same pose applies to eligible
enemies. Unsupported skeletons do not advertise or accept the charge.

The real selected weapon follows a temporary stow attachment frame, using its
sword, hammer or axe slot. Inventory links and the selected weapon remain intact;
no cosmetic duplicate is spawned and existing spares are not displaced. Attack,
throw, drop and switching are locked during the rush. Both rendering and native
attachment queries use the same modified skeleton. Cancellation, death and load
clear the transient pose; normal recovery blends back to the hand.

A stable hash of saved actor names selects about one-third of non-boss Dark
Vikings. Their rage builds from actual combat health damage (one point taken,
half a point dealt), activates at 100, and drains five points/second. Active
rage adds the player-style damage modifier and damage absorption. A faint red head halo
and visible wind-up signal the threat. Enemies choose moves only in combat,
with line of sight and appropriate spacing; their leap has a 0.45-second
wind-up and uses the existing Viking jump animation. Tactical defenders can
recognize a visible bloodlust move at charge range. Other species and scripted
scenes are not given these moves.

The native state is per pawn and transient. Loading cancels pending impacts;
enemy rage and special cooldowns reset on load. The existing player strength
and inventory save behavior remains. Multiplayer now sends explicit leap/charge
intents and replicates meter, phase, cooldown, readiness and pose state. The server
validates and simulates moves; clients only present them. See the separate
[multiplayer checkpoint](NETWORK-NATIVE-COMBAT.md) for its tests and limits.

## Validation

`--rune-tactics-probe=bloodlust` requires an isolated `--save-directory`.
Desktop and ARM64 runtime checks cover availability/cost, bounded leap, charge
wind-up and single impact, raised-shield stop, solid-obstacle stop, toggle-off,
real enemy rage gain, enemy charge/leap completion, a tactical AI charge choice,
and mid-charge save/load. Rush checks additionally cover both hands ahead of the
chest, actual weapon stow position, restoration on cancellation, no movement
during the first 0.45 seconds, and preservation of the weapon across a mid-stow save.
The isolated reference lane measured 554.6 units of leap travel and 171.1 units
of height. Original dual-wield, Dark Viking, goblin and dwarf regressions pass
separately with the new moves disabled to retain their baseline assertions.

Android integration uses `bloodlustProbe=true` plus `saveTest` to grant a shield
and fill bloodlust once. `bloodlustGesture=leap|charge` dispatches timed real
Android touch-view events, including two simultaneous fingers for Block +
Attack; it does not call the engine moves directly. The general touch probe
also covers cue masks and controller-hide behavior. These diagnostic gestures
are never scheduled on normal launch. Screenshots verify amber appearance and
return to ordinary controls after spending the meter. Original assets, normal
saves and saved render preferences are preserved; tests use native 2520×1680
and 4× MSAA session overrides.

Evidence: `evidence/bloodlust/`; full-size screenshots remain in the workspace's
`outputs/bloodlust/android/`. Bounded arena tests are not proof that the larger
leap cannot bypass every campaign puzzle. No campaign-wide traversal or live
multiplayer claim is made.

The 21.66-second Android presentation sample, spanning one touch-triggered
charge and the surrounding idle view, averaged **72.52 FPS** at native 4× MSAA.
Its p95 frame interval was 20.84 ms, with no intervals over 50 ms; the panel
remained at 144 Hz and the Open Rune refresh rule remained 4. This is not a
sustained fight benchmark or evidence of a locked 120/144 FPS. Inactive amber
cues do not continually invalidate the touch buttons.

## Two-handed rush revision (2026-10-27)

Installed and checked on OPPO after the pose revision. Desktop and ARM fixtures
pass the added stow, hand placement, forward shield orientation, delayed movement,
cancel/restore and mid-stow inventory preservation assertions. The existing dual
regression also passes. Laptop front, rear and moving views were inspected; the
shield uses its attachment frame independently of the original forearm guard.
Both actual Android touch gestures still pass, including readiness cues clearing.

A new 21.78-second sample spanning a single rush and surrounding idle frames
averaged **84.75 presented FPS** at native 2520×1680 with 4× AA; p95 was 13.89 ms,
p99 20.84 ms, maximum 27.78 ms, and no intervals exceeded 50 ms. Hardware panel
remained at 144 Hz and the app refresh rule remained 4. This is a bounded smoke
check, not a sustained fight or a controlled performance improvement comparison.
Evidence files are prefixed `rush-`; captures remain in `outputs/bloodlust/rush/`.

For hands-on repetition, `bloodlustProbe=true`, `bloodlustInfinite=true` and an
isolated `saveTest` on Android hold the live player's meter at maximum. The CLI
equivalent is `--probe-bloodlust --probe-bloodlust-infinite --save-directory=...`.
This session-only practice flag replenishes spent/decayed strength; the normal
four-second cooldown remains. Normal launches and campaign saves are unaffected.

## Repeat-leap and quiet bloodlust update

Player leap planning searches supported, unobstructed destinations at 600, 450,
300, 150, 60 and 0 units. A nearby wall or short floor can shorten the move into
an upward leap instead of silently rejecting the combo solely because the
full-length endpoint is unsuitable. Collision, hazard checks, meter cost and the
four-second cooldown remain. Enemies retain their previous target planning.

The original player's berserk start, loop and end sounds are filtered at audio
playback; existing loops are also stopped. Ordinary voices, pickups and ambient
sounds remain. Enemy rage activation no longer adds an extra battle shout.
A small soft red halo follows the actual head joint for living bloodlust-active
Ragnar and eligible enemies. It uses one shared generated 64×64 texture, world
depth testing and camera fading; it does not add lighting or shadow passes.
Original bloodlust eye effects/transition flashes and the enemy orange body tint
are replaced by the halo. The halo disappears on death or when bloodlust ends.

Desktop runtime checks pass three consecutive leaps without resetting the pawn,
plus the original combat/charge/save checks and selective bloodlust audio filter.
ARM runtime passes the same repeat and audio-filter checks. The installed Android
build also completed three real touch double-tap leaps, five seconds apart,
without resetting or repositioning Ragnar. The third leap shortened in the
arena; cooldown cleared and 100% practice readiness returned. Native 2520×1680,
4× AA was verified in the render log. Halo appearance was inspected on laptop
and tablet; this does not constitute campaign-wide leap traversal testing.
Evidence: `halo-desktop.log`, `halo-arm.log`, `halo-repeat-touch.log`.

## Ground-following rush and stronger halo

The rush now uses swept up/forward/down movement when its straight sweep hits
terrain, bounded by the pawn's normal `MaxStepHeight`. It follows walkable floors
with the existing `TryStepToGround` support/base handling. An unsupported ledge
ends the rush and restores falling; failed step attempts do not leave the pawn
raised in the air. Pawn collisions and real walls still stop/resolve normally.
Range counts horizontal progress, so stair height does not consume travel.

Clear-ground distance is 560 units (formerly 450, approximately 24% longer), at
the same 900-unit speed. Preparation, cost and cooldown are unchanged. The head
halo radius increases from 12 to 14 units with a modest increase in red intensity.

Desktop collision fixtures in DM-Hildir pass a full 560-unit rush, an 8-unit
raised grate lip (560 travel), three ascending 12-unit steps (37.2 maximum rise),
descending steps, full-height wall stop, shield stop, and existing combat/save
checks. These are controlled blocking geometry fixtures, not a claim that every
campaign staircase or grate has been tested. Evidence: `ground-rush-desktop.log`.

The same ground-rush collision and combat suite passes on Android ARM64. The
installed native 2520×1680 / 4× AA build completed a real touch-triggered 560-unit
rush and returned to full practice readiness. The stronger halo was visually
checked on laptop and OPPO. See `ground-rush-arm.log` and `ground-rush-touch.log`.

## Rage-jump approach to climbable ledges

A failed jump at the side of DM-Hildir's stairs stopped roughly 56 units before
the wall. The coarse 150/300-unit fallback shortened the jump outside the
original four-unit grab reach. Player leap planning now considers the actual
swept distance to the first vertical obstruction before those fallback lengths.
The endpoint still requires supported, non-hazardous ground. The 600-unit cap,
normal collision sweeps, original ledge clearance checks and original scripted
pull-up animation remain. Enemy leap planning is unchanged.

Three real-map approaches (starting Y=1124,1200,1280, X=-325, Z=-170) failed to
grab before the fix. All three now grab, finish the pull-up, return to walking
on the stair surface at Z=43, and clear the native leap state. The probe also
rejects grabbing from the initial positions away from the wall. These checks
plus the existing bloodlust combat, charge, repeats and save-cancellation suite
pass on laptop and Android ARM64 at a 120 Hz simulation step. This proves these
replayed approaches, not that every ledge on every map is climbable.
Evidence: `ledge-before.log`, `ledge-desktop.log`, `ledge-arm.log`.

Installed APK touch tests pass both leap and charge with a verified native
2520×1680 render target and four samples. The OPPO app refresh rule is still 4
(144 Hz). Returned to an isolated full-meter practice session afterward. The
three stair approaches were checked through the headless runtime on the tablet;
the visible touch checks exercised the moves at the arena spawn.
