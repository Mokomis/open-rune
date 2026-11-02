# Remastered texture pipeline

October 26, 2026. Private Open Rune pilot. Multiplayer was paused during this
texture work and later resumed on the Open Rune server path.

## Full generative pack work

The four-asset pilot is followed by a separate resumable production queue in
`../../outputs/OpenRune-Remaster-production/`. `remaster_queue.py plan` verified
3,392 logical inventory entries and selected 2,374 static texture entries
represented by 1,670 unique RGB masters. It records duplicate aliases so one
generation can serve the same artwork in multiple packages. Animated frames,
effect/blended textures and skies remain explicit separate work groups; UI,
procedural and tiny utility textures retain their originals.

Generation uses built-in ChatGPT image generation within the user's Pro plan.
Do not switch this project to an API key, purchased credits, or a paid fallback.
Every generated texture must retain its source hash, exact prompt, master hash,
visual review and package receipt. A new audit compares broad color and
boundaries against the source, but those metrics cannot approve UV layout or
material identity on their own. The queue is a production workflow, not a claim
that all assets have been generated or installed.

Earlier laptop checkpoint: 30 masters approved, four candidates under review
(Ragnar's face, a wood texture, an alternate goblin body and a meat texture),
and 1,636 jobs pending. The approved masters expand to 45 game texture entries
through duplicate package aliases. The external
`OpenRune-Remaster-approved-astc-v9-village-set/` pack contains both 2× and 4× mip chains
for those 45 entries (360 ASTC KTX files). Independent ASTC decoding passed 90
color/normal comparisons, with a maximum 6.36/255 RMSE against prepared images.
The desktop Vulkan upload fixture passed for the four pilot textures with the
ASTC-only pack, and same-camera Original/Remastered captures showed Ragnar,
goblin and environment artwork in DM-Hildir. The environment comparison showed
sharper stone and wood detail without a large overall color shift. The first
stone pass proved too busy in the floor and arch. The current pack uses a
source-guided 0.7 stone detail gain by default, 0.5 on the Loki09 floor, and
0.4 plus a 35% original-art blend on the VArchDoor atlas. Matched-size desktop
crops show calmer surface grain while the floor joints and arch outline remain.
Additional reviewed village stone, plank, beam and mixed wall artwork now uses
the same quiet-detail policy. The second stone floor keeps its sparse orange
weathering, and the darker wall/plank variants reuse reviewed masters with
source-specific color correction. The beam's unused black region is restored
from its source mask. A separate carved beam candidate was rejected before
ingest because image generation returned the wrong aspect ratio; its prompt and
image are retained in the private queue. The desktop screenshots do not prove
all 45 aliases are visible or correct on every
model or map. The OPPO still has the earlier four-asset
pilot; the user requested laptop-only texture checks while its screen keeps
locking. No new Android runtime or FPS claim follows from this checkpoint.

`remaster_queue.py` tracks source and master SHA-256, exact generation prompts,
review status and quantitative drift reports. `export-recipe` writes all aliases
for approved masters; the source and master hashes are rechecked by the builder.
`revise` archives an earlier candidate, prompt, audit and master before replacing
it, so visual corrections remain traceable. That earlier checkpoint had four held
candidates, which were absent from the approved pack.
`record-failed` archives generation failures while leaving the job pending.
Approved aliases inherit their master's material recipe unless an alias has an
explicit override. Unknown material response uses a low-intensity neutral
profile, recorded in the exported recipe for later review. The builder can
`--resume` a partial batch by validating complete prior outputs, and
`--formats astc` limits a device-specific pack to ASTC. On devices without ASTC
support, those entries fall back to original artwork; build RGBA8 files for a
portable replacement pack.

## October 26 overnight continuation

The user resumed unattended laptop-only generation, with permission to tag
questionable candidates and move on. The current checkpoint is **128 approved
masters, 52 held for review, and 1,490 pending**: 98 newly approved masters and
48 newly held candidates in this run. Two wrong-aspect attempts remain archived;
Wd29_d passed a square retry, while VBEAM remains pending. The latest immutable external
pack is `OpenRune-Remaster-night-17`, with 151 logical texture entries, both 2x
and 4x mip chains, and 1,208 ASTC KTX files. Independent decoding passed 302
color/normal comparisons; maximum RMSE was 6.351/255. Only entries with identical
recipe dictionaries were copied from the previously verified pack before the
new complete pack was verified again.

The new set covers ship timber, plank floors, village framing/thatch, rusted
metal, two stone walls and the Viking shield. Prepared comparison sheets were
visually reviewed after source-color calibration. New stone uses 0.35–0.5 detail
gain, while wood uses source blends to restrain busy grain. Held candidates
include weapon atlases, a carved stone, shingles, a barred window, crate and
carved wall whose layout, ornament or grain needs further review. They are
excluded from the approved pack.

Bounded laptop original/remastered captures completed in RagnarVillage and
DM-Team_ships, but the visible replacements were primarily earlier artwork.
Those captures are not evidence that all new masters have been validated on
models or maps. No tablet testing or new FPS claim accompanies this batch.
Private prompts, master hashes, held reasons, comparison sheets and build/decode
receipts are in `../../outputs/OpenRune-Remaster-production/overnight-2026-10-26/`.
Its `CHECKPOINT.md` records the resumable workflow and latest verified pack.
A same-chat continuation was scheduled through 8 a.m. Central October 27;
subsequent batches should update that external checkpoint and these counts.
Built-in image generation remains the only generation backend; no API key,
paid fallback or artwork upload to Git is involved.

## What is implemented

The shared Video / Enhancements → Materials, water & shadows page includes
**Texture artwork: Original / Remastered 2× / Remastered 4×**. These are texture
dimensions, independent of display resolution. Choices persist; original-effects
comparison temporarily bypasses the pack. The page shows loaded GPU maps and a
128 MiB residency allowance. The default is Original. Four pilot identities are
provided; this is not the full generative pack.

Replacement images affect GPU uploads only. Original texture dimensions, UV
scales, game packages and saves remain unchanged. Each texture is matched by its
full package/group/name and an FNV-1a fingerprint of the original opaque RGB
pixels. This fingerprint prevents accidental artwork/version mismatches; it is
not a cryptographic authenticity signature. Private build receipts also record
SHA-256 for sources, masters and generated files.

The loader accepts a restricted, bounded [KTX1 layout](https://registry.khronos.org/KTX/specs/1.0/ktxspec.v1.html):
2D, power-of-two dimensions up to 2048, complete mip chains, RGBA8 or ASTC 4×4 /
6×6, no arrays, cubemaps or arbitrary metadata. It rejects invalid byte counts,
oversized files, unsupported layouts and mismatched source dimensions. It uses
ASTC only when the logical-device feature and sampled/filter/transfer format
capabilities are available. RGBA8 is the portable fallback. Missing/invalid
assets retain the original texture. Animated/procedural textures are excluded.

Cache changes drain pending GPU work before destroying images and descriptors.
The additional pack residency accounting includes payload bytes plus 64 KiB per
image; it is a conservative upload allowance, not a measurement of total driver
memory. Budget exhaustion uses originals until the next cache flush/map load.
This pilot has no asynchronous streaming/LRU eviction. Loading large packs can
still cause first-use stalls; full-pack rollout requires a separate streaming
and residency test. Do not silently activate the earlier 2,610-image pack.

## Art and materials

`Tools/RuneTextures/build_remaster.py` prepares reviewed masters without
modifying them. The exported production recipe and `remaster_samples.json`
define source identity, wrap mode and approximate material-region polygons.
Local broad RGB differences
are transferred from the original within those regions, preserving new fine
detail while restraining color/brightness drift. Regions need human review when
adding assets. This is calibration, not a guarantee of unchanged artistic intent.
Generated stone detail is compared with the source before its contrast is
reduced; this retains source cracks and mortar lines. Assets whose medium-size
generated detail still crowds the scene can use a documented `source_blend`.

Prepared channels:

- **Color / masked color:** 2× and 4× the original dimensions, uniformly capped
  at 2048 on the longest side. Both tiers use the same master/mip chain, so the
  lower tier retains the same color calibration and material response.
  Masked versions retain the original palette-zero silhouette. Transparent RGB
  is edge-filled; mip filtering uses premultiplied color in linear light and
  preserves alpha-test coverage as closely as each mip's pixel grid permits.
- **Normal + roughness:** RGB tangent-space normal, A roughness. Derived from
  high-frequency detail and material regions, not physically measured geometry.
  Normal mips are renormalized; lost normal variation increases roughness.
- **Material response:** R specular strength, G metal tint, B reserved, A opaque.
  Wood and metal within an axe can now respond differently. This feeds the
  existing Remastered lighting model, not a new full PBR renderer.

Tileable stone has periodic filtering and a narrow matching-edge pass. Other
assets retain their atlas layout and original shape. Generative landmark/UV
alignment and repeating patterns still require in-game review before expanding.
Automatic normal derivation cannot reliably distinguish painted highlights,
pores, cracks and physical height on every surface.

Color mips are filtered in linear light, then encoded back into the renderer's
existing display-encoded UNORM convention. Normal and response maps remain
linear data. This avoids an unreviewed global lighting/gamma change. No extra
sharpening or altered anisotropy is required; existing settings apply.

## Build and installation

External/private artifacts live at `../../outputs/OpenRune-Remaster-pilot-v2/`;
original masters remain in `../../outputs/texture-remaster-samples/`. Never commit
or distribute those derived Rune assets as part of this source repository.

Build with the existing texture Python environment:

```sh
python Tools/RuneTextures/build_remaster.py \
  --recipe Tools/RuneTextures/remaster_samples.json \
  --source /absolute/path/texture-pack/source \
  --samples /absolute/path/texture-remaster-samples \
  --output /absolute/path/new-pack \
  --astcenc /absolute/path/astcenc-neon
```

ASTC preparation uses the official Arm encoder 5.7.0, commit
`baff485b0ff36d2f95d28961605106502c653966`, Apache-2.0. It is a local offline tool;
no encoder library is linked into the game/APK. The command uses linear-profile
UNORM encoding, medium effort, 6×6 color and 4×4 data maps. Normal-map swizzling
is deliberately disabled because roughness occupies alpha.

Install the pack contents as `<game root>/TexturePacks/OpenRune/`. On Android this
is the app-private `files/game/TexturePacks/OpenRune/`. The root contains
`index.tsv`; images are `<lowercase identity>/<2x|4x>/<channel>[.astc].ktx`.
The uncompressed fallback and compressed version contain the same mip chain.
The builder refuses an existing output directory.

Desktop diagnostics can use `--texture-pack-root=/absolute/path/pack` to avoid
placing even replacement files inside the owned game directory.

To remove the pilot, select Original, close the app, and remove only the
`TexturePacks/OpenRune` folder. Original assets and ordinary saves are untouched.

## Validation

- Six Python tests cover linear-light mip filtering, empty/partial alpha,
  rectangular mip chains, color/detail preservation across material regions,
  periodic edges, normal normalization and per-region material response.
- The standalone C++ reader test runs under AddressSanitizer/UBSan: truncated
  files, invalid headers/dimensions/mip counts, trailing bytes and 10,000 seeded
  mutations, plus all 64 actual KTX pilot files.
- Android settings tests cover defaults, persistence, temporary comparison and
  independence from lighting preferences.
- `--probe-texture-pack` / Android `textureProbe=true` checks all four owned
  textures through real GPU uploads and descriptors, retaining original UVs and
  indexed pixels. Requires 4× texture quality and isolated saves.
- `--capture-texture-comparison` / `textureCompare=true` captures originals and
  replacements at the same camera, simulation time and lighting in a bounded
  run. This does not validate animation or campaign progression.

The desktop Vulkan run passed all four assets × four channels using ASTC,
retained original UV metadata/indexed pixels, and completed the same-pose
Original/Remastered capture in DM-Hildir. The initial fixture caught and fixed
the zero-based `UObject::exportIndex` versus one-based package-reference lookup.
ASTC round-trip decoding independently checks each 4× color and normal map.

Android ARM64 and signed APK builds pass. The pilot and app are installed on the
OPPO. At **2520×1680 native, 4× MSAA**, the four-asset/four-channel GPU fixture
passed with ASTC; paired captures and live Original → 2× → 4× switching passed.
The desktop RGBA-only fixture also passed all four assets/channels.

| Stationary DM-Hildir sample | Presented FPS | p95 frame interval | Duration |
| --- | ---: | ---: | ---: |
| Original artwork | 96.66 | 13.883 ms | 12.87 s |
| Remastered 4× | 95.94 | 13.886 ms | 12.85 s |

Both samples retained the OPPO's actual 144 Hz panel mode, with no presentation
intervals over 50 ms. These short samples show similar performance, not sustained
120 FPS or full-campaign coverage. Lighting/material settings were identical;
only artwork quality differed. The normal scene loaded six replacement maps,
about 5.3 MiB of accounted residency. The test-only upload fixture also exercises
assets not visible in that scene. In particular, the DM-Hildir player uses a
different Ragnar head variant; upload success is not visual proof of that pilot
head's model/UV fit. Review remaining actual model appearances before bulk art.

Only the new `menu.texturePack=2` preference was saved. Ordinary save files and all
pre-existing preferences remained unchanged. The live page needed a small label
padding fix to keep “Remastered 4×” on one line.
See [device evidence](evidence/texture-pipeline/validation.json). Private full
logs, receipts and captures are in `../../outputs/texture-pipeline-validation/`.

On October 27 the user removed the morning cutoff and changed review policy: cosmetic variations may ship; only concrete functional defects justify exclusion. The external production queue keeps `suspect-textures.json` and `suspect-textures.md` with concerns and source/master comparison paths. Existing held candidates await individual reassessment under that policy.

## October 27 Android WIP toggle and prepared-pack installation

Video and Enhancements now have a direct **WIP textures** switch. Off uses
Original artwork; On restores the last selected WIP size (2× for a first-time
selection). The shared Textures, water & shadows page retains **Original /
WIP 2× / WIP 4×** choices, a WIP loading status, and an explanation that missing
artwork remains original. Switching artwork does not enable unrelated lighting,
water, materials or display-resolution settings. Existing 4× selections survive
the first toggle off/on as well as subsequent restarts.

The tablet's four-entry pilot was replaced by the already prepared
`OpenRune-Remaster-night-17` pack: 151 logical entries and 1208 ASTC KTX files
with complete 2×/4× mip chains. Payload hashes and index were verified again on
the device. The original pilot remains in app-private
`files/game/TexturePacks/OpenRune-pilot-backup-20261027/`. Original game artwork
and saves remain separate. This does not resume generation or approve held
assets; the suspect list and paused remaster queue are unchanged.

The visible DM-Hildir runtime reads all 151 index entries and loads replacement
color/material maps at 4×. This is a bounded runtime check, not validation of
every remastered asset or a sustained performance claim. Current pack residency
still has the existing 128 MiB cap with original-art fallback.

Installed menu checks pass real Original/WIP switching on both Video and
Enhancements, synchronized state, restoration of the existing 4× preference,
WIP material-page labels and visible replacement-map loading. Both menu pages
were visually checked for fit. WIP 4× is left enabled for the user; constant
bloodlust stays off and the saved display scaling is retained. Evidence is in
`evidence/texture-pipeline/wip-toggle/` and local `outputs/wip-textures-ui/`.


## October 29 complete character WIP pack

`outputs/OpenRune-Remaster-characters-20261029` combines the previous environment
WIP work with the full selectable character roster. Its 120 unique character
masters resolve to 212 logical character identities, including package aliases;
the merged pack has 357 logical entries and 2,856 ASTC KTX payloads. Each entry has
2× and 4× color/mask, normal and response data with complete mip chains. Existing
source packages, saves and older versioned packs are intact. The Mac game path
`work/rune-classic/TexturePacks/OpenRune` is a symlink to this verified pack; no
ordinary saved graphics preference was changed and no tablet install was made.

Full verification passes all 2,856 file/hash/header/mip checks and 714 independent
ASTC color/normal decode checks. Maximum decoded RMSE against prepared review
images is 6.352/255 (verification threshold 15). The ASTC payload is 1,571,240,000
bytes on disk, not resident GPU memory. Existing runtime residency limits and
original-art fallback remain in effect.

All 37 character/skin choices have four-angle model previews; their stock/WIP
front and back comparisons were visually reviewed. The preview draws 117 unique
normal body atlases; the other three masters are damage/gore atlases. Eleven
cosmetic concerns are recorded without blocking use under the user's WIP policy.
Exact prompts, source/master hashes, calibrated images and review notes remain
in the external production queue and `outputs/character-complete-20261029`.

The final native-scale, 4× AA Mac multiplayer Wren/Jun fixture passes and explicitly
loads both characters' remastered atlases with no fallback. Its two concurrent
clients average about 106 FPS over the bounded 900-frame samples; this is neither
an isolated stock/WIP performance comparison nor an Android performance result.
The test also found and fixed direct multiplayer ignoring `--texture-pack`.
The normal menu-card renderer still uses stock textures; offline review overrides
do not change that renderer's production behavior.


### October 29 tablet installation follow-up

The user subsequently requested installation. Updated same-signature APK and the
complete merged character pack are now installed on OPD2515 `99cc517a`. All 2,857
deployed index/payload files pass on-device SHA-256 verification. The former pack
is retained at `files/game/TexturePacks/OpenRune-before-characters-20261029`.
A bounded 240-frame DM-Hildir launch completed at native 2520×1680 with 4× AA,
loading Ragnar WIP 2× color and material maps. This is a launch/upload check, not
full roster device validation or sustained FPS evidence. Saves and shared
preferences match their pre-install hashes. Returned to normal title launch with
saved settings, including WIP 2×. Evidence: `outputs/character-install-20261029`.
