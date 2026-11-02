# Open Rune — Environmental audio and surface detail — October 24, 2026

This is an original implementation of the requested effects, using the OpenAL
Soft library already bundled with this engine. No Rune mod code, DLLs, presets,
normal maps or height-map packs were downloaded or incorporated. Existing
SurrealEngine/SDL/OpenAL licensing still applies. The user's original game
packages supply the textures and sounds; they are not included in releases.

## Controls

Open **Clarity → World detail / sound**. Independent checkboxes control:

- Environmental sound: room reverb and wall/door muffling.
- Surface detail: shallow normal-map shading on eligible world surfaces.
- Surface depth: optional parallax, independent of normal-map shading.
- Headphone spatial audio: explicit OpenAL HRTF; leave off for tablet speakers.

**Original** disables all four additions. Bloom/color, clarity, resolution and
MSAA remain separate. Changes apply live and persist. Diagnostic intent extra
`worldFeatures` overrides preferences without writing them: bits 1/2/4/8 above,
so 0=Original, 3=audio+detail, 7=audio+detail+depth, 15=all. The first-install
default is audio only; the tested tablet is left with audio+detail (3).

## Audio implementation and limits

`EnvironmentalEffects.h` owns one OpenAL reverb effect/slot and per-channel
low-pass filters. It routes effects only; music does not receive a reverb send.
Original detaches filters and sends. Source reuse starts with clean routing;
new sounds are checked for obstruction in their first update. Existing sounds
are checked at 10 Hz with world/mover traces and a smoothed obstruction amount.

Authored reverb zones supply wet level, delay-derived decay and high-frequency
damping. Untagged rooms use a conservative five-ray enclosure estimate, requiring
a blocked ceiling. Water has its own restrained settings. This is an acoustic
enhancement and an approximate use of Rune's zone metadata, not exact EAX
emulation or ALAudio compatibility. Door/wall thickness and material-dependent
transmission are not modeled. HRTF activation is queried after device reset.

## Material implementation and limits

The Vulkan texture manager generates packed normal/height maps in memory from
the original P8 textures in **Wood, Rock and Brckston**. No derived asset files
are written into the game folder. Wrapped smoothing, a complete mip chain and
distance fading limit noise. Animated/procedural, transparent, masked, unlit,
sky and wavy/panning surfaces are excluded, as are surfaces with a macro texture.
This is an initial material subset, not a replacement for every campaign texture.

Static BSP surfaces use a bounded weighted direction from their original static
light list. Tangent-space lighting is computed once per surface on the CPU;
the normal-only shader avoids per-pixel derivative-basis reconstruction. Normal
shading makes a restrained adjustment to the existing baked
lighting; it does not replace Rune's lightmaps, add dynamic shadows or change
collision. Movers currently have no supplied relief-light direction. The depth
option uses at most eight height-intersection steps, explicit texture gradients,
a grazing-angle limit and less than one world unit of apparent depth. Both
effects fade between 220 and 650 view-distance units.

Height is estimated from luminance: painted light or dark marks can be mistaken
for relief. Artist-authored maps would be more reliable. The current result is
deliberately subtle, and parallax does not change silhouettes or geometry.

## Validation

Evidence is under `evidence/world-features/`. Native resolution means 2520x1680
with 4x MSAA. FPS uses actual SurfaceFlinger presentation timestamps, with the
hardware display mode also checked; engine FPS is not substituted for it.

- ARM OpenAL loopback renders a synthetic impulse through the runtime effects:
  dry late energy 0, wet late energy 0.0000432926; filtering reduces direct energy
  from 0.177407 to 0.00617361. Original detaches the previously assigned filter.
  These are signal checks, not a subjective listening-quality assessment.
- In-game EFX and HRTF activation are reported by the backend. Obstruction logs
  show blocked channels. Actual Android controls exercise 0→1→3→7→15→11→3,
  including HRTF active and disabled readbacks.
- Material CPU tests cover flat maps, slope direction, and 1x1/2x2 mip tails
  under address/undefined-behavior sanitizers.
- ARM gameplay/headless regression passes. Native4x Vulkan depth, hit, alpha,
  camera-coverage and clarity pixel checks pass without reported core or
  synchronization warnings/errors; actual material generation also runs with
  validation enabled. Game captures are inspected separately.
- Initial DM-Hildir displayed samples: Original 120.01, audio+detail 119.45,
  audio+detail+depth 119.73, Original again 119.73 FPS. Those small differences
  do not establish a feature cost or speedup.
- Hel3a measured 96.03 FPS off and 96.06 on before brick/stone was added. That
  view had no eligible wood/rock textures, so it validates the audio path and
  fallback, not material performance. The first driver incorrectly required
  every map to generate a material; that test assumption was corrected.
- DeepUnder3 exercises six rock materials in bounded captured frames. Its first
  unoptimized displayed sequence was 118.22 Original, 107.89 audio+detail, 89.76
  audio+detail+depth, then 111.65 Original. These are evolving gameplay samples;
  the baseline also changes. The depth cost is reason to leave it optional/off.
  The normal-only shader and redundant audio routing were subsequently optimized.
- Final shader core/synchronization and existing pixel regressions pass in
  DeepUnder3 with all features enabled. Wood and rock captures were inspected;
  Brckston is eligible in code but its visible use is not established by these
  particular camera positions.
- Final optimized DeepUnder3 samples: Original 117.84, audio only 114.42,
  audio+detail 110.11, Original again 111.22 FPS. The changing baseline and
  evolving encounter prevent a precise cost estimate or a claim of a measured
  optimization speedup. Depth remains off; the selected tablet preference is 3.
- Isolated save-flow and fresh-process save-resume both pass with the additions
  enabled. Normal save hashes and all pre-existing graphics preference values
  are checked separately from the new preference. Final ARM and actual-control
  results are retained with the release evidence.

These are bounded tests, not sustained120FPS or campaign-wide visual/audio parity.

## Reproduce

Run device cases sequentially on an unlocked tablet. The drivers use isolated
saves. `android_world_controls.py` intentionally changes the new preference
and leaves it at 3; it does not change the existing graphics preferences.

```sh
python3 Tools/RuneValidation/android_renderer.py --device DEVICE --map DM-Hildir --world-features 15 --output OUTPUT/gpu
python3 Tools/RuneValidation/android_world_features.py --device DEVICE --bounded --validation --require-materials --modes 0 2 4 15 --output OUTPUT/images
python3 Tools/RuneValidation/android_world_features.py --device DEVICE --modes 0 3 7 0 --output OUTPUT/fps
python3 Tools/RuneValidation/android_world_controls.py --device DEVICE --output OUTPUT/controls
```

Use the validation-layer APK only for the GPU/image validation calls. Performance
measurements and the final installed APK omit that layer. The audio PCM probe
source is `Tools/RuneValidation/environment_audio.cpp`; compile against the
bundled OpenAL and run on ARM with its existing library. The material test is
`Tools/RuneValidation/material_relief.cpp` and needs only a C++17 compiler.

Private prerelease: [v0.8.0-world-features](https://github.com/Mokomis/rune-classic-android/releases/tag/v0.8.0-world-features).
The matching installed/release APK digest and preservation proof are in
`evidence/world-features/installed-build.json`. No validation layer is packaged
in that APK. The same package identity, signing key and Rune-only refresh rule
are retained.
