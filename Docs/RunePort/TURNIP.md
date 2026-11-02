# Open Rune — app-local Turnip experiment

October 27, 2026. User-authorized comparison on OPPO OPD2515, Adreno 829.
This is an optional development driver experiment, not a system driver update.

## Driver and isolation

Selected release: [StevenMXZ Turnip 27.0.0-R1, Gen8 V38](https://github.com/StevenMXZ/Adreno-Tools-Drivers/releases/tag/v27.0.0-R1),
published October 19. It explicitly lists A829. The unpatched Gen8 asset is
used, rather than the separate additional-hacks asset. Actual Vulkan identity:
`turnip Mesa driver`, driver ID 18, `Mesa 27.0.0-devel (git-f28352d117)`.

Original driver binary SHA-256:
`41da037364520e0fe1a2e2fb6d9aa7c0116110daa0ec0c94cbfa6c08a52fd013`.

AdrenoTools source revision: `8fae8ce254dfc1344527e05301e43f37dea2df80`;
linkernsbypass submodule: `aa3975893d83ef1bc84c321ec60c65fbf1287887`.
Source checkout and downloaded driver are outside this repository.

`libopenrune_vulkan.so` owns one loader for the application process. SDL surface
creation, Volk dispatch and Swappy resolve through that same loader. Selecting
Turnip uses AdrenoTools' isolated namespace; selecting System loads Android's
normal Vulkan library. No global graphics setting, system library, root module,
power policy or other application's driver is changed. The loader logs its
selection; renderer initialization checks the actual driver ID and rejects a
silent Qualcomm fallback when Turnip is requested.

## Building and selecting

Configure the existing Android build with
`-DOPENRUNE_ADRENOTOOLS_SOURCE=/absolute/path/to/libadrenotools`, including its
submodule. Build `runeclassic`, then supply
`--turnip-driver /absolute/path/to/libvulkan_freedreno.so` to `Android/build_apk.py`.
Packaging includes the loader, four hook libraries, driver, AdrenoTools/linker
notices and Mesa licensing information. The distribution manifest records both
the input driver hash and final packaged library hashes. Review the exact
third-party driver build's complete notices before public redistribution;
this private experiment does not certify its third-party provenance/licensing.

Normal launches use System. For an isolated test, launch MainActivity with
`--es vulkanDriver turnip --es saveTest driver-compare`; `system` selects the
baseline. Only these two values are accepted, and library paths always come
from the app's own native library directory. A private `files/turnip-enabled`
marker can opt normal launches in; removing it restores System after a restart.
There is no general-purpose external driver importer or menu picker yet.

Both driver choice and test rendering overrides are session-only and leave
saved preferences intact. Changing drivers requires a fresh app process.
Original APK and preferences were backed up outside Git before installation.

## Validation method

`Tools/RuneValidation/android_driver_compare.py` uses isolated saves, seed 1,
native 2520×1680, 4× MSAA, enhanced rendering, world features 8103 and clarity 7.
It retains the user's 144 Hz per-app refresh policy. Fixed-step captures use
760 frames at a synthetic 120 Hz simulation step; their throughput is not
displayed FPS. Actual presentation is measured separately from SurfaceFlinger
after warm-up, in System / Turnip / Turnip / System order, with read-only clock
observations. Combat endurance replenishes health/durability for both drivers.

Combat state can diverge despite the fixed timestep; the harness records that
boundary instead of treating differing encounters as an identical workload.
The separate hall case fixes camera position and direction to isolate renderer
cost. Screenshots and downloaded binaries stay outside Git.

## Result: keep Qualcomm as the default

Actual SurfaceFlinger presentation, two warmed runs per driver in ABBA order:

| Scene | Qualcomm FPS | Turnip FPS | Qualcomm p95 frame interval | Turnip p95 frame interval |
| --- | --- | --- | --- | --- |
| Goblin endurance combat | 90.55 / 88.57 | 43.30 / 43.09 | 13.89 ms | 34.72 ms |
| Fixed hall camera | 91.28 / 88.72 | 47.44 / 47.85 | 13.89 ms | 27.79 ms |

Combat display samples span about 37 seconds each; hall samples about 14.5
seconds each, after the initial 600-frame profile. These are bounded samples,
not all-map or long-session validation. The combat fixture's fixed-step
observations diverged and its initial strict equivalence assertion failed;
the archived result explicitly records that limitation. No identical-combat
replay claim is made. The subsequent fixed-camera hall test completed cleanly.

The hall GPU scene pass took 8.99 ms on Qualcomm versus 17.03 ms on Turnip in
the fixed-step profiles. Both drivers reached the same 1.225 GHz GPU maximum;
hardware presentation remained at 144 Hz throughout. This points to substantially
more rendering cost with this Turnip build, but does not identify the exact
shader, tiling or driver mechanism responsible.

Both hall captures were visually reviewed: no obvious missing geometry or
material corruption. Their mean absolute RGB difference was 0.039/255, with
0.132% of channels differing by more than 3/255. A few larger local differences
remain; this is not a pixel-exact correctness proof.

The private Turnip-enabled build remains installed with System selected for
normal launches. Saved preferences are unchanged (SHA-256
`916a4078507a6447ecc2813b133c6406fd20f20f5152da0633959993d9e3bfc0`),
including Hardware Scaling 50%, 4× MSAA and the 144 Hz refresh request.
Tests used separate save directories. Compact logs, clock/display results,
build/package checks and provenance are in [evidence/turnip](evidence/turnip/).
Full captures and the rollback APK are outside Git under
`outputs/turnip-comparison` in the outer project directory.
