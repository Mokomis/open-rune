# Remastered lighting

Video and Enhancements share **Lighting mode: Original / Per-pixel / Remastered**.
Original retains the original vertex/lightmap path. Per-pixel retains the previous
continuous diffuse lighting. Remastered adds stronger direct lighting, material
highlights, relief normals responding to actual lights, and moving shadows.
All modes use original game assets. This change does not alter game packages,
scripts, combat, save formats, or display refresh policy.

## Rendering

Remastered uses a 1024×1024 D32 shadow map for one dominant nearby light. It selects
from the player's existing visible light list, applies selection hysteresis, and
renders a 120-degree cone aimed at the player. Coverage fades at its edges. This
is a focused local shadow, not omnidirectional shadows from every light or global
illumination. Outside its coverage, other lighting and baked shadows remain.

Casters are collected from the light's own frustum, including objects outside the
main camera view. BSP, moving brushes, posed characters, weapons, and opaque
scenery use the production geometry paths. Masked textures retain their holes;
transparent effects and sky/portal surfaces do not become solid blockers.
Camera fading does not erase the player's shadow. Shadow rendering never advances
animation or simulation. Dynamic skeletons remain updated by the game tick.

The depth pass precedes world rendering. A depth gather and bilinear comparison
filter soften the edge;
normal offset and depth bias reduce self-shadow artifacts. Visibility applies
only to the selected light's contribution. Ambient, other lights, and authored
baked shadow masks remain independent. The shadow image uses explicit read/write
barriers. The native MSAA color pass starts after shadows when PreRender has not
drawn anything, avoiding an empty color/depth store and reload on mobile tile
GPUs. If scripts draw first, scene color/depth survive the interrupted pass. Buffer
continuations retain shadow depth and the active light transform.

Material presets use original texture/package names, with a rough default for
unknown materials. Recognized metal and weapon names get stronger, narrower
highlights; wood remains rough. These are heuristics, not authored PBR textures.
Generated stone/wood normals feed the actual per-light shading when Surface
detail is enabled. They are not high-resolution replacement textures.

Diffuse illumination retains the original 0–1 lighting limit. A local boost of
up to 20% fades smoothly between 15% and 55% of each light's radius, replacing
the earlier full-radius 65% scenery / 35% character boost. Ambient remains
reduced modestly to improve shape and light-pool contrast. Material highlights
are added separately; the existing highlight shoulder retains bounded bloom headroom.
This is SDR output through the existing floating-point scene targets, not HDR
display output, ray tracing, or a physically based material conversion.

The October 25 exposure adjustment follows same-viewpoint observations with
shadow/midtone lift Off: Remastered brightened the room broadly compared with
Original. Moving-shadow visibility and material-highlight calculations are
unchanged. ARM64 compilation and signed APK packaging passed. The installed
build rendered the user's latest DM-Hildir checkpoint with Remastered enabled
and shadow lift Off; the screenshot shows dark corners with retained lit
materials. The latest checkpoint has a different viewpoint from the earlier
comparison, so this is a visual smoke check, not a matched exposure measurement.
No new FPS benchmark or full GPU regression suite was run for this adjustment.
Earlier GPU/performance results below describe the previous lighting balance.

### Character ground contact

Remastered also adds a soft ground-contact footprint for up to four nearest
visible living pawns within 800 world units. At most four downward traces find
actual world/mover ground. The footprint uses the pawn's collision radius,
darkens the center by at most 48%, and fades to zero over a 24-unit foot-to-ground
gap. Swimming/flying pawns are excluded. Receiver plane/normal checks prevent
painting walls or floors below the traced surface; overlapping footprints use
the strongest contribution rather than stacking to black.

This is a local ambient-occlusion approximation, not an additional directional
shadow map or a foot-joint silhouette. It runs inside the existing world-lighting
shader, with no extra render pass, and is disabled in Original/Per-pixel modes.
Directional shadows and room exposure remain unchanged.

ARM64 build, signed APK installation and the production GPU pixel probe passed
at native 2520×1680 with 4× MSAA. The contact cases cover ground darkening,
height fade, receiver plane/normal/radius guards, Original mode, bright baked
lighting preservation and both buffer continuation paths. Existing moving
shadow, material, water, clarity and camera pixel checks also passed. See
[retained results](evidence/contact-shadows/gpu-check.log). This run did not
enable Vulkan validation layers or measure FPS. All four normal save files and
all saved preferences matched their pre-test copies. The final normal launch
returned to the menu; user navigation resumed before checkpoint restoration,
so automation stopped without changing their menu selection. Live traversal,
moving-platform and multi-character footprint appearance remain unverified.

## Configuration and portability

World feature bit 256 retains per-pixel lighting. Bit 512 enables Remastered;
the menu sets both (768). Existing values 0/1 migrate unchanged; value 2 selects
Remastered. Compare original effects temporarily disables both without changing
saved preferences. Video and Enhancements share one setting/model.

Shadow rendering is in the shared Vulkan backend, with no Android-only graphics
API. Other render backends retain their existing behavior. Windows/Linux Vulkan
support is source-level only until tested; iOS support is not established.

## Validation

- Android ARM64 and macOS builds; host settings migration/persistence checks.
- GPU pixel tests exercise moving casters, alpha cutouts, both mesh and BSP
  receivers, continuation during the shadow pass, and the disabled-mode gate.
- Existing original/per-pixel, depth, alpha, hit testing, camera coverage,
  screenshot, and clarity regressions remain in the Vulkan probe.
- Live native 2520×1680 / 4× MSAA and displayed FPS results are recorded below
  after device verification. A short scene sample is not a campaign-wide guarantee.

The gather ordering follows the [Khronos GLSL specification](https://docs.vulkan.org/glsl/latest/chapters/builtinfunctions.html).

### OPPO release measurements

Native **2520×1680, 4× MSAA**, existing bloom/clarity/detail settings retained:

| Short sample | Displayed FPS | Duration | 95th-percentile interval |
| --- | ---: | ---: | ---: |
| Original combat | 119.03 | 15.11 s | 8.33 ms |
| Remastered combat | 108.26 | 14.29 s | 16.66 ms |
| Remastered movement/camera/attack | 113.84 | 14.60 s | 16.66 ms |

These are SurfaceFlinger presentation timestamps, not engine-counter FPS. The
hardware compositor reported 120 Hz in all three samples. Remastered therefore
misses some 120 Hz deadlines; it is **not locked at 120 FPS**. These bounded
DM-Hildir samples do not establish campaign-wide performance. The movement sample
uses ordinary movement, camera and attack input and a replenished-health fixture;
it is separate from the recorded video, so screen recording does not affect it.

An initial implementation measured 95.35 FPS. Deferring the empty MSAA pass,
using a single depth gather and hoisting shared light calculations improved the
final combat sample to 108.26 FPS without reducing resolution, AA or shadow-map
resolution. Further performance work remains possible; do not conceal this cost
by reporting the panel's refresh rate as game FPS.

Final Vulkan core/synchronization validation on RagnarVillage passed with no
warnings/errors, including both a clean shadow-first frame and a frame where
PreRender had already drawn. See [evidence](evidence/remastered-lighting/).

The release APK was installed with the existing package/signing identity and no
validation layer. All ten protected save files were byte-identical afterward.
Only the saved lighting mode changed (Per-pixel → Remastered); resolution and
other preferences were preserved. The handoff session explicitly uses native
resolution and 4× MSAA. A 15-second live preview was recorded separately; its
engine overlay and video frame rate are not presentation-performance evidence.

Handoff: DeepUnder1Odin checkpoint restored through the game menu and paused.
Save hashes were checked again after restoration; all ten remained unchanged.
