# Contrast-adaptive sharpening

`CASShader.h` adapts AMD FidelityFX CAS's sharpen-only five-tap filter, using
full-precision, per-channel weights (the `CAS_SLOW` / `CAS_GO_SLOWER` path,
without `CAS_BETTER_DIAGONALS`). Source:
https://github.com/GPUOpen-Effects/FidelityFX-CAS/blob/9fabcc9a2c45f958aff55ddfda337e74ef894b7f/ffx-cas/ffx_cas.h

Upstream header SHA-256:
`3224935584202390ca9bc0149a45973ad2269f4d874abb2ff13625263fc036e0`.

Changes: GLSL vector notation, sampled texture input, safe denominator at black,
fixed conservative peak -1/8, and a blend with the unfiltered source for Low
(0.35) and Medium (0.65). Off bypasses the filter. This is an adaptation, not an
unmodified SDK integration. It runs at native resolution; the existing SGSR
reconstruction retains its own sharpening when upscaling is selected.

The MIT license is included here and packaged in the Android APK's licenses.
