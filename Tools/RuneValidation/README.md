# Focused native regressions

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
python3 Tools/RuneValidation/android_present.py --device 192.168.68.51:37547 \
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
  --device 192.168.68.51:37547 \
  --output /absolute/path/to/local-analysis/wall-approach --steps 4
```

The JSON reports positions, health, and a bounded stop reason; the PNG shows
what the game rendered. This helper does not plan around collision, identify
the correct ledge, or confirm that a scripted event fired. Review each puzzle
on the device and retain a separate save before traversing it. See
`Docs/RunePort/UNDERWATER-CONTROLS.md` for the SinkingShip2 checkpoint.
