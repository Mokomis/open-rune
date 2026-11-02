# Materials, water and multiple shadows

Open Rune, October 25, 2026. Optional Vulkan features, controlled by the shared
**Materials, water & shadows** page in both Video and Enhancements.

- **Material maps: Enhanced** generates normal and roughness textures from local
  game textures, with original procedural profiles for stone, wood, metal, skin
  and cloth. These are estimates, not artist-painted replacement assets. Broad
  painted lighting is filtered before deriving relief; normal mips are
  renormalized and roughness accounts for lost normal variation. Uses Remastered
  lighting, on supported static paletted textures. Animated and unsupported
  formats keep their original rendering.
- **Water reflections: Approximate** is the standard enhancement: animated
  ripples and a view-dependent environment sheen derived from the authored lit
  water color. It does not reflect individual objects or render another scene.
  The approximation follows local color/brightness rather than inserting a blue
  outdoor sky into caves. Existing enabled water settings now select this mode.
- **Water reflections: Full scene** retains the 512-square planar view with the
  main view's antialiasing, refreshed at up to 30 updates per game second. It
  reflects actual scenery and actors at substantially higher cost. Both modes
  animate waves every main frame and preserve submerged scenery / authored
  opaque water. Only the nearest eligible horizontal plane is enhanced;
  waterfalls, lava/slime, underwater views and moving brushes remain authored.
  Collision, swimming, scripts and progression are unchanged.
- **Shadow-casting lights: Three nearby** lets up to three nearby lights cast
  dynamic shadows in Remastered mode. Each gets a 1024-square tile in a depth
  atlas. Shadow visibility affects only the corresponding light. This retains
  focused cones aimed at the player, not whole-level omnidirectional shadows.

Existing installs retain unrelated settings. Each new feature has an independent
original/off choice. Enhancements' original-effects comparison bypasses these
without overwriting saved choices. Original game packages are never modified.

Feature bits: 1024 material maps, 2048 enhanced water, 4096 three shadow lights,
8192 full-scene reflection (requires 2048). Existing bit 2048 alone now selects
Approximate; Original clears both water bits.
The former 0..1023 settings retain their meaning. Android package/signing identity
is unchanged, preserving saves and the existing per-app refresh-rate rule.

## Validation

CPU material-profile/mipmap tests, settings migration/persistence checks, and the
desktop Rune native/model/map probe pass. On the OPPO, the shared page, water
toggle, original-effects comparison and process restart preserve all unrelated
preferences. Live SailingShip ocean reflections run without Vulkan validation
errors or warnings. The final native 4× GPU suite passes without validation
messages: normal-map lighting, three shadow-atlas slots, moving/masked casters,
reflection clipping/depth, transparent and opaque water, buffer continuation,
original rendering, hit readback and post-processing.

Native 2520×1680 with 4× MSAA and a 120 FPS target are the test baseline. Frame-rate optimization is deferred at
the user's request; a 120 Hz display or a 120 FPS target is not a sustained-FPS
claim.


| All new effects enabled | Actual presented FPS | p95 interval | Sample |
| --- | ---: | ---: | ---: |
| DM-Hildir living goblin encounter | 78.35 | 16.67 ms | 14.08 s |
| SailingShip ocean/cutscene with reflections | 37.88 | 33.34 ms | 14.20 s |

SurfaceFlinger timestamps and SDM both confirm the 120 Hz display mode; these
scenes do **not** sustain 120 FPS. Validation and screen recording were disabled
during measurement. No resolution or settings-combination sweep was performed.
All ten protected save files are byte-identical. Only the three requested new
feature choices and their shared bitmask changed in preferences. Evidence is in
[evidence/materials-water-shadows](evidence/materials-water-shadows).

## Observed SailingShip sky mismatch

The user reported fast overhead clouds beside a static horizon. It also appears
with `worldFeatures=0`. The original map assigns `PF_AutoUPan` (512) to
`Sky.cloudbase025` overhead, while the vertical `Sky.cloudbase02` surfaces have
zero surface and texture flags. The renderer advances autopan once per game
update; reflected views do not advance it again. No sky timing or asset edits
were made. This identifies the separate moving/static layers; it does not prove
that our apparent speed and seam match the original executable. A dedicated sky
blending/compatibility pass remains separate work. The read-only desktop
`--probe-sky-surfaces` and `--probe-water-surfaces` options report authored data.

## Water performance correction

The original water shader shared its fragment program with ordinary geometry,
and a visible water plane caused another scene traversal and 1024-square render
every frame. Water now has dedicated opaque/transparent shader pipelines, so
ordinary surfaces compile without the wave/reflection branch. The planar view is
512-square and cached for up to 1/30 game second. Plane changes, teleports, map
loads, time resets, feature changes and render-target recreation force a refresh.
The cached projection stays paired with its image. Reflected animation can update
less smoothly than the main view; this is an intentional quality/cost tradeoff.

The main native resolution and 4× MSAA are unchanged. Reflection reuse avoids
both scene traversal and draw submission. Shadow completion also avoids opening
an empty native-sized MSAA pass immediately before the reflection pass. Water
material classification is cached per map, skipped when disabled, and no longer
mistakes opaque floors merely bordering water zones for water. Masked decorative
surfaces retain authored rendering.

Sequential SurfaceFlinger comparisons on the OPPO, native 2520×1680 / 4× MSAA,
120 FPS target, seed 1, all other effects held constant:

| Scene | Previous water off / on | Revised water off / on |
| --- | ---: | ---: |
| DM-Hildir opening, no visible reflection | 86.65 / 83.13 FPS | 96.35 / 91.28 FPS |
| SailingShip ocean | 44.88 / 39.14 FPS | 46.79 / 48.50 FPS |

Each revised sample covers about 10–11 seconds. These are sequential moving
scene/device samples, not a sustained or perfectly identical workload. The
small reversed on/off difference is variation, not evidence that water makes
rendering faster. The ocean regression is substantially reduced in these
samples, but neither scene sustains 120 FPS with all enhancements. No claim is
made that every campaign scene is fixed. The 120 Hz hardware mode was confirmed
in every sample; no device policy was changed. Validation was disabled for FPS.

The native 4× Vulkan suite passes with synchronization validation and no warnings
or errors, including cached reflection pixels across frame-buffer swaps, wave
time advancement, absence of an additional reflected draw, expiry, plane change,
teleport, time reset and map invalidation. A live ocean screenshot also confirms
that the boat/sky reflection remains visible. The existing sky-layer mismatch is
unchanged. Evidence: [evidence/water-performance](evidence/water-performance).

The release APK was installed with the existing signing key and without the
validation layer. All ten protected saves and every preference match the
pre-test snapshot. The latest SinkingShip2 checkpoint is restored and paused.
Desktop and Android builds and the desktop SailingShip native/map suite pass.

## Water-wheel comparison

A fixed above-water camera at the first wheel in Dwarf1wwheel, native 2520×1680,
4× MSAA, 120 FPS target, measured 45.35 displayed FPS with reflections off and
29.63 with them on (17.33 / 17.25 second samples). All other effects matched.
The view was fixed while simulation continued; the player remained at the
underwater spawn. This is a renderer comparison, not a gameplay traversal test.

The reflected scene contained approximately 638 BSP surfaces and 88–92 actors.
In the on run, 600 reflected renders had only 30 cached reuses: near/below 30
FPS, the 1/30-second expiry causes almost every frame to redraw the reflection.
The visual difference in this view is predominantly darker, reflective water,
with a small benefit relative to the cost. Both screenshots use the same camera,
but simulation/water animation time is not identical.

Do not interpret the FPS difference as an isolated 35% shader penalty. GPU clocks
were about 1.025–1.075 GHz early in the off sample, then capped at 646 MHz; most
of the on sample ran at 646 MHz before recovering at the end. CPU limits also
varied. No clock/power policy was modified. Hardware refresh remained 120 Hz.
The earlier 400-frame engine profiles cover different windows from the displayed
FPS captures and must not be substituted for those results. No additional
settings sweeps or optimization changes were made after this comparison.

The next optimization should reduce reflection-scene traversal/draw/lighting
work, or use an approximate reflection. Lower image resolution alone does not
remove those costs. Original gameplay saves and preferences were verified
unchanged and the SinkingShip2 checkpoint restored paused.
Evidence: [evidence/water-wheel](evidence/water-wheel).

## Lightweight water implementation

Approximate prepares only wave time and plane parameters; it never traverses or
submits a mirrored scene, even below 30 FPS. The water shader uses its authored
lit color, a reflected view-direction gradient, Fresnel and a broad ripple
highlight to approximate an environment. It is deliberately not a claim of
accurate local reflections. Full scene remains separately selectable in the
shared Video / Enhancements page, and its existing cache/path are unchanged.

GPU pixel checks cover animated approximate water, submerged visibility, opaque
water and zero reflection-scene renders, plus the retained full reflection and
cache checks. Native 4× Vulkan synchronization validation reports no errors or
warnings. Host menu checks cover three-state encoding, restart persistence and
unrelated feature isolation. Performance and deployment results follow below.

The shared Android menu passes Original / Approximate / Full scene changes,
including native acceptance of the Full scene bit, original-effects comparison,
Video/Enhancements synchronization and restart persistence. The release APK
excludes validation layers; its installed hash matches the local signed build.
All ten protected saves and every persisted preference match the pre-test
snapshot. The enabled water setting now means Approximate. SinkingShip2 was
restored and paused after testing.

Fixed water-wheel camera at native 2520×1680 / 4× MSAA / 120 target:

| Run | Presented FPS | Sampled GPU clock |
| --- | ---: | --- |
| Original, first | 59.93 | 1.075–1.225 GHz |
| Approximate | 31.62 | 646 MHz throughout |
| Original, repeat | 57.38 | 1.025–1.15 GHz |

These samples are **not a fair isolated FPS comparison**: GPU clock limits
changed between modes, and the repeat recovered before measurement. No device
policy was changed and no further timing sweeps were performed. Do not advertise
a measured speedup or attribute the entire difference to the shader. The
structural saving (zero mirrored scene traversal/draws) and correctness are
verified; a controlled FPS gain is not established. Hardware refresh remained
120 Hz. These are stationary renderer samples, not campaign performance proof.
Evidence: [evidence/water-approximate](evidence/water-approximate).
