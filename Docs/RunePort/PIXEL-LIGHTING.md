# Open Rune — Per-pixel lighting

Video and Enhancements share **Per-pixel lighting: Original / Per-pixel**.
Changes apply immediately and persist. Compare original effects temporarily
selects the original lighting without overwriting the preference. New installs
retain Original until explicitly changed.

The current implementation covers lit Rune meshes and world surfaces; see the
world-expansion section below. The initial character/equipment stage and its
validation are recorded here. Unlit and modulated mesh materials bypass the new
calculation. Emissive sprites/particles, fog, masked textures, translucency,
camera coverage and actor opacity keep their existing paths.

The shader uses the same up-to-eight actor lights, colors, radius falloff,
ScaleGlow and ambient/zone brightness as the original vertex lighting. It
interpolates posed world-space normals and positions, renormalizes the normal,
and calculates diffuse lighting for each fragment. The CPU skips redundant vertex-light
calculation for eligible Vulkan draws; unsupported backends keep it. The shader
loads only the light entries it needs instead of copying an eight-light packet
into per-fragment temporary storage. Two-sided materials retain
the original absolute-dot-product rule. There are no new specular highlights,
normal maps, shadows or authored assets in this feature.

A bounded storage buffer holds immutable light packets for each submitted frame.
Adjacent triangles with the same lighting reuse a packet. Each overlapping frame
owns its own buffer and descriptor; continuation waits before reusing storage.
The buffer is bounded to 4096 packets, flushing safely when full. Eight lights
are the existing actor-light limit, not a new nearest-light approximation.

The setting occupies bit 256 of the existing world-feature value; all previous
bits remain unchanged. The GPU implementation is Vulkan; other backends retain
vertex lighting. Original game packages and gameplay/save formats are unchanged.

## Validation

The native-4x GPU probe exercises a point light inside a coarse quad, normal
orientation, two-sided diffuse, unlit bypass, ambient-only lighting, all eight
lights, Original round-trip, and both rendering continuation paths. Existing
camera-coverage, depth, transparency, hit-buffer and clarity checks run alongside
it. Host settings tests cover packing, comparison and persistence.

`android_pixel_lighting.py` compares Original / Per-pixel / Original in isolated
GoblinWarrior and DarkViking encounters. The optional clear-lane fixture rotates
the player toward a collision-checked lane before spawning the enemy. Endurance
maintains health/body parts; the AI and animations remain live. These changing
encounters provide bounded displayed-FPS evidence, not a precise feature-cost
measurement or a campaign-wide 120 FPS guarantee.

`android_pixel_menu.py` exercises both real menu entries, Original, temporary
comparison and restart persistence. It intentionally leaves this feature enabled
and verifies that unrelated saved preferences remain unchanged.

## OPPO results (October 25, 2026)

Android ARM64 and desktop builds pass. The final native4x GPU probe passes with
core/synchronization validation enabled and no Vulkan warnings/errors. Desktop
Rune model/VM/native checks pass. Captures were inspected for Ragnar, the goblin
and Viking encounters; runtime mesh traces include their axes and shields.

Release APK measurements (validation layer removed), 2520×1680, 4x MSAA:

| Encounter | Original before | Per-pixel | Original after |
| --- | ---: | ---: | ---: |
| GoblinWarrior | 118.37 FPS | 118.93 FPS | 119.02 FPS |
| DarkViking | 119.09 FPS | 119.79 FPS | 119.58 FPS |

Each sample spans roughly 14 seconds. The hardware compositor reports 120 Hz;
none of the six samples has a presentation interval above 50 ms. Bloom/color,
clarity and surface relief were disabled equally during these comparisons.
Differences smaller than one FPS do not establish a performance improvement.
These are moving encounters in DM-Hildir, not all campaign areas or all devices.

The first implementation's Viking sample was 84.11 FPS; it duplicated CPU vertex
lighting and copied a full light packet into fragment temporaries. The final
version removes those costs. Initial wall-facing fixture captures were rejected
because the enemy was not visible; they are not part of the final evidence.

Both actual menu entries, Original round-trip, temporary comparison and process
restart persistence pass. The installed release leaves Per-pixel enabled. All ten
protected normal/campaign save files match their baseline hashes, and unrelated
preferences are identical. The legacy world value changes only from 7 to 263
(the new 256 bit). The release APK contains no validation layer.

Menu and save-preservation results are recorded alongside the release evidence.
See [evidence/pixel-lighting](evidence/pixel-lighting).

## World expansion — October 25

The same toggle now covers all lit Rune meshes, including props and scenery,
and BSP/lightmapped brush surfaces. Unlit materials and emissive sprites keep
their authored behavior. Geometry, collision and original game packages are
unchanged.

For world surfaces, up to eight ordinary point/wave lights per lightmap are
re-evaluated per fragment. Two packed float visibility textures preserve the
original baked per-light shadows. Ambient, special spotlight/searchlight/shell/
cylinder effects and any overflow lights remain in the original lightmap, so
those contributions are not discarded or counted twice. Shadows retain their
original resolution; this does not add dynamic shadow casting or new textures.
A separate cache preserves the Original path. Mover lightmap coordinates use
the same mapping as the existing CPU builder, including its baked-position policy.

The light buffer now has 336-byte entries. World vertices carry the packet index
in flag bits 20–31, leaving all four material texture bindings available. Per-pixel
mesh behavior is otherwise unchanged.

The native4x GPU regression verifies a point light inside a coarse surface,
blocked visibility, retained base illumination, eight lights, Original round-trip
and both render continuation paths. Core/synchronization validation reports no
warnings or errors. `lightingCompare=true` in an isolated bounded Android run
captures `frame.ppm-off.ppm` and `frame.ppm-on.ppm` at the same camera, actor pose
and simulation time; no simulation step occurs between the two renders.

The release build's short DM-Hildir living encounter measured **119.61 FPS with
Original lighting and 119.12 FPS with per-pixel lighting**, at native 2520×1680
and 4x MSAA, with the user's other effects retained. Both samples reported actual
120 Hz hardware refresh; these are approximately 14-second samples with live AI,
not a controlled GPU-cost measurement or a full-campaign performance guarantee.
Android/desktop builds and the desktop Rune native/model probe passed. The
installed release excludes the Vulkan validation layer. See
[evidence/world-lighting](evidence/world-lighting) for compact evidence.

Final preservation checks found all ten protected save files and unrelated
preferences unchanged. The existing package and signing key are retained.
