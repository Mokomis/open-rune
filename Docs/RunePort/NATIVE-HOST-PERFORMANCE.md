# Native Android multiplayer performance follow-up

Later follow-up: [frame-wide lighting buffer optimization](UNIFORM-LIGHTING-PERFORMANCE.md).
Its installed artifact supersedes the earlier APK hash recorded below.

October 28, 2026. Follow-up to the 42.17 FPS DM-Hildir hosting observation in
[MULTIPLAYER-UI.md](MULTIPLAYER-UI.md). Tests use the stock Qualcomm Vulkan
driver, native 2520×1680, 4× MSAA, the existing 144 Hz app rule, and world-features
40871 (including medium ambient occlusion). No effects were disabled in retained
comparisons. Saved defaults remain hardware scaling at 50%; diagnostic overrides
are session-only.

## Cause and measurement limits

The tablet was thermally constrained during the low-FPS observation. A later
live report showed Android thermal status 3 (severe), shell_skin 45°C and a
646 MHz GPU ceiling. Current HAL GPU temperatures were approximately 67–69°C;
the approximately 99°C entries earlier in the same report were cached readings,
not current temperatures. The tablet was USB powered with a full battery.
This establishes throttling during that period, not continuous overheating or
that charging alone caused it. No thermal limits or device clocks were changed.

A host/client profile attributes approximately 11.05 ms to rendering, 0.13 ms to
network processing and 0.18 ms to presentation/interpolation. Local hosting at
646 MHz presented 51.69 FPS; a separate single-player sample at mostly 1150 MHz
presented 82.25 FPS. These are different clock conditions, so the difference is
not a measurement of networking overhead. The initial GPU profile during an
embedded join includes the title scene and must not be labeled gameplay GPU time.

In a fixed scene at high clocks, medium AO adds approximately 0.7 ms in the
presentation pass. Materials add approximately 0.65 ms to the scene, while
remastered lighting and its dependent features together account for approximately
2.45 ms. These feature costs overlap; they must not be added together.

## Retained changes

- Android uses a supported packed positive HDR color buffer (R11G11B10 float)
  instead of RGBA16 float for the scene, postprocessing, bloom and reflection
  buffers. It halves storage per color sample. Resolution, MSAA, effect settings
  and HDR range are retained, with lower color precision and no stored alpha.
  Scene blending does not consume destination alpha. Capability checks require
  sampling, linear filtering, blending, blits and every AA count selectable by
  the existing renderer; unsupported devices keep RGBA16F. Other platforms keep
  their old default pending testing. `--render-packed-hdr=0` / Android
  `--ei packedHdr 0` is the diagnostic reference path.
- Opaque Android menus limit their hidden 3D view to 10 FPS. Android UI drawing
  is independent. HUD and touch-layout editors expose the scene and keep normal
  rendering. Single-player waits wake promptly when the menu exposes the game.
- Covered multiplayer clients continue processing input state, receiving network
  updates and presenting replicas at a 60 Hz loop limit, rendering only at 10 FPS.
  Accumulated render time preserves scrolling textures and animated lights.
  The separate authoritative host keeps its existing tick rate. Resuming restores
  the requested game cadence; covered frames are excluded from gameplay profiles.
- Embedded map-load completion no longer overwrites a menu opened while loading.
  This fixes the invitation page displaying while native input/render state had
  already been reset to gameplay.

## Controlled measurements

Same DM-Hildir camera, seed and fixed simulation; 300 timed frames per run.
ABBA order helps expose clock drift. These are engine throughput and GPU timings,
not displayed-FPS measurements or sustained gameplay results.

| Run | Engine FPS | GPU scene ms | GPU presentation ms |
| --- | ---: | ---: | ---: |
| RGBA16F reference | 90.36 | 9.011 | 1.401 |
| Packed HDR | 97.30 | 8.407 | 1.327 |
| Packed HDR repeat | 96.16 | 8.415 | 1.332 |
| RGBA16F repeat | 90.45 | 9.025 | 1.409 |

The scene GPU saving is approximately 6.7%, with approximately 7% higher engine
throughput. Visually reviewed native captures show no apparent change in this
view: mean absolute RGB8 difference 0.168, p99 1, maximum 7; approximately 0.0034%
of channels differ by more than 3. This is not a claim of bit-identical rendering.

At the opaque title menu, twelve one-second GPU samples average 93% busy at
1219 MHz on the old path, versus 30% at 683 MHz with menu idling. These are short
clock/load samples, not power measurements. The HUD editor returned to normal
rendering (117 engine FPS in its accessibility snapshot); cancel restored idling.

The optimized local-host scene presented 108.81 FPS over 13.07 seconds with the
GPU at high clocks. This does not establish a 42-to-109 optimization gain: the
original 42 FPS observation was thermally constrained. The controlled ABBA result
above isolates the buffer change more credibly.

### Sustained combat

Two approximately 171-second presentation samples use a laptop host, a rendered
scripted opponent, an Android defender, and 100 ms added RTT with ±5 ms jitter
each way. Both exercise real blocks, weapon/kick/bash contacts, damage and
respawn. This verifies Android joining/combat; it is distinct from tablet hosting.

| Metric | RGBA16F reference | Packed HDR |
| --- | ---: | ---: |
| Displayed FPS | 80.48 | 79.69 |
| p95 frame interval | 13.89 ms | 20.83 ms |
| Longest interval | 34.72 ms | 41.67 ms |
| Intervals above 50 ms | 0 | 0 |
| Respawns / contact outcomes | 1 / 30 | 2 / 34 |

These sustained averages do **not** demonstrate a performance win. Thermal
conditions and respawn positions differ. The reference had mostly 1025–1150 MHz
GPU samples and none at 646 MHz; the later packed run had 19 of 75 samples capped
at 646 MHz. Its individual client profile windows fell into the upper 40s at that
cap, then recovered as clocks changed. Android reported severe thermal status
again. The measured 7% gain remains the controlled fixed-scene result; it does
not solve thermal throttling or establish sustained 120/144 FPS at native 4× AA.

## Validation and evidence

Android and desktop builds pass. The packed Android production pixel-probe suite
passes native 4× checks for camera coverage, per-pixel mesh/world lighting,
dynamic/masked/contact shadows, receiver bounds, material normal/roughness maps,
water reflection/cache/approximation/transparency, clarity, depth continuation,
alpha blending, hit testing, and original/enhanced rendering. This is production
pixel-probe evidence, not a Vulkan validation-layer claim.

The final local-host UI recheck passes start/join, idle invitation page, resume,
reopen menu and stop/leave. The tablet receives 1508 worlds in that check; the
laptop bot reports 1467 active updates over 50.195 seconds (about 29.2/s), with
a maximum gap of 601 ms. This establishes continued networking while the menu
idles, not a zero-jitter guarantee. The HUD editor's visible background remains
uncapped by this menu optimization.

Installed APK SHA-256 matches the built artifact:
`6468472ff9a66434a42b2ccb834491375f454b30f4111f218ef14797c7d97f26`.
Save0, Save10 and both backups match their original hashes. MainActivity
preferences are byte-identical. Test peers/hosts are stopped, diagnostic launch
arguments are cleared, and the ordinary title menu is left open using the saved
Hardware Scaling 50% / 4× AA settings. No commits or pushes were made.

An opaque-caster depth-only shadow pipeline experiment showed no repeatable gain
and was removed. Its diagnostic artifacts are retained outside Git.

Raw logs, captures, scripts, thermal reports and presentation timestamps:
`../../outputs/native-host-performance-2026-10-28` relative to the repository.
Invitation credentials and generated captures remain outside Git.
