# Windows shadow-light budgets — November 2, 2026

**Later Android follow-up:** the eight-slot renderer and all five menu choices are now also installed and tested on Android; see [Android shadow lights](ANDROID-SHADOW-LIGHTS.md). Statements below about Android retaining three slots describe the initial Windows-only delivery. Mac still retains three. The Windows implementation and measurements below remain unchanged.

Windows now offers **Video → Lighting & shadows → Shadow-casting lights → One / Three / Four / Six / Eight nearby**. Remastered lighting is required. Changes apply live; the setting is a maximum, and does not create more light sources or brighten the scene by itself. Saved indices 0 and 1 retain their old one/three meanings. The existing Windows preference remains three. Android and Mac retain their three-light capacity, atlas and receiver encoding.

## Rendering changes

Windows uses eight 1024×1024 D32 depth tiles in a 4096×2048 atlas (32 MiB, previously 12 MiB). Resolution per light stays unchanged. Both shadow-pass and subsequent scene-node viewport updates now address the correct row/column. The first GPU run exposed the old scene-node override writing slot five outside its intended tile; that is fixed and all eight tiles pass.

The receiver's eight lighting lanes use four bits each on Windows; unsigned shifts preserve the sign bit in lane seven. Mesh and BSP receivers use the same encoding, checked C++/GLSL packet offsets and grid coordinates. Android/Mac keep their original two-bit encoding and 3072×1024 atlas. Coincident light-source matches choose one slot rather than combining incompatible slot identifiers.

Higher budgets examine up to 32 touching lights rather than eight, score eligible sources with the existing distance/brightness and selection hysteresis, then render up to the requested count. Legacy one/three budgets retain their previous candidate limit. Radius, flicker-effect exclusions, focused 120-degree shadow projections toward the player and eight-light receiver limit are unchanged. This is nearby focused shadow coverage, not omnidirectional point-light cubemaps or whole-map shadows. Large budgets can increase CPU geometry work as well as GPU depth/receiver work; no platform default was raised.

Windows shadow views now use conservative frustum bounds rather than software occlusion-span rasterization. These shadow views never accumulate CPU occlusion spans; Vulkan performs final clipping and depth tests. Normal camera/portal visibility and non-Windows shadow views retain the original path. `--shadow-frustum-cull=0` retains the reference path for diagnostics. `--shadow-lights=N` sets a bounded diagnostic budget; `--trace-shadows` reports actual eligible/active counts.

## Validation

Windows native Vulkan, RTX 4080 SUPER / Ryzen 7 7800X3D. Render fixtures use a **1024×768 window, render scale 1, 4× MSAA**, fixed seed and 120 Hz simulation. This is native window rendering, not a fullscreen monitor-resolution benchmark.

- `--probe-shadow-budget`: 68 actual GPU pixel cases pass, covering all eight slots, BSP/mesh receivers, the highest-bit lighting lane, moving silhouettes, masked casters, grouped/ungrouped passes, invalid indices and inactive-frame reset. Opposite silhouettes in neighboring slots detect incorrect atlas selection. Isolated saves are mandatory.
- The full existing Vulkan suite passes: dynamic shadows/contact guards, material maps, water/continuation, alpha/depth/hit paths, visual-mode toggles and capture.
- Authored DM-Hildir view selects four eligible lights with the eight-light maximum. Matched reference/optimized captures are byte-identical at both three and four active lights. The 1,200-frame goblin encounter also has identical final pixels and matching sampled player/enemy/camera outcomes.
- Actual Windows menu clicks cycle through all five choices, verify the active budget and persistence, and preserve water/AO feature bits and native/4× settings. The existing saved three-light index loads correctly. The menu's expanded help fits the Details pane.
- UBSan CPU tests cover three/eight-slot packing, all eight receiver lanes, invalid indices, sign bit, saved values and platform clamp. The menu structure test checks all 42 settings and the five shadow choices.
- Final Mac and Android ARM64 source builds pass. No tablet installation or device tests were performed for this Windows-only change.

## Bounded performance results

Same binary/seed/map/simulation and settings, reference software visibility versus conservative Windows shadow culling:

| Fixture | Active lights | Reference mean | Optimized mean | Reduction |
|---|---:|---:|---:|---:|
| Fixed view, 240 sampled frames | 3 | 21.43 ms | 20.16 ms | 5.9% |
| Fixed view, 240 sampled frames | 4 | 20.66 ms | 18.69 ms | 9.5% |
| Goblin encounter, 1,080 sampled frames | 4 | 29.73 ms | 28.68 ms | 3.5% |

The combat sample includes attacks, a kill and subsequent corpse presentation; it is not a continuous crowd stress test. Its p95 changes 32.16→31.86 ms. GPU scene time in the four-light fixed view stays about 0.207→0.206 ms; this workload is dominated by CPU scene preparation. The encounter's measured engine throughput is about 34.9 FPS, not locked 120 FPS. These are bounded instrumented runs, not sustained fullscreen performance or proof across all maps/hardware. A higher shadow maximum will not improve FPS. All eight slots are validated synthetically; the authored view only exercises four simultaneous lights.

## Delivery and evidence

The default Windows executable and `SurrealEngine-Menus-20261101.exe` launcher are updated to the tested binary. The previous launcher is retained as `SurrealEngine-Menus-20261101-before-shadows-20261102.exe`. Normal Windows settings are byte-unchanged, including three shadow lights and Original sound effects. No match/server was launched, saves overwritten, tablet settings changed, commit made or push performed.

Evidence outside Git: `outputs/shadow-lights-20261102/` above `work/SurrealEngine`, including exact scripts, build logs, pixel/trajectory comparisons, menu captures, executable hash and deployment record. Final Windows binary SHA-256: `fdf10b34d140809a9833ac89d22bf849324adf9035392c844fcfe1feb80f771c`.
