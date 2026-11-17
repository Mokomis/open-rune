# Outdoor snow — October 28, 2026

## Current behavior and controls — November9,2026

Outdoor maps choose automatic weather at load in campaign and multiplayer. A
per-map seed chooses Clear/Light/Heavy with40%/35%/25% weighting. Each
map has a seeded3–7 minute interval; conditions are reevaluated at each interval
and may remain the same. Changes fade over20seconds. Rendering remains cosmetic,
with the existing roof/moving-roof/water shelter and no terrain accumulation.

**Snowfall override** is a client setting: Automatic / Always Off / Always Light /
Always Heavy. Automatic is the default. Always Off suppresses
both added snow and original Rune Snow emitters. The other overrides force only
the local presentation; they do not change the host or other clients. Original
comparison alone restores authored Rune weather. Overrides use the new
snowOverride storage identity, so old fixed-weather indices are not accidentally
interpreted as new overrides. Existing unrelated settings are preserved.

Campaign weather uses a separate cosmetic seed and clock; it does not consume
original gameplay randomness or add actors/scripts. Map/save loads start a fresh
weather schedule. No save format or original assets are changed.

In multiplayer the host publishes a nonzero seed and host-clock map origin in
reliable authenticated MatchStatus metadata. Clients use the existing clock
synchronization to calculate map age, including late joining and reconnecting.
The origin is converted to an age using synchronized current time so a match
that predates a joining device's boot still works. The seed/origin remain constant
until map travel; no per-flake packets or periodic weather broadcasts are needed.
The local override never changes this shared timeline. dev17/native rules37
requires matching hosts/clients for the12-byte status extension. Snapshot schema,
combat rules and wind-ups are unchanged.

Density, coverage, flake size, wind and light vary smoothly. Integrated fall phase
prevents a falling-speed change from teleporting particles. Fully settled modes
retain separate calm/storm fast paths. Maps without sky surfaces return early;
indoors and underwater remain sheltered even with Always Heavy selected.

The historical October measurements below predate automatic weather. The final
November9 Blizzard stress comparison is also retained; it is a fixed weather
comparison, not proof of dynamic-weather or populated-match performance.

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
- `--snow=0|1|2|3`: desktop launch selection; `--snow-profile` logs bounded cost.
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

## Historical Blizzard menu option — November 9, 2026

Superseded by the removal below; retained only as historical test evidence.

Blizzard is now the fourth shared desktop/Android snow choice (saved index3).
Existing Off/Light/Heavy selections retain their values. It promotes the existing
extreme preset:120 candidates per cell,2.3x fall speed, strong bounded gusts,
larger diagonal flakes, an18,000-quad maximum and55% projected-quad coverage cap.
All static/moving roof rejection, water exclusions and sky-only rendering remain.
This is cosmetic and local to each client, independent of host gameplay rules.

`--snow=3` selects it directly. The earlier `--snow-blizzard` diagnostic switch
remains compatible. Both the live Android setting and standalone desktop/network
launch paths accept the fourth value.

Current native/4xAA Mac comparison evidence and limitations are recorded under
`C:/OpenRune/outputs/blizzard-20261109` and controller
above are not current measurements. No tablet deployment is implied.

### Current DM-Seaside result

The final same-process comparison uses DM-Seaside in RuneMultiPlayer mode, an
identical fixed beach view, actual native2816x1762 backing drawable and4xMSAA.
World features200614, three shadow lights and Crest water model3 stay constant.
After600 warm-up frames, settings run Off/Heavy/Blizzard/Blizzard/Heavy/Off in
600-frame blocks. Each block discards120 switch/warm-up frames and measures480.
There are no bots or connected remote players in this weather-only view.

| Snow | Combined FPS | Mean frame ms | Repeat FPS range |
| --- | ---: | ---: | --- |
| Off |40.26|24.841|38.51–42.16|
| Heavy |40.84|24.486|40.20–41.50|
| Blizzard |36.90|27.098|36.84–36.97|

FPS uses the combined mean frame time, not a mean of rounded FPS. Blizzard costs
about2.26ms versus Off and2.61ms versus Heavy in this view: an8.3%/9.6% FPS loss.
Its p95 frame times are33.69–35.49ms, against Heavy24.73–25.52ms. Heavy's small
apparent advantage over Off is within repeat variation, not a weather speedup.
Warm snow preparation is roughly1.36–1.55ms with about2,150–2,250 drawn quads,
versus about0.10ms/175–186 quads for Heavy.

The earlier separate-launch village, Thorstadt and Seaside runs used a different
(base-rendering) feature setup and showed launch variance; their higher raw FPS
must not be combined with this enhanced-rendering result. One village reverse run
was closed before completion; it is retained as interrupted evidence, not a pass.
Thorstadt's narrow exposed patch was not the representative Blizzard stress view.

Use `--probe-snow-cycle` with bounded frames and isolated saves for this comparison;
4200 frames complete the warm-up and six blocks. The flag is inert in normal play.
Normal preferences and saves were preserved. Mac/Windows builds,40008 snow motion
and budget checks, and Java persistence/clamping/reset/original-comparison checks
pass. Android source includes Blizzard but was not built or installed in this task.

## Automatic-weather validation

and Windows `C:/OpenRune/outputs/variable-snow-20261109`. Seed14 with a bounded,
isolated6-second test interval renders Clear→Light→Heavy→Blizzard→Light→Clear
on DM-Seaside at actual native2816x1762/4samples. This accelerates the clock only
for validation; real intervals remain3–7minutes. Weather tests check seeded
weights, transition continuity, overrides, different client clocks, late joining
and a map origin predating local boot. Network codec tests include independent
wire fixtures and reject truncated/malformed weather metadata. Normal settings
and game saves remain separate from validation profiles.

CLI `--snow=-1` follows automatic weather;0..3 force Clear/Light/Heavy/Blizzard.
`--snow=-2` is the original-weather comparison. `--snow-weather-test` requires
bounded frames and isolated saves and must never be used for gameplay benchmarks.

Final automatic-weather checks pass on Mac and Windows, including independent
wire fixtures; Android ARM native compilation also passes. A normal native/4x
RagnarVillage campaign startup selected Light with no override/test clock. The
actual encrypted two-client Seaside match shared the host seed/origin, admitted a
late joiner and retained that client's Always Off override, with798/649 received
world snapshots. The native/4x accelerated weather captures were reviewed through
all four states. No APK installation or tablet runtime qualification occurred.

## Blizzard removed — November 9, 2026

Only Automatic, Always Off, Always Light and Always Heavy are available in the
Android and desktop menus. Automatic weather now selects Clear/Light/Heavy with
40%/35%/25% weighting and keeps the existing interval, fades and shared timeline.
Older saved Blizzard overrides, `--snow=3` and the legacy `--snow-blizzard`
diagnostic alias all fall back to Heavy. The native runtime also caps direct
overrides and transition appearances at Heavy, so old values cannot enable the
extreme particle budget. Light and Heavy visuals are unchanged. No gameplay or
network schema changes are needed. Historical Blizzard timings above do not
describe a currently selectable mode.
