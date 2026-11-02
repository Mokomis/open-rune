# Open Rune — Native clarity controls — October 24, 2026

User-requested visual additions, independent of the earlier bloom/color preset:

- Adaptive sharpening: Off / Low / Medium. A compact MIT-licensed adaptation of
  AMD CAS's five-tap sharpen-only filter, blended at 0.35 / 0.65 with the source.
  Runs in the existing final presentation shader, with no extra framebuffer or
  pass. At upscaled resolution the existing SGSR path keeps its own sharpening;
  CAS is native-only. Source and attribution: `Thirdparty/cas/`.
- Texture filtering: 8x / 16x, clamped to the physical device's supported limit.
  OPPO accepted 16x. Changing it retires pending GPU work and invalidates texture
  descriptor caches using the same safe path as LOD bias changes. The existing
  LOD bias is unchanged.
- Shadow / midtone lift: Off / Low / Medium. Applies a smooth luminance-dependent
  gain after color/gamma correction, preserving black, white and channel ratios
  without pushing the brightest channel above one. It is SDR tone adjustment,
  not HDR or new scene lighting.

Android defaults: Low / 16x / Low. Tap **Clarity: Adjusted · settings** at the
upper right to change individual controls or switch the whole group to Original
(Off / 8x / Off). The adjusted choices are retained while comparing. Preferences
persist across process restarts. Bloom/color remains a separate button, now
explicitly labeled to distinguish it from clarity. Native / upscaled is unchanged.
The Android overlay is unaffected by the shader; the in-engine HUD shares the
scene's final processing, as it already does for bloom/color.

Engine `--clarity=N` and Android integer extra `clarity` provide deterministic
diagnostics: bits 0–1 sharpen level, bit 2 selects 16x, bits 3–4 shadow level.
`0` is Original and `13` is Low / 16x / Low. CLI desktop default stays 0; Android
uses preferences unless the intent supplies an override. Campaign-map and enemy
tools explicitly default to 0 and accept `--clarity 13` for adjusted checks.

## Verification

The native 2520x1680, 4x MSAA Vulkan fixture passed with core and synchronization
validation, without reported Vulkan warnings/errors. The existing 24-frame
depth, alpha, hit and continuation checks pass. Seven additional gray-pattern
frames exercise the actual final shader and gamma-corrected capture:

- Low/Medium sharpening changes gray boundaries but preserves flat gray patches.
- Shadow adjustment increases dark and middle gray; Medium increases it further.
- Black and white remain unchanged within readback quantization.
- Returning effects to Off reproduces all baseline pixels within one 8-bit value.
- Runtime 8x/16x changes complete without validation errors.

Marker: `RUNE_CLARITY_PROBE_OK`; Low changed 8039 pixels and Medium 10936 in the
synthetic boundary test. This is targeted correctness evidence, not a claim of
full campaign rendering parity. The normal APK excludes the validation library.

`Tools/RuneValidation/android_clarity.py` operates actual Android dialogs and
collects a stationary Original → Adjusted → Original presentation comparison.
It uses isolated saves and ends at Low / 16x / Low. Logs, timings and screenshots
are retained under `evidence/clarity/`. Short samples do not establish sustained
120 FPS or isolate a precise GPU cost; device clocks and scene animation vary.

The final layer-free APK's Hel3a comparison, within one process and unchanged
camera position, measured:

| Setting | Displayed FPS | Sample duration | p95 interval |
| --- | ---: | ---: | ---: |
| Original A | 118.898 | 18.352 s | 8.335 ms |
| Low / 16x / Low | 118.537 | 18.652 s | 8.335 ms |
| Original B | 118.490 | 18.702 s | 8.335 ms |

All three samples had zero intervals above 50 ms and a sampled GPU ceiling of
1.225 GHz. They show no appreciable displayed-FPS regression in this bounded
scene, not a universal or sustained guarantee. Screenshots were visually checked;
the adjustment is subtle and preserves the dark scene. Actual Android taps
verified Off/Low/Medium sharpening, Off/Low/Medium lift, 8x/16x filtering and the
Original/Adjusted comparison. The final preference is Low / 16x / Low.

The more expensive Dwarf1wwheel entry completed without script exceptions and
looked intact, but measured only 74.163 FPS in its first short adjusted sample.
A subsequent same-process Original → Adjusted → Original comparison measured
96.116 → 82.444 → 71.737 FPS. Its recorded GPU ceiling was 1.225 GHz in Original A,
changed from 1.225 GHz to 646 MHz during Adjusted, and remained 646 MHz in Original B.
This confounds a precise effect-cost comparison; Original is also below 120 FPS.
No thermal, power, clock or refresh policy was changed. These results must not be
described as a proven regression from the new effects, nor as proof of free
performance. Existing BubbleSystemOneShot / LANGUAGE / ISADDON warnings remain.

Installed layer-free APK SHA-256:
`a90efe5ebba475485ab1fc8e99b938913d3784f3873b75e15913e10ee1d8878b`.
Package `org.runeclassic.port` and existing signing key are retained. The local
deliverable and installed APK hashes match; see `installed-build.json`.
Normal icon launch was verified to restore Low / 16x / Low at native 4x, outside
test-save directories. Normal save hashes match this pass's starting snapshot,
and Rune's existing OPPO refresh property remains 3.
