# Two-handed aerial slam

Double-tap Jump within 0.32 seconds, then press Attack while airborne. The first
tap remains the ordinary responsive jump. The second prepares a slam for 0.75
seconds. Starting the two-handed attack gives one upward launch (600 units/s),
then the completed flip drives downward at at least 850 units/s. Repeated Attack
cannot stack lift. An ordinary double-tap without Attack still gives no free
second jump.
An available bloodlust leap retains its existing behavior, and its prepared
two-handed attack can transition into the slam.

The move requires a live heavy sword, axe or hammer. The five light weapons
accepted by dual wield are excluded, as are held body parts and non-weapon
inventory. The heavy weapon families include the dwarf blades and player Sark
weapons. It cannot overlap dual wield, corpse carrying, kicks or shield bashes.
No bloodlust meter is required for the ordinary-jump combo.

## Animation and combat

A 0.45-second wind-up rotates the whole skeleton forward around its hip center,
with the authored heavy-weapon attack pose blended into the flip and downward
strike. Fractional samples remain enabled. The root rotates as well as the
torso; joint lengths and scales are preserved. Held equipment and shadow bounds
use that same skeleton. Swept actor collision and zone gravity remain live;
ceilings can clip the launch. The original latent animation entry can clear
velocity after the move starts; the same launch is restored once, rather than
boosting repeatedly. Edge grabbing is suppressed during the move. Separating
upward pawn contacts do not stop the launch; incoming contacts remain blocking. The strike commits only after landing following
the wind-up, rather than dealing damage at an airborne timer. A long drop
cancels the move after 1.25 seconds of dive without a remote hit or immunity
to ordinary fall damage. Recovery lasts 0.32 seconds, with a two-second move
cooldown.

The committed strike selects one forward target in bounded melee reach with
world line of sight. Its base damage is 1.5 times the weapon's Damage property (clamped to 10–100
before multiplying), with the original weapon damage type. For example, the
Work Hammer submits 68 damage from a base of 45, before normal game modifiers;
then the original target damage callbacks apply. A facing raised shield stops
the damage and limits recoil. A crossed dual-weapon guard is weaker, allowing
reduced damage. The server uses the existing contact deferral and 33 ms late
block opportunity; damage does not occur early in the flip or repeat on landing.
This is a separate native move, not a promise that every original powered
weapon's special scripted swipe effect is invoked by the slam.

Death, weapon loss, invalid movement/state, and saving cancel the transient move.
Camera overrides and scripted campaign states cannot initiate it. Scripted and
cinematic targets are excluded, including the deferred server contact. Custom
recoil requires a defended hit or actual damage accepted by the original
callback; boss and heavy-target recoil is limited and mass-scaled. Eligible
non-boss Viking combatants can choose it when already airborne, near their live
enemy, facing it and able to see it. Multiplayer bots use the same authenticated
Jump → second Jump → Attack inputs as players, with a six-second decision
cooldown. Defenders recognize the visible wind-up; bots do not read future input.

## Multiplayer and validation

Protocol/ALPN remain **23**; native combat/movement rules are now **24**. Snapshot native phases 6/7 represent
flip/recovery and carry fractional age through the existing interpolation path.
Older clients cannot safely consume the new state and must use matching builds.
Discovery uses the matching protocol-23 topic. No campaign save-format change
is required.

Mac and Windows native single/network/team fixtures cover heavy/light eligibility,
the actual Fire hook, no early damage, one heavy contact, facing-shield defense,
cooldown, death cancellation, replicated flip state, NPC choice and scripted-state
protection. On the Mac, 1,980 sampled roster poses additionally check finite
transforms, actual root rotation and unchanged bone lengths/scales. A native
2816×1762/4× MSAA rendered Jump/Jump/Attack fixture completes all four pose stages
and a real hit; clear camera captures were visually reviewed. The ordinary
camera can hide Ragnar when jumping beneath a low ceiling, so the pose review
uses an isolated, world-traced side camera and disables only test camera fading.

All 15 sanitized network tests and discovery tests pass. Android touch integration
uses `slamTouchProbe` with isolated `saveTest` and at least 900 bounded frames:
it dispatches the actual Jump/Attack Views, without native key injection. A
connected controller's hidden Views can be shown briefly by that explicit test
and are restored afterward; normal preferences are not changed. The OPD2515
actual-touch check passed all four pose stages and a real hit at native
2520×1680 with 4× AA; the initial 600-frame sample averaged 107.45 engine FPS, with a later 101.13 FPS repeat.
This short diagnostic is not sustained combat or proof of 120 displayed FPS.
All 68 Android shadow GPU cases and full/half/zero fade readback pass.
Deployment hashes and final repeat results are recorded in the evidence folder.

Evidence: `outputs/visibility-slam-weapons-20261103` outside Git. These fixtures
are not a full campaign or exhaustive latency/gameplay certification.

Higher-flight follow-up: native single/network/team checks additionally cover
launch preservation, no stacked boost, no airborne timer hit, landing contact,
long-drop cancellation and actual blocking pawn contacts in both directions.
A rendered Mac native4× check measured222.62 units of ascent from the test
floor, observed the dive and committed one hit. Its360-frame sample was77.24
engine FPS in a different diagnostic lane, not a before/after performance claim.
Hidden map lights reached full shadow strength in that same integrated test.
Evidence: `outputs/slam-air-20261103`.

The landing callback commits contact before original BaseChange/JumpOffPawn
behavior, including near-vertical head landings. Repeated bounce contacts do not
repeat damage. This was corrected after the Android test exposed a miss despite
working flight and shadows. Mac/Windows single/network/team fixtures and the
actual Android touch check now cover this path. OPD2515 native 2520×1680/4×
AA measured 221.79 units of rise, a dive and one hit, with real hidden-light
shadow strength 1.0. Its short 600-frame sample averaged 113.14 engine FPS;
this is not a sustained combat benchmark or a before/after performance claim.
