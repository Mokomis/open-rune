# Open Rune offline texture pack

October 26, 2026. The user requested upscaled textures from the owned Rune source
graphics and explicitly selected a faithful local AI batch workflow. This is
asset production, separate from display-resolution scaling (hardware/GSR).
Multiplayer was paused during this texture work and later resumed on the Open
Rune server path.

## Scope and storage

The source exporter inspected 171 packages across Maps, Meshes, System and
Textures: 3,338 indexed source images and 54 procedural textures. Original package
SHA-256 hashes matched before and after extraction, with zero package failures.
Fourteen source images also have compressed copies; the exporter explicitly uses
their uncompressed indexed source art. These are menu/map-preview images.

All source images, downloaded models, generated images and the Python environment
stay outside Git at `../../outputs/texture-pack/` relative to this checkout.
Never commit these derived game assets or bundle the original packages.
The tools and this documentation can be committed to the private fork.

The output is `OpenRune-Textures-v1/`, with `2x/` and `4x/` folders, a `pack.json`
manifest, per-texture receipts, model/recipe metadata and validation results.
This earlier full faithful pack is **not installed or connected to the renderer**.
A subsequent four-asset generative pilot uses the new
[replacement pipeline](TEXTURE-PIPELINE.md). No tablet restart,
save changes, gameplay changes or FPS claim are part of asset generation.

## Completed first pack

- 2,610 texture identities generated at both 2× and 4×.
- 13,040 PNG files verified against SHA-256 receipts, source dimensions and masks;
  this includes 2,600 alpha-aware masked variants.
- 782 originals retained: 140 animated, 54 procedural, 505 UI/effect/sky package
  entries and 83 small utility textures. This is not an upscale of every asset.
- Logical PNG bytes: 2× **1,154,613,322**; 4× **4,128,991,909**, including masks
  and separate masked variants. Local hard-link reuse reduces physical disk use.
- Full validation passed. Reviewed examples include Ragnar, axe, stone, wood,
  masked foliage and the highest pixel-change outliers (runestone/grate/bricks).
  Artistic quality across all textures and in-game appearance remain unverified.

## Processing

- Official [Real-ESRGAN general v3](https://github.com/xinntao/Real-ESRGAN/blob/master/docs/model_zoo.md)
  weights, loaded through [Spandrel](https://github.com/chaiNNer-org/spandrel).
  The model runs locally on Apple MPS; no images are uploaded for processing.
- General/weak-denoise weights are interpolated at 0.25 denoise strength. No
  generative prompts, face restoration or separately invented material details.
  Super-resolution still estimates missing detail; visual fidelity needs review.
- Generate a 4× master, then Lanczos-downsample it to the 2× tier. These scales
  mean each dimension: a 256×256 texture becomes 512×512 or 1024×1024.
- Environment art uses periodic padding; mesh skin atlases use reflected padding.
  256-pixel tiles have 40 source pixels of context on every edge. The context
  exceeds the compact model's convolutional receptive radius.
- Preserve palette-zero coverage separately in `.mask.png` sidecars. Declared
  masked textures also receive `.masked.png` RGBA versions: fill hidden RGB from
  the nearest opaque texel before inference, then bilinearly interpolate the
  original coverage. The sidecar preserves the exact binary source mask with
  nearest-neighbor scaling. No AI-generated alpha shapes. The first foliage
  preview exposed blocky nearest-neighbor alpha edges; recipe 2 corrects this.
- Ordinary RGB stays available separately, because the renderer can use the same
  texture with different surface flags. For textures masked only by a surface
  flag, the sidecar is preserved, but alpha-aware RGB needs integration review.
- Identical source RGB, mask and boundary policy reuse identical results. Duplicate
  files may be hard links within the local pack. Treat generated packs as immutable;
  regenerate into a new directory rather than editing a linked file in place.
- Animated frames, procedural water/fire, special effect/sky/UI packages and tiny
  utility textures retain originals. The manifest records every original fallback.
  Static map-preview screenshots may be included as additional generated assets.

## Reproduce

Use an isolated virtual environment with `Tools/RuneTextures/requirements.txt`.
The currently tested host is Apple Silicon; other inference backends are not
validated. The model URLs are the official v0.2.5.0 release assets:

- `https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesr-general-x4v3.pth`
  SHA-256 `8dc7edb9ac80ccdc30c3a5dca6616509367f05fbc184ad95b731f05bece96292`
- `https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesr-general-wdn-x4v3.pth`
  SHA-256 `1641f8c4464b9f097c9fdda5589273713f67cf59f3d909e0bd688f0cee269dca`

From the checkout, with an already built desktop engine, an owned game directory
and the model files in the external output directory:

```sh
python Tools/RuneTextures/export_all.py \
  --engine build/SurrealEngine.app/Contents/MacOS/SurrealEngine \
  --game ../rune-classic --output ../../outputs/texture-pack
python Tools/RuneTextures/upscale.py --root ../../outputs/texture-pack \
  --models ../../outputs/texture-pack/models \
  --output ../../outputs/texture-pack/OpenRune-Textures-v1
python Tools/RuneTextures/test_tiling.py \
  ../../outputs/texture-pack/models/realesr-general-x4v3.pth
python Tools/RuneTextures/verify_pack.py --root ../../outputs/texture-pack \
  --pack ../../outputs/texture-pack/OpenRune-Textures-v1
```

Export supports resuming completed packages with matching source hashes; it
refuses to overwrite incomplete/stale exports. Upscaling resumes only a matching
recipe and verifies finished output hashes. Partial PNG writes are atomic.

## Validation and remaining integration

The exporter built successfully on the desktop host. The wrap and reflected
tiling regressions both matched full-image inference exactly (max RGB difference
zero) on the test input. The initial four visual samples passed every hash,
dimension and exact-mask comparison. Full-pack validation passed and is recorded alongside
the pack and in `evidence/texture-upscale/`; it is distinct from in-game validation.

Next integration must preserve original UV scale, texture identity, surface
masking and animation/procedural fallbacks, generate mipmaps, and impose a bounded
texture cache. Add Original/2×/4× selection in Video and Enhancements. Measure the
chosen scene at native resolution with 4× AA before recommending a default.
The 4× tier has 16× the top-level texels of the original; PNG file size is not GPU
memory use. No inference should run in the game. The current files are albedo
textures, not new normal/roughness/height maps.

Tool/model notices are in `Tools/RuneTextures/notices/`. Those licenses do not
relicense Rune's original or derived artwork; keep generated assets private.
