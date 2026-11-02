# Combat rendering pass — October 27, 2026

This pass profiled a fixed GoblinWarrior encounter in DM-Hildir on the OPPO
OPD2515 with the Qualcomm system Vulkan driver. All runs requested native
2520×1680 rendering and 4× MSAA, used isolated `combat-render-profile` or
`combat-radius-compare` saves, and kept the 144 Hz per-app display policy.
The 760-frame fixed-step runs profile 600 frames after warm-up. Their FPS is
rendering throughput, not measured presentation or a sustained 120 FPS claim.

## Cost isolation

The initial sweep held the encounter and all other features constant. The two
all-effects baseline scene times were 9.33 and 9.78 ms, showing drift during
sequential runs. Removing material maps measured 8.76 ms; limiting the extra
shadow-light setting measured 9.18 ms; switching from Remastered to per-pixel
lighting measured 7.07 ms. Disabling specular highlights in a separate
diagnostic build measured 8.77 ms, between enabled runs of 9.30 and 9.69 ms.
The diagnostic highlight switch was removed. These figures isolate approximate
costs but do not justify removing the visual features.

## Retained change

The BSP fragment shader now checks both baked shadow-mask samples before
calculating normal mapping, view direction and per-light highlights. When every
mask channel is zero, those lights contribute no diffuse or specular energy,
so the shader returns the same baked ambient color and contact-shadow result.
The shortcut is active only with the existing lighting-cull path. It does not
change the number of lights, materials, shadows, resolution or antialiasing.

Alternating optimized/baseline/optimized fixed-step runs measured GPU scene
times of **9.65 / 9.84 / 12.52 ms**. The first adjacent pair suggests a roughly
0.19 ms saving. The final run slowed sharply across scene rendering and CPU
timings, so this series does **not** establish a repeatable displayed-FPS gain.
All three runs had the same combat observations, and the final full-resolution
PPM frames were byte-for-byte identical. Saved Android preferences matched
before and after the test. The APK was installed as an update without clearing
game data. An attempted light-radius arithmetic change gave no repeatable gain
and was removed.

The final APK, without the temporary highlight diagnostic switch, built on
macOS and Android and completed another fixed encounter at 9.44 ms GPU scene
and 93.32 engine FPS. Its captured PPM SHA-256 matches the baseline and both
optimized variants. A separate roughly 13-second **live** combat presentation
sample measured **84.49 displayed FPS** and 9.55 ms GPU scene time at native
4× MSAA. The panel stayed at 144 Hz, while sampled GPU clocks varied from about
1.025 to 1.150 GHz. This is a single final-build observation, not a matched
displayed-FPS gain or a claim of sustained 120 FPS.

Raw logs, fixed frames, build variants and results are kept outside Git under
`../../../../outputs/combat-render-pass-2026-10-27/`. The next worthwhile
performance work is an instrumented breakdown of the remastered scene pass;
the current scene timer includes lighting, shadow passes and other scene work.
