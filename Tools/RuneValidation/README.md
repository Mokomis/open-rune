# Focused native regressions

## Multiplayer bot lifts

`bot_lifts_allmaps.py --binary PATH --game PATH --output NEW_DIRECTORY` extends
the selected matrix below to initial inventory of all shipped multiplayer maps
and independent-engine rides of every discovered meaningful stop pair. It
reports unplanned brushes separately; these can be doors, traps or unresolved
platforms and are not counted as passing rides. `--maps NAME ...` limits review.

`bot_roam_allmaps.py` with the same arguments samples one minute of ordinary
input-bot exploration from one normal spawn per map, with original death and
respawn handling. It records travel and prolonged stationary intervals, without
forced routes or equipment. It uses TeamGame for movement and does not certify
every room/secret, all mode objectives, secure transport or FPS. See
[existing-map scope and corrections](../../Docs/RunePort/BOT-MAP-TRAVERSAL.md).

`bot_lifts.py --binary PATH --game PATH --output NEW_DIRECTORY` exercises six
real lift rides in separate server/client engine processes on LostCrypt,
Cathedral and Aqueduct. It isolates saves/configuration and verifies actual
host basing, travel and full departure while the client has no authoritative
base pointer. The local file exchange uses the normal codecs; it does not test
secure socket transport, latency or rendered FPS. See
[supported activation, evidence and remaining gaps](../../Docs/RunePort/BOT-LIFTS.md).

For initial-state discovery only, use `--rune-network-probe
--rune-net-native-combat --rune-bot-traversal-probe --traversal-map=MAP.run
--traversal-plans` with isolated configuration/saves. Discovery success alone is
not an end-to-end traversal result.

## Cinematic physics

The cinematic matrix also runs `--rune-physics-kick-probe` in Original,
Cinematic and Chaos under campaign and multiplayer authority. Each case samples
256 committed kicks per player/NPC victim, validates the two outcomes' landing
and recovery and checks host snapshot state. Fixtures reset damage/bloodlust and
recovery protection between independent samples; normal gameplay retains those
protections. No independent client-side probability roll is added.

`android_physics_environment.py --device DEVICE --output NEW_DIRECTORY` checks
six repeated authored-wall slams and original prop shatters at native/4×.
`--baseline` disables only the added impact/break consequences for comparison.
Both paths preserve normal data/preferences. The native matrix includes
campaign/multiplayer environment cases and the independent world test checks
pre-solver normal velocity against fast glancing contact.

`cinematic_physics.py --binary PATH --game PATH --output NEW_DIRECTORY` checks
Cinematic/Chaos knockdowns, recovery, prop interaction, pose replication and real
save/load using isolated saves/configuration. `openrune_physics_tests` exercises
the independent rigid-body world. `android_physics.py --device DEVICE --output
NEW_DIRECTORY` drives the actual menu into the isolated playground and restores
normal preferences. `android_physics_pickup.py --device DEVICE --output
NEW_DIRECTORY` taps the actual Use/Throw controls on a naturally killed physical
body, verifies binding/release order, and preserves normal data. See [scope and controls](../../Docs/RunePort/CINEMATIC-PHYSICS.md).

## Shadow visibility and offline stairs

`map_visibility_audit.py --binary PATH --game PATH --output DIRECTORY` checks all
installed maps' subtree bounds without playing them. It is portable to Windows
and produces per-map logs and JSON for later review. See
[terrain/shadow follow-up](../../Docs/RunePort/RENDER-VISIBILITY.md).

The existing native corpse fixture adds `--rune-slam-probe` for the aerial move,
replica pose and runtime pickup lifecycle checks. Single/network/team variants
remain selectable. `--probe-slam-gesture` drives bounded renderer input; Android
`slamTouchProbe` dispatches the real touch Views instead. See
[aerial slam](../../Docs/RunePort/AERIAL-SLAM.md) and
[runtime pickups](../../Docs/RunePort/WEAPON-PICKUP-PLACEMENT.md).

`--rune-network-probe --rune-shadow-culling-probe=MAP --shadow-culling-strict`
compares the actual shadow caster list with direct actor visibility, then checks
skinned-vertex containment. `--shadow-roster-bounds` adds all roster geometries
and sampled clips. Use isolated saves/configuration. See
[shadow visibility](../../Docs/RunePort/SHADOW-CULLING.md).

`local_stairs.py --binary PATH --game PATH --output NEW_DIRECTORY --captures`
compares original/fixed offline stair presentation at native/4×, with identical
simulation/camera assertions. `local_stairs_test.cpp` covers rates and resets
under UBSan. The authenticated multiplayer route remains `network_stairs.py`.
`android_local_stairs.py --device DEVICE --output NEW_DIRECTORY` runs the matched native/4× OPPO route and returns to the normal title menu while checking save/preference preservation. It restarts the app; only run after the live session is released.
See [offline stair results](../../Docs/RunePort/OFFLINE-STAIRS.md).

## Moving-lift visibility

`lighting_visibility_bvh.cpp` checks the accelerated world query against an
independent analytic shape and BSP ancestry cases. Build with UBSan and `-I SurrealEngine`.
`--rune-probe --probe-map=MAP --probe-lighting-bvh` compares 6,000 actual level rays
against the original visibility implementation. `android_moving_lift.py --device DEVICE --output PATH`
runs matched native/4× original lift interpolation on LostCrypt and HelLift and
checks exact images/motion while preserving normal data. `--maps DM-LostCrypt.run`
limits it to the one map. It finishes with a normal LostCrypt launch, not an isolated
test session. See [measured results and scope](../../Docs/RunePort/LIFT-PERFORMANCE.md).

## Mover lighting visibility

`world_visibility_cache.cpp` exercises fixed-world memoization with independently
changing moving occluders and ray endpoints; compile with UBSan and `-I SurrealEngine`.
The existing `--rune-probe --probe-map=MAP --probe-polyobj` fixture also compares
the split cached visibility against the complete production query on actual brush
geometry. `android_mover_visibility.py --device DEVICE --output PATH` performs the
matched native/4× LostCrypt comparison, checks exact captures/player state, preserves
normal data and finishes in LostCrypt without diagnostic save overrides. See
[the measured change and limits](../../Docs/RunePort/LOSTCRYPT-PERFORMANCE.md).

## Normal Android save-menu startup

`android_save_menu.py --device DEVICE --output PATH` cold-launches the normal client
without diagnostic save overrides and opens Load game as soon as it is available.
It checks existing quick/checkpoint/first-manual slots against actual save files,
verifies their buttons are enabled and confirms every original save hash remains
unchanged. It never loads or writes a save and leaves the normal Load page open.
This covers the first-frame metadata regression and diagnostic handoff boundary.

## Windows shadow budgets

`shadow_settings.cpp` checks both three/eight-slot receiver layouts, all lanes,
unsigned high-bit behavior, legacy saved choices and the platform capacity clamp.
Build with `c++ -std=c++20 -I . -fsanitize=undefined Tools/RuneValidation/shadow_settings.cpp -o /tmp/open-rune-shadow-settings`.
`menu_structure.cpp` additionally checks the five Windows shadow choices.
The real Vulkan `--probe-shadow-budget` fixture requires native rendering,
4× MSAA and `--save-directory` isolation. See
[Windows shadow lights](../../Docs/RunePort/WINDOWS-SHADOW-LIGHTS.md) for the
68-case GPU coverage, authored-scene checks and performance limitations.

Android now shares the eight-slot layout. `android_shadow_lights.py --device DEVICE --output PATH`
runs actual native/4× GPU checks and authored DM-Hildir PlayerStart4 comparisons.
Use `--cases map-three map-eight` for only the map comparison. It restarts Open Rune,
uses isolated saves and records/restores normal preferences if needed. See
[Android shadow lights](../../Docs/RunePort/ANDROID-SHADOW-LIGHTS.md).


## BSP spans

`bsp_spans.cpp` drives the actual production `BspClipper::DrawSpan` against an independent per-pixel coverage oracle. It checks returned visibility, exact newly visible portal gaps, and accumulated coverage through 100,000 deterministic operations, including empty spans, overlaps, fragmented rows and complete coverage. It does not rely on a copy of the optimized interval algorithm.

Run from the repository root on the existing macOS host:

```sh
clang++ -O2 -std=c++20 -I SurrealEngine \
  Tools/RuneValidation/bsp_spans.cpp SurrealEngine/Render/BspClipper.cpp \
  SurrealEngine/Math/FrustumPlanes.cpp SurrealEngine/Math/mat.cpp \
  -o build/bsp-spans-test
build/bsp-spans-test
```

Expected: `BSP_SPAN_ORACLE_OK operations=100000`.

The pre-fix implementation fails at deterministic operation 0:91 because merging/erasing an interval during its gap walk skips the following gap. Retained logs are in `Docs/RunePort/evidence/`. The optimized implementation walks the affected gaps first and merges the touched intervals once.

The `--rune-probe` runtime also checks property lookup cache semantics: missing properties, case-insensitive names and invalidation while a class layout grows/replaces its property vector, alongside the existing model, VM, joint and package checks. Run it with the user's own game data as documented in `Android/README.md`.

## Actual Android presentation rate

Run the game first, leave it visible, and use the read-only timestamp reader:

```sh
  --samples 60 --output /absolute/path/to/analysis/display
```

Use `--adb /path/to/adb` if needed. It reads the game's BLAST SurfaceView, deduplicates actual-present timestamps, discards history before the first poll, and writes JSON plus raw timestamps. No fresh timestamps means failure, not a zero or inferred FPS. `--samples 60` takes roughly 30–40 seconds including wireless command overhead. Keep the same app instance alive throughout.

On this OPPO, the hardware FPS lines and presentation timestamps exposed 60 Hz even while `dumpsys display` claimed a 120 Hz mode. See `Docs/RunePort/PERFORMANCE.md` before making device-policy changes. This helper does not alter any policy.

## Enemy encounter checks

`android_combat.py` runs one bounded installed-app encounter at a time and asserts the expected damage/death/shield outcome. Use the commands in `Android/README.md`. It force-stops only the prototype and does not change device policy or original assets. Final results and source evidence are in `Docs/RunePort/COMBAT.md`.

## Campaign, weapons and saves

`android_campaign.py --device DEVICE --output /absolute/path/campaign` runs severed head/arms, throw/drop/recovery, isolated save round trips and fresh-process resume, DarkViking damage, and a replenished combat workload. Select cases with `--cases`; run `save-flow` before `save-resume`. It records explicit assertion markers and requires bounded completion. The save flow also checks quick-save/checkpoint overlap and rejection of overlapping session actions. Keep one device test running at a time.

`android_stage_fps.py --device DEVICE --stage combat --output /absolute/path/fps-combat-native` measures actual displayed frames during combat, village, underwater or transitions. Native is the default and the user's current requested test mode. It runs Original visuals at 4x MSAA and leaves the app visible. Combat is sustained with replenished health; transitions use isolated save slots and invoke real exit scripts. These are bounded fixtures, not a full playthrough. See `Docs/RunePort/CAMPAIGN.md` for results and limits.

## Actual touch pickup

`android_touch_pickup.py --device DEVICE --output /absolute/path/touch-pickup` locates and taps real Throw/Use buttons. The isolated fixture positions the player near the thrown axe and supplies a food item, but never injects the Use command. It checks weapon attachment and food healing. This covers the Android → SDL → engine mapping that direct engine-key tests bypassed. See `Docs/RunePort/TOUCH-PICKUP.md`.

## Cross-chapter exit and save continuity

`android_campaign_routes.py --device DEVICE --output PATH` invokes placed exits
through their original Touch scripts and tests source/destination saves, player
values, inventory, held/stowed weapons, attachments and brief continuation. It
requires isolated save directories and native4x. It waits for the source opening
to return player control. Unavailable exits remain explicitly unvalidated;
fixtures do not solve puzzles or demonstrate route traversal.

## Bounded waypoint assistance

`android_waypoint.py` turns and swims/walks toward one selected map coordinate
using ordinary controls. Start with an isolated campaign session visible and
paused in its original Escape menu; the script pauses it again on exit. Example:

```sh
python3 Tools/RuneValidation/android_waypoint.py 56 383 1196 \
  --output /absolute/path/to/local-analysis/wall-approach --steps 4
```

The JSON reports positions, health, and a bounded stop reason; the PNG shows
what the game rendered. This helper does not plan around collision, identify
the correct ledge, or confirm that a scripted event fired. Review each puzzle
on the device and retain a separate save before traversing it. See
`Docs/RunePort/UNDERWATER-CONTROLS.md` for the SinkingShip2 checkpoint.


`navigation_integrity.py --binary EXE --game GAME --output NEW_DIRECTORY` runs
six defensive native graph cases,35 missing-incoming queries across five shipped
maps,18 real-FSM pursuits on three stale-data maps and a LostCrypt lift barrier.
Owned assets and isolated saves are required. No original arrays/assets are
rewritten by production repair; the synthetic guard fixture restores temporary
arrays. See Docs/RunePort/NAVIGATION-AUDIT.md for scope and report review.
