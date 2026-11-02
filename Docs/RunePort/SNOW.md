# Outdoor snow — October 28, 2026

## Behavior and controls

Video and Enhancements share **Snow: Off / Light / Heavy**, saved as `menu.snow`.
Default Off retains original map weather. Original comparison temporarily disables
the new effect without losing the saved selection. Light and Heavy replace the
rendering of original Rune `Snow` emitters, while leaving their actors/scripts
alive; switching Off restores authored snowfall. Other particles are unchanged.
Both campaign and multiplayer render through the same local weather code. Weather
has no server actors, gameplay collision, replicated state or protocol changes.

Soft flakes have world-anchored falling phases, gentle lateral drift, varied size
and distance fades. The main view uses the existing batched particle renderer;
no extra full-screen pass, particle shadows, dynamic light list or new shader is
required. It is not snow accumulation, terrain whitening or gameplay weather.

## Cost controls and shelter

- A 21 by 21 rolling cache covers a 640-unit-radius neighborhood. Only eight new
  columns may be sampled per frame; static BSP results are reused until the cell,
  camera-height band or map changes.
- Five traces across each footprint reject mixed roof/sky samples. Downward traces
  set the ground/roof floor, so flakes above a roof cannot fall below it. Water
  zones are excluded. Narrow geometry between samples remains a coverage limit;
  this is conservative sampled shelter, not an exact precipitation simulation.
- Nearby blocking moving brushes use their current world bounds each frame, so
  moving roofs/trapdoors cannot retain stale exposure data. Bounds are deliberately
  conservative and can shelter more than the brush's exact shape.
- Light/Heavy use 4/10 candidate flakes per cell, at most 700/1800 visible quads,
  and estimated screen-coverage budgets of 2.5%/6%. Quads are depth tested and
  sorted back to front. Their budget is separate from combat effects.
- Off returns before weather work. Maps without sky surfaces also return early.
  Sky portals, shadow passes and mirror views do not render extra snowfall.

## Mac validation

Native **2816 x 1762**, **4x MSAA**, Apple A18 Pro / MoltenVK. Vsync was disabled
only in isolated benchmark configurations; user display settings were not changed.
The test uses two rounds, Off/Light/Heavy then Heavy/Light/Off, with 60 warm-up and
900 sampled frames per run. Separate save/config directories preserve normal saves.

| Fixed view | Off FPS | Light FPS | Heavy FPS | Off / Light / Heavy GPU scene ms |
| --- | ---: | ---: | ---: | --- |
| RagnarVillage courtyard | 72.60 | 73.64 | 72.88 | 5.979 / 5.962 / 5.946 |
| Mountain2 canyon | 67.26 | 69.86 | 70.13 | 7.205 / 6.828 / 6.849 |

FPS is computed from combined mean frame times, not the best sample. Village
results are within run variation; Mountain2 replaces 69 existing snow emitters'
rendering and has wider CPU/timing variation. These results show no consistent
regression in these views, not a general FPS improvement or Android guarantee.
Snow's last 300-frame CPU averages were 0.20–0.25 ms Light and 0.26–0.38 ms Heavy.
Warm static views performed zero additional BSP traces. Initial cache fill is
included in the earlier logged samples. These are fixed views, not a sustained
combat benchmark or every-map certification.

Headless geometry checks: RagnarVillage 1 exposed / 22 closed navigation points;
Mountain2 6 / 181; DM-Hildir 0 / 33 and DeepUnder1Odin 0 / 67. No exposed point
failed its independent upward-sky check. The indoor maps have no sky surfaces.
40,004 pure checks cover fall speed, periodic continuity across camera translations,
negative world coordinates, density limits and default Off. Java menu model tests
cover persistence, shared choices, reset/clamping and original comparison.
Mac and ARM builds pass; the signed APK packages successfully. Android runtime and
physical menu/controller checks remain untested because the user reserved the tablet.

## Reproduction and evidence

- `Tools/RuneValidation/snow_render.py`: isolated sequential native/4x comparisons.
- `Tools/RuneValidation/snow_test.cpp`: standalone motion/budget checks.
- `--snow=0|1|2`: desktop launch selection; `--snow-profile` logs bounded cost.
- `--snow-probe=MapName --save-directory=...`: headless geometry validation.
- Local evidence: `outputs/snow-20261028/final-village`, `final-mountain`,
  `geometry-final`, and the Off/Heavy PNGs in `outputs/snow-20261028`.
- Signed build copy: `outputs/snow-20261028/open-rune-snow.apk` (not installed).

No tablet access, original asset changes, normal save changes, commits or pushes.
The existing public multiplayer host was left running unchanged.

## Blizzard stress preset (October 28 follow-up)

The user requested an exaggerated blizzard test on the Mac.
`--snow=2 --snow-blizzard` enables a diagnostic preset without changing the saved
Off/Light/Heavy menu choices: 120 candidate flakes per cell (12 times Heavy),
2.3x falling speed, stronger bounded gusts, larger diagonal flakes, an 18,000-quad
ceiling and a 55% projected-quad coverage budget. The actual visible count in the
courtyard is about 3,000 quads. The coverage metric is summed quad area, not the
percentage of opaque white pixels. Roof tests and distance fades still apply.

`Tools/RuneValidation/snow_render.py --presets off heavy blizzard` compares them
in one test series. The first two runs averaged 78.06 and 87.92 FPS, with snow CPU
windows around 2.0–2.8 ms. Heavy was 74.99–76.71 FPS but much cheaper at about
0.35 ms; blizzard frame-time p95 was worse (21.06–23.11 ms versus Heavy's
19.41–19.47 ms). Other timing components changed substantially between launches,
so the higher average FPS must not be interpreted as a snow performance gain.
The refinement removes redundant normal-snow calculations from the blizzard path
and expands edge culling to accommodate elongated flakes. Final measurements are
recorded separately below. No new menu option, APK installation or tablet testing.

Final native 2816x1762 / 4x runs (`outputs/snow-20261028/blizzard-final`):
blizzard **76.38 / 82.86 FPS**, p95 **21.85 / 21.97 ms**; Heavy
**73.64 / 75.77 FPS**, p95 **20.06 / 19.30 ms**. Blizzard's warm CPU
windows were **1.91–2.39 ms**, versus **0.31–0.38 ms** for Heavy. Roughly
3,145–3,165 versus 250–259 quads were drawn. GPU scene timings varied from
5.62–6.27 ms for blizzard versus 6.04–6.11 ms Heavy. The raw average-FPS
ordering remains confounded by varying non-snow CPU/presentation timing; the
repeatable finding is much higher particle CPU work and worse frame-time tails.
No measured improvement is claimed for the small calculation cleanup.

Reviewed preview: `outputs/snow-20261028/blizzard-final.png`. Mac build and
existing motion tests pass. Existing Android APK remains the prior non-blizzard
package. Keep this as a command-line experiment pending visual preference and
future device testing.
