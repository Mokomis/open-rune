# Cross-platform follow-up — dev.11, November 8, 2026

The surface wave phases now follow two broad non-aligned currents with varying local strength, breaking up the former evenly spaced parallel ridges from both sides. This reuses the existing two modulation oscillators without another texture sample or render pass.

Enhanced water now uses the same replacement surface shader on Windows, macOS and Android. Original and Legacy reflections choices are preserved. The previous Windows-only limitation below is historical.

Mac (2816×1762) and OPD2515 (2520×1680), both at native scale and actual 4× MSAA, passed rendered-pixel checks for ripple animation, depth response, opaque/transparent water and zero extra reflection scene passes. The focused modern-water probe runs independently of the Mac’s pre-existing unsupported integer hit-buffer diagnostic. Coastal old/new ABBA profiles measured about 58→81 engine FPS on Mac and 89→111 on Android; those are bounded fixed-camera results, not sustained gameplay guarantees. Initial waterwheel comparisons were approximately neutral. Evidence: `WaterCrossPlatform-20261108`, outside Git.

Underwater refraction is strengthened to six pixels at 1080-high output, scaled with viewport height so it remains visible with render scaling. It uses the existing presentation pass; menus disable it and edge HUD pixels are unchanged. GPU readback confirms animation between two times on Mac and Android. It is screen-space refraction with the game’s existing underwater environment, not new volumetric fog.

Sky reflections now use a cached upward BSP trace from the representative water facet, cleared on map change. They no longer depend on whether the camera currently shows a sky portal. This corrected the waterwheel view: the map has sky openings, and that basin has a direct overhead sky hit. Snow there is legitimate; its 317-column geometry check found 73 exposed columns and zero covered-column leaks. The earlier viewport-based “indoors” label was not proof of a roof. A bounded `--snow-probe=MAP --snow-point=x,y,z` diagnostic records the direct sky hit and nearby column checks.

The replacement and stronger refraction were visually reviewed in coastal and waterwheel views. Final builds include the sky-exposure correction; it was additionally rendered on Mac. No all-map, long soak, new multiplayer or extra physics work is claimed. Normal saves/preferences are preserved. No Unity runtime or extra water simulation thread is involved.

---

# Windows enhanced water rebuild — November 8, 2026

## Scope and controls

The Windows Vulkan client has a replacement for the former approximate water
shader. It is a custom implementation; no Crest code/assets or Unity dependency
were incorporated. Monsters & Memories screenshots were a visual reference.

Windows Video/Enhancements retain the existing three saved values:

- `0`, Original: authored Rune water.
- `1`, Enhanced: new surface shader and gentle underwater presentation distortion.
- `2`, Legacy reflections: previous planar-reflection renderer, with its extra
  scene traversal and render cost.

`--water-model=0` restores the previous approximate shader for matched comparisons;
`--water-model=1` selects the replacement. Other platforms default to model 0 and
have not been qualified or deployed by this work. `--water-underwater=0` disables
only the new underwater shimmer. `--trace-rune-water` logs mode, sky selection,
cache warmup, and the actual backing target dimensions/sample count.

## Implementation

- Four directional ripple bands with short-band cross modulation, Fresnel sheen,
  original sky/cloud texture reflection, local-light glints and depth absorption.
  Ripple derivatives attenuate detail below pixel size.
- A dedicated water fragment branch bypasses the generic multi-light/material
  world shader. No FFT, extra simulation thread, reflected-scene pass or compute
  water simulation is needed for Enhanced.
- Static BSP water depth is sampled downward at vertices and cached by point and
  face until map change. This is coarse bathymetry, not a screen-space depth read.
- Preserve authored opaque water and its BSP occlusion. Existing transparent
  water uses premultiplied depth-dependent alpha; do not force opaque ocean
  polygons transparent when geometry behind them may have been culled.
- Support visible horizontal water at multiple elevations, including viewing from
  below. Waterfalls and geometry displacement remain unchanged.
- Outdoors use an authored sky/cloud texture selected from map materials, with a
  cached palette average when available. Indoors retain authored water energy
  and sampled local environment colour. One eligible nearby water-associated
  light supplies glints. Reflection is an approximation: no characters, rooms
  or other local geometry appear in the reflected image.
- Underwater shimmer runs in the existing final presentation shader, based on the
  actual rendered camera water zone. Its 128-byte push layout is asserted. The
  outer image/HUD edges are untouched and menus disable shimmer. Custom HUD
  elements moved into the centre can still be distorted; this is a final-image
  effect, not a separate pre-HUD scene refraction pass.
- Fog-map view selection now uses the rendered frame zone rather than the
  gameplay camera actor zone. This handles detached/reflection cameras correctly;
  it was not shown to solve the wheel scene's general CPU bottleneck.

## Qualification and limits

Machine: Windows, RTX 4080 SUPER, actual 3840×2160 scene and capture, four actual
MSAA samples, scale 1.0. Isolated game/config/save fixtures, seed 1, 120 Hz fixed
simulation, uncapped rendering, no public host. These are bounded profiles,
not a sustained interactive campaign or Android performance claim.

The final v5 matched coastal comparison measured 1.221 ms GPU scene with old
approximate water versus 0.923/0.906 ms with the replacement. Total frame time
was 9.395 versus 9.111/9.103 ms. That is about 25% less GPU scene time and 3%
less total frame time in that fixed view, not a 25% overall FPS gain.

The wheel fixture is CPU-bound: about 77–79 ms/frame while GPU scene time is
about 1.5 ms. The older water-off control was 77.6 ms and legacy planar water
about 120.9 ms. Enhanced avoids the planar traversal penalty, but this work does
not fix the underlying CPU render preparation cost or establish 120 FPS there.

The underwater on/off comparison measured final presentation around 0.13 ms in
both cases. GPU readback tests verify ripple animation, depth response,
transparent and opaque water, zero extra reflection scene passes, and underwater
centre distortion with exactly unchanged edge pixels. The existing Vulkan
resolve/depth/alpha/hit-output regression probe also passes. Validation layers
were not enabled; this is not a synchronization-validation certification.

Visual review rejected early flat/cloudy versions and refined ripple scale and
sky detail. Indoor authored-light preservation was added after an excessively
dark basin was caught in screenshots. Appearance still needs user review; no
claim of Crest feature parity or a finished ocean simulation is intended.

## Evidence and deployment

Windows: `C:/OpenRune/outputs/water-rebuild-20261108`.
Mac recovery/evidence: `/Users/rwillmore/OpenRuneArtifacts/WaterRebuild-20261108`.
Versioned v4/v5 folders preserve final comparisons and GPU logs; earlier failed
compile/shader attempts and prototype captures are retained separately.

Windows main executable was rebuilt. No Unity port, Mac/Android update, tablet
operation, live-match restart, public server, commit or push was performed.
Original packages, normal saves/settings, and unrelated source changes remain.

Reference: https://monstersandmemories.com/screenshots and official Update 50
(https://monstersandmemories.com/updates/update-50-sept-oct-nov-2025), which reports
its Crest 5 upgrade. Crest documentation: https://docs.crest.waveharmonic.com/.
