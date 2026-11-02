# Native 4x lighting optimization — October 26, 2026

The installed Android build keeps all remastered effects enabled and improves
native-resolution rendering by separating discard-capable camera fading from
ordinary draws, and using relaxed precision for bounded lighting values.
This is shared renderer work, so both campaign and multiplayer benefit.
It does **not** establish 120/144 FPS throughout the game.

## What cost the most

OPPO OPD2515, 2520x1680 internal and display resolution, 4x MSAA, worldFeatures
8103, bloom, adaptive sharpening and 16x anisotropic filtering. The panel remains
144 Hz. GPU clocks sampled at 1.225 GHz; no clock/thermal/power policy was changed.
USB remained connected. Isolated saves and a fixed camera were used throughout.

The first 600-frame GPU sweep in the character-visible DM-Hildir view measured:

| Change from all-effects baseline | GPU scene time | Reduction |
| --- | ---: | ---: |
| Baseline | 11.039 ms | — |
| Original per-pixel lighting instead of Remastered | 6.786 ms | 4.253 ms |
| Material maps off | 10.011 ms | 1.028 ms |
| One shadow light instead of two | 10.326 ms | 0.713 ms |
| Bloom off | 10.433 ms | 0.606 ms |
| Surface depth off | 11.028 ms | 0.011 ms |
| Surface detail off | 11.025 ms | 0.014 ms |
| Baseline repeated | 11.035 ms | 0.004 ms |

These are overlapping effect costs, not additive savings. Original per-pixel
mode also bypasses Remastered material/specular/dynamic-shadow work. The separate
attempt to disable bit 256 while leaving Remastered bit 512 enabled was invalid:
the menu settings correctly restored the required bit; the engine logged 8103.
It is explicitly excluded from conclusions. Sharpening affected the present pass
(about 0.692 to 0.455 ms), not the dominant scene cost.

## Changes retained

- The ordinary fragment shader no longer contains camera-coverage discard and
  uses early fragment tests. Masked surfaces and camera-fading actors keep their
  discard-capable paths, so holes preserve background depth. Separate visible
  and invisible coverage pipelines preserve the original color-write rules.
  This reduces expensive shading of hidden pixels, particularly in the water
  view. It does not lower resolution, AA, light count, shadow resolution or
  material quality.
- Unit lighting directions, light colors, material response and accumulated
  lighting permit GLSL `mediump`/SPIR-V relaxed precision. World positions,
  distances, derivatives, UVs and shadow projection remain high precision.
  Actual packed arithmetic is driver-dependent. This change alone saved about
  0.5 ms of GPU scene time in the hall; it is not a claim that every GPU gains.
- Diagnostic reference switches remain available: Android
  `--ei opaqueEarlyDepth 0 --ei fullPrecisionLighting 1`; desktop
  `--render-opaque-early-depth=0 --render-full-precision-lighting=1`.
  Defaults are optimized (1 and 0). These are process options, not saved menus.
- `probeTickHz` / `--probe-tick-hz` permits matching simulation frames for
  renderer comparisons. It requires bounded frames, isolated saves and 10–1000
  Hz. It does not change normal game timing or multiplayer server timing.

## Measurements

Actual SurfaceFlinger presentation timestamps, approximately 13 seconds per
sample, native 2520x1680 and 4x MSAA with effects unchanged:

| View | Reference displayed FPS | Optimized displayed FPS |
| --- | ---: | ---: |
| DM-Hildir, character visible | 80.39 | 89.24 |
| Dwarf1wwheel, water view | 50.42 | 61.87 |
| Interactive multiplayer, fixed camera | 79.99 | 88.59 |
| Live goblin encounter | 72.29 | 76.63 |

Live combat and water animation evolve in real time; these samples are not
identical simulation trajectories. The multiplayer camera/settings match, but
network timing and actor poses can vary. They are short local measurements, not
long-session or whole-campaign guarantees. A 144 Hz panel does not mean 144
newly rendered frames per second.

A separate fixed-120-Hz simulation test rendered the same 760 frames (600 timed
frames after warm-up), including identical combat observations in all 11 samples:

| Fixed-step workload | Reference engine FPS | Optimized engine FPS | GPU scene ms before → after |
| --- | ---: | ---: | ---: |
| Hall | 80.46 | 89.46 | 11.024 → 9.841 |
| Water wheel | 50.15 | 65.28 | 15.867 → 12.113 |
| Goblin encounter | 72.09 | 79.95 | 12.441 → 11.126 |

Fixed-step FPS measures rendering throughput; it is not real-time play speed or
displayed FPS. The combat samples match positions, animation/state, enemy health
and shield damage. The optimized network client's measured receive/install work
is 0.076 ms/frame; presentation/input/render costs remain separately logged.

## Correctness and remaining limits

- Android production GPU regression passes: color/depth/alpha, hit readback,
  continuation, camera coverage, invisible coverage depth, world/mesh lights,
  eight-light cases, material normals/roughness, moving/masked shadows, contact
  shadows, water/reflection/cache/approximation, and clarity. This is the engine's
  pixel fixture, **not** a claim that the Vulkan validation layer was enabled.
- Full-resolution fixed-frame image comparisons retain the appearance. Mean
  absolute 8-bit channel differences are 0.027 (hall), 0.072 (water), and 0.036
  (combat); 99th-percentile differences are 1, 2 and 1 respectively. Differences
  are not zero: the largest single-channel combat difference is 61, confined to
  a few edge pixels (12 of 4,233,600 pixels exceed 10 in any channel).
- Two rendered desktop clients pass the interactive server regression, including
  movement, combat, jump, throw, death and respawn. The normal desktop rendered
  smoke test also completes.
- User save hashes and preferences match the pre-test backups. The OPPO per-app
  144 Hz policy remains value 4. Normal saved scaling is still Hardware 50%; all
  measurements here explicitly override it to native without saving the override.
- The complete Mac GPU fixture cannot run on this Apple/MoltenVK combination:
  its integer hit-target MSAA resolve is unsupported. Both reference and optimized
  paths encounter that limitation. Android's fixture passes; a rendered desktop
  smoke test uses the existing omit-unused-hit path. Do not describe the full
  Mac fixture as passing.

Evidence: [render-lighting-performance](evidence/render-lighting-performance/),
including the effect sweep, reference/optimized profiles, presentation timestamps,
full-resolution captures and pixel comparison statistics. Generated APKs and
signing keys remain outside version control.

Reproduce fixed-frame checks:

```sh
python3 Tools/RuneValidation/android_render_cost.py --device DEVICE --output NEW_DIRECTORY
```

Add `--real-time` for presentation measurements, or select `--cases hildir water`.
The script is currently sized for this OPPO's native 2520x1680 target. It restarts
Open Rune and requires the tablet unlocked and untouched. Multiplayer uses
`android_network_performance.py`; add `--reference-renderer` for the old path.

## Sustained combat follow-up (October 26)

The follow-up is an engine-wide optimization, not per-map tuning. Native
2520x1680, 4x MSAA, remastered lighting/materials, original selected light count,
1024-pixel shadow tiles, bloom and 16x filtering remain unchanged. The retained
shader skips zero-contribution lights, reuses existing light directions for
shadow projection, uses a per-surface geometric normal-map basis for planar
geometry, and conservatively excludes contact-shadow footprints that cannot
reach a receiver. Animated meshes retain the derivative basis. Receiver bounds
follow the shader's world-position reconstruction, including cached mover
lightmaps. The manual shadow filter remains unchanged.

`--render-cull-lighting=0` (Android `--ei cullLighting 0`) restores the reference
shader for comparisons without changing saved options. The default is 1. The
416-byte lighting packet adds two vec4 lanes; CPU and std430 layouts match.
Planar normal directions no longer depend on parallax-displaced UV derivatives.
This is a coordinate-basis correction as well as a reduction in repeated work.

Fixed 120-Hz simulation, 600 timed frames, same native 4x target:

| Workload | Reference GPU scene | Optimized GPU scene | Reference throughput | Optimized throughput |
| --- | ---: | ---: | ---: | ---: |
| Hall | 10.543 ms | 8.889 ms | 83.79 FPS | 98.03 FPS |
| Water wheel | 13.023 ms | 10.674 ms | 61.21 FPS | 67.53 FPS |
| Goblin combat | 11.061 ms | 9.358 ms | 80.30 FPS | 93.71 FPS |

These are short renderer-throughput samples, not real-time displayed FPS.
Clocks were not forced. The first combat comparison received different input
and failed the trajectory assertion; it is excluded. The clean repeated pair
matches all 11 combat observations. Full-frame mean absolute 8-bit differences
are 0.0075, 0.0049 and 0.0055 respectively; p99 is zero in each. A few shadow-edge
pixels differ more (hall maximum 77; water 48; combat 16). Only 455 / 158 / 25 of
4,233,600 pixels differ by more than 10 in any channel. Pixel checks include
normal maps, mirrored coordinates, translated contact-shadow receivers and
both draw continuation paths, plus the existing lighting/shadow/water suite.

A hardware depth-comparison filtering experiment was supported on the OPPO but
changed GPU scene time only 8.804 -> 8.752 ms and throughput 99.61 -> 99.79 FPS.
That was not a meaningful gain; the experiment was removed. Vulkan comparison
filtering semantics were checked against the [Khronos sampling specification](https://docs.vulkan.org/spec/latest/chapters/textures.html).

The sustained fixture uses the existing 100 ms added-RTT private match with a
scripted desktop opponent and an explicit tablet defender test flag. It cycles
real shield input and permits ordinary damage/death/respawn; it does not hold a
dead character on screen for the measurement. `--automated-defender` is opt-in,
never enabled for human play. Client profiling logs repeat in 600-frame windows.
`android_present.py` records actual SurfaceFlinger timestamps and GPU/CPU clocks;
long samples also report 30-second windows. Use the first 300 seconds of each
sample, excluding shutdown/menu transitions. Because network timing and respawn
locations vary, sustained runs are workload checks, not frame-identical A/Bs.

### Five-minute displayed combat result

| Metric | Reference | Optimized |
| --- | ---: | ---: |
| Mean displayed FPS | 72.41 | 86.79 |
| Lowest 30-second mean FPS | 58.88 | 72.46 |
| 95th-percentile frame interval | 20.84 ms | 20.84 ms |
| Longest frame interval | 34.73 ms | 27.79 ms |
| Frame intervals above 50 ms | 0 | 0 |

The optimized run included shielded and unshielded hits and four respawns. This
is a useful sustained workload improvement, but different respawn locations and
clock behavior prevent assigning the entire average gain to the shader change.
The controlled runs independently measured a 15–18% GPU scene-time reduction.
The result does not demonstrate a locked 120 FPS or an all-map guarantee.

The final receiver-bound refinement was built after the sustained run; the final
APK passed the full Android production pixel-probe suite at native resolution
and 4x MSAA, including translated receivers and mirrored normal bases. This is a
pixel correctness check, not a Vulkan validation-layer claim. Android and desktop
builds succeeded. Save0, Save10 and MainActivity preferences retain their original
hashes, and the OPPO per-app refresh rule remains 4 (144 Hz).

Evidence, raw presentation timestamps, controlled profiles and final build hash:
[evidence/render-lighting-sustained/results.json](evidence/render-lighting-sustained/results.json).

## Shadow receiver follow-up (October 26, second pass)

The next sweep retained native 2520x1680, 4x MSAA and effects 8103. Reference
GPU scene time was 8.804–8.842 ms in the hall. Diagnostic omission of material
highlights reduced it to 8.453 ms; omission of dynamic shadow sampling reduced
it to 7.543 ms. These omissions only identify costs: neither is shipped. Shadow
sampling remained the largest isolated lighting component, about 1.3 ms.
The broader effect sweep measured about 0.75 ms for material maps and 0.67 ms
for bloom; these costs overlap and must not be added together.

Explicit loop-unroll hints measured 8.815 ms, effectively unchanged. A dedicated
BSP shader measured 8.998 ms against 8.804 ms for the shared shader. Both
experiments and their diagnostic switches were removed.

The candidate retained for final validation excludes a BSP receiver from a
dynamic shadow lookup only when its complete reconstructed bounds lie outside
one of the shadow's six clip planes. It preserves all lights, shadow casters,
shadow-map resolution, depth precision, filtering and effects. A half-unit
expansion covers the shader's at-most 0.37-unit normal offset; a relative
rounding margin and nonfinite-input fallback retain ambiguous receivers.
`--render-shadow-receivers=0` / Android `--ei shadowReceivers 0` selects the
reference path without changing saved settings.

Review identified an MSAA boundary prerequisite: world receiver positions must
be evaluated inside the covered primitive, like existing mesh positions.
The final candidate uses `interpolateAtCentroid` for that position reconstruction;
authored texture/lightmap sampling otherwise remains unchanged. This follows
the [Khronos interpolation rule](https://docs.vulkan.org/spec/latest/chapters/shaders.html).
It can change a few partially covered edge pixels relative to the old shader.

Before this edge correction, matching-frame tests measured 8.797→8.688 ms in
the hall, 10.415→10.462 ms at the water wheel, and 9.473→9.192 ms in combat.
Reverse-order combat repeated 9.358→9.033 ms. All three captured frames were
identical, and both combat comparisons matched all 11 encounter observations.
These preliminary results suggest a small 1–3.5% benefit in the first and third
workloads and no water benefit. They do not describe the final candidate until
the checks below are complete.

The standalone conservative-bounds oracle passed 100,000 deterministic cases
and 3,200,000 sampled points under AddressSanitizer and UndefinedBehaviorSanitizer.
The production Android pixel fixture now compares culling on/off and checks an
entirely out-of-volume receiver, in addition to moving/masked casters and draw
continuations. Final Android validation is pending tablet unlock; Android and
desktop builds succeeded. Evidence: [render-shadow-receivers](evidence/render-shadow-receivers/).
