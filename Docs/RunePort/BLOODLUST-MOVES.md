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
