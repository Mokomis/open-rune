# Frame-wide lighting buffer optimization

October 28, 2026. Follow-up to [native hosting performance](NATIVE-HOST-PERFORMANCE.md).
This is an engine-wide Android renderer change, not map-specific tuning.
Stock Qualcomm Vulkan on OPPO OPD2515, native 2520×1680, 4× MSAA,
world features 40871 (including medium AO), packed HDR enabled and the existing
144 Hz app rule. No visual features, resolution or AA were reduced.

## Retained change

The 464-byte frame-wide lighting packet now uses a Vulkan uniform buffer on
Android, instead of a storage buffer. It contains three shadow transforms and
light positions, the lighting camera, the water transform/parameters and four
contact-shadow centers/planes. The per-draw mesh-light array remains a storage
buffer. Values, precision, shadow filtering, atlas size, caster selection, light
counts and buffer update order are unchanged.

The shader uses std140 for this path and std430 for the reference. Only mat4 and
vec4 members are present; host size and member-offset assertions enforce the
shared layout. Descriptor binding 1 and buffer usage switch together. Both
frame buffers and reflection descriptor sets use the selected type. The buffer
range is 464 bytes at offset zero. Existing coherent mapping and GPU submission
synchronization are retained.

Android defaults to the new path. Other platforms retain their existing default
pending device validation. A startup diagnostic override remains:
`--render-uniform-shadow-data=0` / Android `--ei uniformShadowData 0` selects the
reference; `1` selects the uniform path. It does not change saved settings.

## Why this change

In a fixed DM-Hildir view, removing receiver-side moving-shadow projection and
sampling reduced scene GPU time from 8.403 to 7.149 ms. Omitting shadow-map
creation as well reduced it to 7.125 ms. Removing material highlights alone
reduced it to 8.030 ms. These diagnostic effects were restored. The costs overlap
and are not additive; the result describes this view, not every map.

The expensive work was primarily in the receiver shader. Moving shared values
to a uniform buffer improved the measured shader path without changing its math.
This is evidence of a faster buffer access/compiled shader path on this device,
not a hardware-counter proof of one particular cache or instruction bottleneck.

Rejected and removed: hardware depth-comparison filtering (less than 1% in the
initial sample), per-draw shadowless shader specialization (about 4% slower),
explicit loop unrolling/no-unrolling and an early zero-shadow exit (no useful
gain). Diagnostic feature-removal switches were also removed from the final build.

## Controlled rendering

Initial ABBA test, 300 timed frames per run, 1225 MHz during rendering:

| Run | GPU scene ms | Engine throughput FPS |
| --- | ---: | ---: |
| Storage reference | 8.398 | 96.38 |
| Uniform | 7.784 | 102.67 |
| Uniform repeat | 7.787 | 102.74 |
| Storage repeat | 8.395 | 97.15 |

Final clean APK, 600 timed frames per run, native 4× AA, same fixed simulation and
settings for each pair; rendering GPU samples at 1225 MHz:

| Scene | Reference GPU ms | Uniform GPU ms | Scene saving | Engine FPS before → after |
| --- | ---: | ---: | ---: | ---: |
| DM-Hildir fixed view | 8.395 | 7.783 | 7.3% | 96.98 → 103.65 |
| Dwarf1wwheel | 10.223 | 9.875 | 3.4% | 72.50 → 74.85 |
| Goblin combat | 8.767 | 8.411 | 4.1% | 93.56 → 96.40 |

These are fixed-step throughput/GPU measurements, not displayed gameplay FPS.
Hall and combat captures are pixel-identical. Combat also preserves all 11
recorded encounter observations. The water pair has mean RGB8 difference 0.093,
p99 zero, and 11,157 of 4,233,600 pixels differing by more than 3; the visible
moving jellyfish differs between captures. Water surfaces/reflection and scene
lighting were visually reviewed, but the water pair is not pixel-identical.

## Validation

Android ARM64 and desktop builds pass. Native 4× production pixel probes pass
for original/enhanced mesh/world lighting, eight lights, camera depth coverage,
moving and masked shadow casters, contact shadows, receiver culling, material
maps, water reflection/cache/approximation/transparency, clarity and hit testing.
This is production pixel-probe evidence, not Vulkan validation-layer coverage.
No repeat of the full laptop multiplayer map/mode matrix was needed.

## Sustained multiplayer result and limits

Two approximately 169-second displayed-frame samples used a laptop host,
rendered scripted opponent and Android defender with 100 ms added RTT and ±5 ms
jitter each way. Both used native resolution, 4× AA and world features 40871.

| Metric | Storage reference | Uniform |
| --- | ---: | ---: |
| Displayed FPS | 87.77 | 83.95 |
| p95 frame interval | 13.89 ms | 20.84 ms |
| Longest interval | 34.72 ms | 34.73 ms |
| Intervals above 50 ms | 0 | 0 |
| Respawns / contact outcomes | 1 / 30 | 2 / 33 |

The later uniform run spent 9 of 75 GPU samples at a 646 MHz ceiling; the
reference spent none there and ran mostly at 1075–1150 MHz. Android reported
severe thermal status. One uniform 600-frame client profile dropped to 51.92 FPS
before recovering into the upper 80s. The two runs also have different respawn
positions. These averages therefore do **not** demonstrate a sustained FPS gain.
The repeatable optimization evidence is the controlled fixed-scene measurement.
Native 120/144 FPS and freedom from thermal dips remain unachieved. No thermal
limits, clocks, device tuning or effect settings were changed.

Both clients continued exchanging worlds, performing weapon/kick/bash actions,
blocking, taking damage and respawning without logged script or device-loss
errors. The panel retained 144 Hz. This run validates Android joining/playing;
it is not a new tablet-host capacity test.

## Installed artifact and handoff

Final APK SHA-256:
`bba35b7d83ab5b9183f2b82bdee04a47631669040bf701fb81d3fb2f0d89a083`.
The installed APK hash matches the built artifact. Save0, Save10 and both backups
match their original hashes; MainActivity preferences are byte-identical. Test
hosts/peers are stopped and diagnostic launch arguments are cleared. The normal
menu uses the saved Hardware Scaling 50% / 4× AA preferences, with the new
uniform-lighting path enabled. No commits or pushes were made.

## Reproduction and evidence

`Tools/RuneValidation/android_render_cost.py` accepts
`--compare-uniform-shadow-data --world-features 40871 --oppo-clocks` with its
usual adb, device and new output-directory arguments. It explicitly requests
100% surface resolution, avoiding the saved 50% hardware-scaling default.
`--oppo-clocks` uses existing root read access only; it never changes clock policy.
The combat launcher accepts `--uniform-shadow-data 0` or `1` for sustained tests.

Raw comparisons, clocks, images, exact scripts and probe logs are outside Git:
`../../outputs/lighting-shadow-pass-2026-10-28` relative to the repository.
