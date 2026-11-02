# Open Rune — Close-camera correction — October 24, 2026

Backing Ragnar against a wall could show his face through the back of his head.
The close-camera alpha blend did not write depth, so hidden body surfaces were
blended over visible ones. Hidden bloodlust eye gear also left its attached eye
flames rendering independently.

## Behavior

The local player's camera fade now uses shared screen-space coverage with depth
writes: surviving pixels show the nearest body/equipment surface, while gaps
show the world. An 8x8 ordered pattern provides 64 coverage steps. When the
camera enters the player's radius, the body disappears completely; it smoothly
returns over 20 world units before rejoining the original script fade. Camera
tracing and movement are unchanged. This is a presentation correction, not a
claim of recovered native camera parity.

Rune attachments inherit hidden-parent visibility through AttachParent, bounded
to 16 ancestors. Attached player particles are suppressed during camera fade;
detached projectiles and world particles retain their normal behavior. This
deliberately omits decorative attached effects in a crowded view.

Diagnostic integer intent extra `cameraCoverage=0` restores the old mesh blend
for comparison; hidden-parent visibility remains fixed. Generic alpha blending
is unchanged. The new coverage path is implemented in the Vulkan renderer.

## Tablet evidence

See [evidence/camera-occlusion](evidence/camera-occlusion/). The final layer-free
APK is installed as the existing `org.runeclassic.port`, using the existing key.

- Actual Android movement in DM-Hildir: camera distance 58.57 at the wall,
  25.07 after backing in, and 180 after moving away. Screenshots were inspected:
  no face/eye bleed, complete hide at the closest position, and normal opaque
  Ragnar after moving away.
- Native 2520x1680, 4x MSAA: 1,277 presented frames over 10.65 seconds measured
  **119.80 FPS**. Median/p95 were 8.334 ms; the maximum interval was 16.668 ms.
  SurfaceFlinger timestamps and hardware display output both confirm 120 Hz.
  This is one short arena sample, not sustained or campaign-wide 120 FPS.
- Native 4x Vulkan core/synchronization validation passes without reported
  validation warnings/errors. Pixel tests cover five fade fractions and both
  near/far draw orders: hidden surfaces contribute no pixels and the background
  remains visible through coverage gaps. Existing depth/alpha/hit/clarity tests
  pass as well.
- ARM headless regression passes, including nested hidden attachments, visibility
  restoration and detached ownership checks.

## Save crash found during reproduction

The initial app log showed `stoi: no conversion (value = [0])` while saving INI
settings. UpdateFile incorrectly parsed brackets as part of an integer and did
not respect the loader's literal bracketed keys. It now prefers exact keys and
decodes a numeric suffix only for explicitly indexed base keys.

An ARM fixture updates and reloads bracketed save metadata twice, preserving
plain/repeated/literal-bracket keys, an unrelated section and a comment. Actual
Android isolated save-flow and fresh-process save-resume fixtures both pass.
These fixtures do not overwrite normal save slots.

Normal save hashes, saved graphics preferences and the Rune-only OPPO refresh
rule were verified unchanged. The user currently has Enhanced, Medium adaptive
sharpening / 16x filtering / Medium shadow lift and upscaling saved. Tests used
native-only intent overrides; the final normal launch honors saved preferences.

## Reproduce

Run sequentially with an unlocked tablet and the correct installed APK:

```sh
python3 Tools/RuneValidation/android_camera_coverage.py --device DEVICE --output OUTPUT/camera
python3 Tools/RuneValidation/android_campaign.py --device DEVICE --cases save-flow save-resume --output OUTPUT/save
```

The camera driver uses real Android movement buttons; review its screenshots in
addition to the numeric assertions. Use the validation-layer APK with
`android_renderer.py` for GPU checks, then reinstall the layer-free APK.

Private prerelease: [v0.7.2-camera-fix](https://github.com/Mokomis/rune-classic-android/releases/tag/v0.7.2-camera-fix).
Installed APK SHA-256: `297459b066396e6dfcd3e3427dda6cabcc5a6922bfd9eea204a3b2af750c4568`.
Full installed-build and preservation proof is in `evidence/camera-occlusion/installed-build.json`.
