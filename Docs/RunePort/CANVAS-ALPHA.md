# Rune canvas opacity and the ChAoS startup fade

November 8, 2026. Fixed in shared source; Mac and Windows applications build. Mac Vulkan/MoltenVK rendering is tested at actual 2816×1762 scene size, four MSAA samples. No tablet update, public match, commit or push.

## Cause

The earlier deathmatch sweep captured ChAoS during its authored startup fade and described the black image too broadly as a black-world rendering failure. The original world does become visible once that fade ends. The real defect was that partially transparent fade tiles were rendered fully opaque, producing a black hold followed by an abrupt reveal.

Runtime evidence at frame60: canvas style5, AlphaScale0.508334, LevelFadeAlpha0.508334, FadeRate1. At frame120: opacity0.008334, yet the pre-fix screenshot still has an entirely black world. Renderer diagnostics showed1517 surfaces and101 actors submitted, normal player FlashScale(1,1,1) and zero FlashFog. Disabling scene lighting, fog or translucent world geometry did not reveal the world. Omitting the post-render canvas overlay did.

Rune uses render style5 for alpha-blended canvas tiles. Native DrawTile/DrawTileClipped only handled styles2–4, ignored Canvas.AlphaScale and consequently selected the opaque path for style5. Independently, all three tile renderers forced vertex alpha to1. Correcting only the native style flag would therefore be insufficient.

## Change

- Rune style5 tiles obtain finite, clamped opacity from Canvas.AlphaScale.
- An internal PF_CanvasAlpha marker distinguishes these calls from legacy tiles whose DrawColor alpha is frequently zero. The marker is stripped before pipeline selection.
- Vulkan, D3D11 and OpenGL tile paths use the existing premultiplied alpha blend pipeline, preserve the supplied opacity and premultiply tint RGB. Zero-opacity calls emit no geometry.
- Both DrawTile and DrawTileClipped retain their existing cursor/clipping behavior. Other styles and non-Rune calls keep their previous color/alpha behavior. Font subpixel flags remain excluded from marker interpretation.
- Original game packages, fade timing and scripts are unchanged. No special-case map brightness or lighting override was added.

## Validation

- Mac and Windows full application builds pass. D3D11 and OpenGL changes compile; live GPU validation in this task is Mac Vulkan/MoltenVK only.
- Paired ChAoS captures at60 frames: old world crop RGB mean0; fixed crop approximately11.05/11.98/15.82. At120 frames the fixed crop is22.28/24.14/31.88, approaching the fully revealed world. This follows the actual requested opacity instead of abruptly switching from black.
- At240 frames, after the fade, the central ChAoS world crop is pixel-identical before/after. An Alfheim world crop is also pixel-identical. Whole-frame differences are restricted to original HUD meter tiles that now correctly honor their scripted opacity.
- Additional ChAoS player starts and an upper platform were captured and visually reviewed. A final ten-view map pass completed with 300 profiled frames each at native scene size and four samples; all captures were reviewed. Some headings face close walls or void, as recorded in the sampled-view evidence.
- All temporary lighting, fog, transparency, post-render and tracing bypasses were removed before the final build.

Evidence: `/Users/rwillmore/OpenRuneArtifacts/ChaosRendering-20261108`, including exact arguments, original source snapshots, failed diagnostic compile and correction, reference/changed captures, logs and final source/binary hashes. Windows counterpart: `C:/OpenRune/outputs/chaos-rendering-20261108`.

The previous overnight receipts remain historical evidence. Their black ChAoS images are startup snapshots, not evidence of a permanently missing world. Native-scene timing is separate from presentation FPS; no all-map120FPS or Android performance claim is made.
