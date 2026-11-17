# Outdoor breath and physics naming — November 9, 2026

Living Ragnar, NPCs, multiplayer bots and remote player models emit short, soft,
world-space breath clouds when exposed to the sky. The effect uses the existing
Enhanced Fire & combat effects choice. It does not require snowfall to be enabled.
A mouth/head attachment follows the interpolated skeletal pose. Dead bodies,
underwater/pain zones, roofs, solid scenery and movers suppress the clouds.
Each cloud is checked for shelter, so an outdoor cloud cannot drift into a room.

Undead class ancestry, explicit undead flags and original undead mesh/skin pairs
are excluded. This includes zombies, Dark Warrior and Sark player/bot variants,
even when their visual model is placed on a living player class. Mechanical/stone
creatures are also excluded. Undead idle chest/spine tracks now hold the first frame of the current idle's
stance, removing the breathing cycle. Root/pelvis, limb and head tracks remain
animated; living models, non-idle combat/movement clips, cinematic/rope/swimming
poses and dead bodies remain unchanged. The correction runs before animation
tweening and combat/IK/physics overlays, including independent upper-body proxies,
legacy frame sampling and the separate character-browser renderer. It does not
change animation clocks, collision, damage or networking and is independent of
the vapor/Enhanced effects setting.

Presentation is local and shared by single player and multiplayer. It does not
spawn gameplay actors, consume gameplay randomness, replicate particles or add
lights. Nearby visible living emitters are capped at32, retained puffs at96 and
drawn quads at48 within the existing shared192-quad effects budget. Clouds last
0.8 seconds and occur in three small puffs about every2.4–3.2 seconds. Exact sky
traces are limited to emission and active puffs; no whole-map ray scan runs per frame.

## Physics settings

User-facing labels are Legacy(0), Cinematic(1) and Standard(2). Standard retains
the former Chaos behavior and is the default for fresh/reset settings and fresh
hosts. Legacy retains the original gameplay physics. Cinematic remains the gentler
intermediate choice. Existing explicit saved values, protocol IDs and host rules
retain their meanings. Online clients continue to follow their host's physics.
The C++ Original/Chaos names remain source aliases for Legacy/Standard.

## Validation and limits

Native Windows and Mac probes check37 roster/skin variants:28 living and9 undead
variants, plus a swapped zombie mesh on a living class and dead-body suppression.
Exposure checks found25 outdoor/21 covered navigation samples on DM-Seaside,
6 outdoor/81 covered on DM-Thorstadt and0 outdoor/33 covered on DM-Hildir.
These samples establish the shared exposure rule, not an exhaustive all-map audit.

Windows isolated on/off captures show a small breath-only image change by the
living character's face; the zombie remains clear. A covered three-character
preview emitted no puffs. Windows diagnostic captures used1024x768/4xAA; they are
not native-display performance evidence. A Mac outdoor capture rendered2816x1762
with4xAA and was visually reviewed. A second comparison resized during fullscreen
transition, so no matched FPS/percentage gain or loss is claimed from that pair.

Windows native multiplayer combat regression passes495 snapshots/8,159 ticks.
Its original-contact baseline now explicitly selects Legacy, rather than inheriting
the new production default; existing explicit Standard tests remain intact.
Existing Java menu/settings/navigation/laws checks and native physics checks pass.
The pre-existing desktop menu_structure checker fails on migration-only setting
keys in both the unchanged baseline and candidate; it is not a new runtime failure.

Primary Windows, Mac and Android builds pass. The signed Android update is installed
in place with the original key; game and host were already stopped before updating.
All15 protected save/configuration file hashes remained unchanged. The installed
ARM environment also passes the37-variant/exposure native probe; its temporary
executables were removed afterward. This is a headless logic check, not an Android
visual/FPS result. The app was left stopped and no match was started.

Builds and package status are recorded outside Git in
`C:/OpenRune/outputs/physics-settings-20261109` and the corresponding Mac
`~/OpenRuneArtifacts/PhysicsSettings-20261109` folder. No sustained Android FPS result is claimed for this feature.

Diagnostics: `--rune-breath=0|1`, `--trace-rune-breath`,
`--rune-probe --rune-breath-probe`. `--breath-demo=outdoors|indoors` is an opt-in
three-character preview that requires an isolated `--save-directory`; it changes
only that diagnostic session's camera/actors. Android demo extras require bounded
`frames` and an isolated `saveTest`.


## Undead idle follow-up

Windows, Mac and ARM native checks cover all37 roster/skin choices and every
available sequence at two phases.3,420 breathing joint samples were corrected,
1,670,922 unaffected joint samples remain identical and702 undead idle samples
retain motion in other tracks. Actual body/proxy sampling, an undead mesh swapped
onto Ragnar, corpse exclusion and a half-way idle tween are also checked.
Existing37-character animated/rotating menu previews pass;56 living preview images
(two frames per28 living variants) are pixel-identical to the previous build.
Nine undead second-phase images change, while their first-frame stance is retained.
Representative Zombie, Dark Warrior, Sark Spawn and Sark Ragnar previews were
visually reviewed. Existing native combat passes495 snapshots/8,159 ticks.

Primary Windows, Mac and Android builds pass. The matching Android APK is installed
with the original certificate while game and host were already stopped; all15
protected save/configuration file hashes remained unchanged. The ARM pose checks
pass and temporary executables were removed. No new match was started and no
Android graphical/FPS result is claimed for this follow-up. Evidence is in
`C:/OpenRune/outputs/undead-idle-20261109` and the matching Mac artifact directory.


## Complete undead idle-cycle correction (dev18)

The previous chest-only correction still allowed authored pelvis and shoulder
tracks to move the torso. A PlayerZombie weapon1_idle sample retained about0.66
model units of world-space torso displacement despite frozen local spine joints.
Undead now hold the first-frame stance of breathing/idle clips as a whole, before
transition blending, independent aim/guard, IK and physical hit reactions. Walking,
attacks, climbing, swimming, cinematics and dead-body animation remain unchanged.
This applies to undead campaign actors, multiplayer models/bots, animation proxies,
model swaps and character previews, independently of the vapor-effect setting.

The updated probe checks all idle joint transforms in world space, every other
sequence for exact preservation, living idle motion, proxies, previews, model
swaps, dead bodies and tween continuity. See the PlayerNames-20261109 receipts for
current build/visual qualification. No tablet installation is implied.


## Undead mannerisms

Instead of breathing, undead now remain unnaturally still between occasional
crooked head turns, a brief hand twitch and a head-back, open-jaw silent scream.
An11–16 second individual clock spaces the gestures; its three types alternate
with long quiet gaps. Actor/model identity staggers timing without consuming
simulation randomness. Missing jaw joints simply omit that part of the pose.

Only the head, jaw and one wrist rotate; chest/pelvis tracks stay fixed. No bone
positions/scales, damage, collision or sound change. The shared body/proxy and
preview sampler handles campaign NPCs, bots and undead player models. Movement,
non-idle animation, attacks, guard, kicks, bashes, slam and rush take priority.
Independent look/IK/hit presentation and normal transition blending still follow.
Cards use their own preview clock; native pawn fields are never read from class
default objects, which expose only script-property storage.

Evidence: OpenRuneArtifacts/UndeadGestures-20261109, including all37variant pose
and preview checks, fixed-camera gesture images and final build receipts. Follow
the report for actual platform testing/install status; no tablet update implied.
