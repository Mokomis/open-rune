# Material lighting and local environment probes

November 3, 2026. Shared Vulkan implementation; Mac native-resolution / 4× MSAA
validation. This is a material-response upgrade within the retained Rune lighting
model, not a complete physically based conversion of the original artwork.

## Controls

Video → Lighting & shadows contains two independent choices:

- **Material lighting: Classic / Physical**. Physical uses GGX highlights,
  Schlick Fresnel and Smith visibility on metal and smoother materials. Very
  rough nonmetals use a bounded broad-lobe approximation. Roughness comes from
  the existing material maps when enabled, otherwise the existing presets.
- **Local environment lighting: Off / On**. Adds softly directional reflected
  room color and broad material reflections from static map geometry.

Both require **Remastered** lighting. Existing saved choices/defaults remain
unchanged. Original-visual comparison disables both temporarily. Windows and
Android share the setting meanings; this change does not deploy either client.
The Mac's existing console supports `materiallighting 0/1` and
`environmentlighting 0/1`. These console changes are session-only.

World-feature bits 16 and 17 select these features. The original low 16 bits
retain their previous meaning. Example isolated Mac launches use
`--world-features=1792` for Remastered + material maps, or `198400` for the same
configuration with both new features enabled.

## Probe construction and performance design

- Baked on first enabling the effect in a level, before the scene render pass;
  retained in RAM until map change. Toggling off/on reuses the field.
- Navigation points, pawns and offset BSP polygon centers seed at most four
  probes per convex BSP leaf. Spatial cells reject redundant placements.
- Each probe traces 24 static-world rays. Hit surfaces contribute their original
  palette-average artwork color multiplied by bounded authored static lighting.
  Surface radiance/texture averages are shared during construction.
- Only supported steady, ordinary/wavering static lights are considered;
  unsupported special light effects and special-lit lights are excluded.
- Six directional radiance lobes represent broad reflections, not detailed
  images. Missing probe coverage keeps the original lighting.
- Interpolation is restricted to the same BSP leaf, preventing blends through
  nearby walls. This is intentionally conservative: transitions between leaves
  can differ, and sparse probes cannot reproduce detailed reflection geometry.
- Broad direction/roughness weighting runs per vertex. Actual direct light,
  normal maps, GGX and the reflection Fresnel response remain per fragment.
  Actor probes are sampled once with their shared lighting, preserving triangle
  batching. There is no extra scene draw, screen-resolution effect or frame-time
  ray tracing.

The field excludes moving geometry, combatants and live shadow state. It does
not update to reproduce a door opening or a scripted light changing after the
bake. Original direct lighting/shadows remain live. Backdrop skies are not
treated as reflecting walls; no generic blue environment is introduced in caves.
The optional texture remaster is neither required nor regenerated.

Diffuse lighting retains Rune's existing art calibration and display transfer;
this is not a full linear-light PBR pipeline or true HDR display output. Material
classification/response maps remain estimates, not newly authored metal masks.

## Validation and evidence

Evidence and isolated test game/configuration/save directories are under
`outputs/material-probes-20261103` above the checkout. Original game data is
linked read-only for tests; configuration files are independent copies and saves
use the existing absolute `--save-directory` override.

- `environment_probes.cpp`: sanitizer-enabled field interpolation, missing
  coverage, same-leaf wall isolation, continuity and map-reset checks.
- `MenuSettingsTest.java`: defaults, independent feature bits, persistence,
  comparison and unrelated-setting isolation.
- `--probe-material-lighting`: real native/4× GPU draws for both mesh and BSP
  receivers, material toggle, roughness, directional reflection with no direct
  lights, and unlit bypass. Does not rely on the older integer hit-target tests.
- `--probe-material-cycle`: isolated bounded off/on/on/off blocks, 240 frames
  each, first 60 excluded from each block. Repeated within one process to expose
  timing drift; all resolutions, MSAA and other settings stay fixed.
- `--capture-material-comparison`: redraws the exact final world/pose without a
  simulation tick, producing `-off.ppm` and `-on.ppm` captures.

The older comprehensive Vulkan probe encounters a pre-existing MoltenVK
integer-hit-attachment resolve limitation on this Mac. Its failed run is retained;
it is not counted as a passing full graphics validation run.

### Final Mac observations

Apple A18 Pro / MoltenVK, actual **2816×1762**, **4× MSAA**, scale 1.0,
uncapped engine timing, one shadow light, existing material maps enabled in both
states. Each scene alternated off/on/on/off twice in one process, with 180 timed
frames per block after 60 excluded warmup/switch frames. Values below are the
median block mean, not independent display-presentation measurements.

| Scene | Classic material + probes off | Physical + probes on | Added time |
| --- | ---: | ---: | ---: |
| DM-Hildir stationary scene | 13.91 ms / 71.9 FPS | 14.53 ms / 68.8 FPS | 0.62 ms / 4.3% FPS |
| RagnarVillage opening | 13.57 ms / 73.7 FPS | 13.79 ms / 72.5 FPS | 0.22 ms / 1.6% FPS |

Timing drift is visible in the raw blocks; these bounded comparisons are not a
sustained campaign-wide or Android FPS guarantee. An earlier fragment-stage
probe implementation had materially higher cost and was replaced. Broad probe
weighting now runs per vertex, and matte materials avoid unnecessary microfacet
work. The one-time bake measured about 0.6–0.7 s in these two maps and 2.1–2.2 s
in HelLift; it is separate from steady rendering and reused after toggling.

The final focused GPU test passes all twelve mesh/BSP material cases at actual
native/4×. The field sanitizer and Java preference tests pass. A 1,200-frame
DarkViking combat fixture completes with accumulated player/enemy damage 270/30
(health/body durability replenished by the existing fixture). This confirms
active combat rendering, not normal survival/balance or sustained multiplayer.

Moving-lift execution also completes. Its close-camera view becomes occluded,
and its timing drifts substantially; the lift runs are retained as limited
functional evidence, not a representative lighting-quality/FPS comparison.
Two accidentally overlapping early benchmark launches are explicitly discarded
in `discarded-overlap.txt`; final comparisons ran sequentially.

Matching same-pose off/on captures were visually reviewed for Hildir, the village
and combat. Hildir's mean captured channel value changes 27.62→26.63/255; the
upgrade does not simply brighten the whole frame. The combat comparison contains
the same original hit-flash tint in both states. No obvious new wall leakage or
image corruption was observed in these views; full campaign review and static
probe transitions remain unverified.

Eight original Mac INI files match their starting hashes. Test saves and config
copies remain isolated. No tablet access, Windows deployment, commit or push.
