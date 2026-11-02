# Readable gameplay fonts

October 26, 2026. Single-player and multiplayer share the new readable text paths.

- Android HUD labels, meter values, messages, FPS/status and touch captions use the same cached system sans-serif face as menu body text. Message wrapping and text fitting measure that same face. Rune-themed menu headings retain their serif styling.
- Native canvas text uses the already-bundled Noto Sans Regular font, rasterized once per used font into a cached, garbage-collector-rooted atlas. Glyphs use 3x resolution, filtered premultiplied alpha and a minimum 22-pixel logical line cell. Existing serialized fonts and palette-based scripted textures retain their original data/path. Font/resource failure falls back to the original font and logs the reason.
- Canvas drawing, clipping and text measurement share UTF-8 glyph decoding for Rune. Latin text and common typographic punctuation are covered; this is not full multilingual font fallback. Other games retain their byte-oriented path.
- Multiplayer name labels use logical canvas dimensions and measured centering; the old pixel/logical-coordinate mix misplaced labels on high-resolution displays. Desktop multiplayer rows have more room for the larger text.
- Noto Sans is already distributed under its bundled SIL OFL notice; no new external font dependency or original asset modification.

## Validation

Desktop and Android builds pass; the signed update is installed on the OPPO. A four-client desktop TDM cycle passed scoring, results and travel after the native font replacement. Its first visual check prompted the final larger minimum size and row spacing. The final Android build passed the four-player TDM score/result/restart cycle and loaded the 22-pixel native atlas without fallback.

Visual checks: RagnarVillage narrative subtitles, touch captions and FPS panel; multiplayer health/score/status panels and the pause scoreboard. Text fits the inspected panels. Native 2520×1680 with 4× MSAA was confirmed in the renderer log. The approximately 17-second TDM sample presented 72.41 FPS, p99 20.84 ms, no intervals above 50 ms; this is a short UI validation sample, not a matched performance regression or sustained combat benchmark.

Normal save/preference hashes and the 144 Hz app rule are unchanged. Fixture cleanup leaves the title screen visible. Evidence: [readable-fonts](evidence/readable-fonts/).
