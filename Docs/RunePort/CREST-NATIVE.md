# Native Crest water — Android qualification, November 9, 2026


## Implementation

The actual MIT-licensed Crest 4.23.0 spectrum/FFT kernels (upstream commit `db0658ff0b2e93e4a9e28cc2867509658b0ecc00`) run as native Vulkan compute: 128×128, 16 cascades, six rendered wave bands, persistent compression foam and filtered wave mips. Source, upstream originals, reproducible shader generation and notices are in `Thirdparty/crest`. There is no Unity runtime or commercial Crest 5 dependency.

Android Video's Water rendering setting adds **Crest (experimental)** alongside Original, Enhanced and Full scene (legacy). Switching to/from Crest requires restarting the app; this is explicit in the hint/notice. Existing saved water selection is preserved. No new default has been forced.

The Vulkan device now enables supported extended storage-image formats, validates float-format/filter/blit capability and checks compute pipeline creation errors. Crest needs these capabilities; the OPPO Adreno 829 reports them. Other Android GPUs have not been qualified.

Crest has a dedicated surface pipeline, keeping legacy/ordinary water shaders separate. Constant wave-band sampling is unrolled. On an in-pass MSAA renderer the background capture reuses the already resolved colour rather than resolving it twice. A packed wave/foam experiment produced no meaningful gain and was removed.

## Visual corrections approved in playtesting

* Distant skybox water retains its authored geometry and horizon. Its miniature coordinates must not receive playable-ocean displacement; its wave shading remains animated.
* Underwater carrier geometry blends toward the flat gameplay water boundary. FFT normals, scene transmission and underwater distortion remain animated, without exposing moving above-water BSP slivers.
* Underwater absorption uses camera-to-surface distance, not the distance to scenery in air beyond the surface.
* Refraction distortion is bounded. Absorption uses the undistorted scene depth, with smooth foreground/discontinuity rejection. A warped depth lookup formerly made shallow floors flash between bright transmission and deep absorption.

The user approved the corrected visuals after the final refraction-edge fix. Earlier frames/numbers are retained as development evidence, not all accepted as final.

## Android results

OPPO OPD2515 / Adreno 829, proprietary Qualcomm driver, actual 4× MSAA. Native is 2520×1680; 50% renders 1260×840 with Android hardware scaling to the display. Fixed camera, matching feature selections, 120 FPS target; these are engine timing measurements, not independent display-presentation counts or combat/thermal guarantees.

| DM-Seaside beach | Enhanced | Final Crest |
| --- | ---: | ---: |
| Native | 113.12 FPS | 45.41 FPS |
| 50% | 119.96 FPS | 118.46 FPS |

Waterwheel representative checks before the last refraction-edge cleanup measured Enhanced/Crest at 65.82/60.18 FPS native and 69.01/67.43 FPS at 50%. That view is substantially CPU limited at 50%; those earlier figures are not a fresh final-shader retest. Native above/below and 50% moving-view checks render successfully. The six-thousand-frame above/below motion fixtures also retain screenshots through the animation.

Independent inverse-DFT checks against actual Adreno GPU readbacks pass all 16 cascades (maximum absolute error about 0.00000364 m). Foam is finite, nonzero and bounded 0–1. Actual render-target dimensions/sample counts are asserted in each check. All 12 protected save/settings/INI files remain byte-identical. The app's normal title menu is restored; settings are unchanged.

## Boundaries

The current wind/choppiness/height preset is shared, with depth attenuation; there is no automatic calm-pool/storm classification. Gameplay water volumes remain flat. This is an adapted Crest 4 simulation/material, not complete Crest: buoyancy, full underwater volume scattering/total internal reflection and reflected rooms/actors/SSR are not provided. Mac Crest qualification remains outstanding. Native beach performance is still costly; the 50% option is retained and useful. Ocean-foam improvements are explicitly deferred in `IDEAS.md`.

