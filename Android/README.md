November 8: dev.7/build7 is installed with the original certificate. See [matching client consolidation](../Docs/RunePort/CLIENT-SYNC-20261108.md) and [circular touch layout](../Docs/RunePort/TOUCH-CONTROLS.md). Normal saves and non-layout preferences are preserved. Only the Surreal production package remains for testing; Unity candidates are archived on Windows. Older deployment statements below describe earlier milestones.

November 7 Android pass-profiling follow-up COMPLETE; user requested stopping after
current feature tests. Optional --profile-passes=1 requires --profile-frames and adds
GPU timestamps at existing pass boundaries, without splitting render passes. Android
intent profilePasses is restricted to bounded, isolated diagnostics. 480 complete
samples per profiled case, zero dropped; pass sums match aggregate GPU interval.
Main scene accounts for 95-97% GPU time in the tested native2520x1680/4x-AA LostCrypt,
village and underwater water-wheel views; shadow maps0.09-0.17ms, presentation~0.4ms.
Bloom and reflected-water passes were inactive in these controlled views: zero time
is not a claim that those optional effects are free. Profiled static captures are
pixel-identical; underwater particles/animation vary between runs.
Two targeted trials were rejected and fully removed: extra world-only pipelines
1.85% frame/1.09% GPU gain (not worth additional pipelines); interpolated world lighting
coordinates3.76% frame/3.14% GPU gain with tiny shadow-edge pixel changes. No new rendering
optimization is retained. Existing qualified specialization/in-pass resolve defaults remain.
Android final APK adf55637ae4bd9f0d87676d9bc8280ed606f2086261d0b035342c965ce9197b3 is
installed with the original key. Final native4x default check83.60FPS, capture pixel-identical
to prior accepted deployment; saves, both preferences and both SE INIs byte-unchanged.
Normal title/scaling restored; profiler inactive normally. Windows primary source holds
the diagnostic changes, but Windows/Mac clients were not redeployed in this Android-only
follow-up. Evidence: ~/OpenRuneArtifacts/AndroidPassOptimization-20261107 and matching
C:/OpenRune/outputs/android-pass-optimization-20261107. No commits/pushes. Work stopped;
do not start another optimization or resume unrelated automation without user direction.

November 7 shader deployment: the Windows-host ARM64 build uses SDK build-tools 36.0.0,
platform 35, NDK 27.0.12077973 and SDL 2.32.10. The APK is aligned/notice-verified on
Windows and signed on the Mac with the original installed certificate. The signing
key now has a protected local backup outside iCloud; do not include it in source,
artifacts transferred to Windows, or distributions. `build_apk.py --unsigned` supports
this split build/sign workflow; `--keystore` accepts an existing key without replacing it.
Windows response-file handling and portable notice/manifest paths are verified by packaging.
Native 2520x1680, 4x-AA ABBA comparisons on OPD2515/Qualcomm Adreno 829 showed 7.4-8.7%
lower mean frame time in LostCrypt and RagnarVillage with pixel-identical captures.
In-pass resolve and lighting specialization default on for this GPU/proprietary driver;
other Android GPUs and Turnip retain their previous defaults. Diagnostic intent
`specializedLighting` requires bounded frames and isolated `saveTest`. Existing
packed HDR, overlap, optional Turnip payload and user scaling/refresh choices are preserved.
Evidence: C:/OpenRune/outputs/shader-deployment-20261107 and Mac
~/OpenRuneArtifacts/ShaderDeployment-20261107. This is representative rendering
qualification, not a new full campaign or multiplayer certification.

# Current deployment — November 3, 2026

The [matching client update](../Docs/RunePort/CLIENT-SYNC-20261103.md) is installed on OPD2515 with the existing signing key. It includes the mover fixes, material lighting/probes, enhanced gore and proportion-preserving corpse carry/throw poses. Protocol/ALPN and native rules are 22. Saved graphics/audio choices and original saves were preserved; no new texture or sound pack was deployed. Older not-built/not-installed statements below describe earlier checkpoints.

November 3 source follow-up: [corpse carrying/throwing](../Docs/RunePort/CORPSE-THROWING.md) uses existing Use, Attack and Throw controls, with slower carry movement and eligible bot support. Native match rules 22; wire/ALPN 22 unchanged. Carried corpses now use proportion-preserving relaxed poses and matching visibility bounds. Mac validation only for this change; Android has not been rebuilt/installed.

# Open Rune — Android build and device guide

November 3 gore: [Original / Enhanced gore presentation](../Docs/RunePort/ENHANCED-GORE.md). Independent Video → Effects & weather setting; local 3D fragments, tumbling, wet wound materials, aligned trails/impact marks/pools and bounded post-animation corpse settling. Original actor physics, pickup limbs, damage, callbacks, protocol and saves are retained. Mac native 2816×1762/4× rendered on/off checks preserve 3,540 actor poses per run; existing 24 gib cases and native multiplayer sever/corpse regression (430 snapshots / 4,093 ticks) pass. Frozen same-scene render cost ~0.35 ms plus ~0.02 ms average update, separate measurements rather than an Android FPS claim. Windows/Android menus/JNI wired, not deployed. No tablet access, Windows update, commit or push.


November 3 material lighting: [GGX highlights and local environment probes](../Docs/RunePort/MATERIAL-LIGHTING-PROBES.md). Independent Video → Lighting & shadows controls, retained defaults, shared Vulkan implementation. Mac native 2816×1762/4× focused GPU, field/preference and active-combat checks pass. Alternating Hildir/village blocks add about 0.62/0.22 ms (4.3%/1.6% engine FPS); one-time map bake is separate. Broad reflections use static scenery and vertex-weighted radiance; this is not full PBR/GI. Older full hit-buffer GPU tests hit a Mac resolve limitation; the lift camera is unsuitable for a representative visual benchmark. No Android/Windows deployment, commit or push.


November 2 moving lifts: [fast fixed-world visibility](../Docs/RunePort/LIFT-PERFORMANCE.md) is installed. Moving-lift native 2520×1680/4× fixtures improve LostCrypt ~30.5→75.2 and HelLift ~100.9→116.5 engine FPS with identical captures/motion. This accelerates lighting queries without changing collision or reducing settings. Diagnostic `fastWorldVisibility` and `liftProbe` extras require isolated `saveTest` runs; ordinary sessions do not activate the automated lift/camera fixture.

November 2 LostCrypt performance fix: [cached fixed-world light visibility](../Docs/RunePort/LOSTCRYPT-PERFORMANCE.md) is installed on OPD2515. Native 2520×1680/4× matched checks improve ~40→83 engine FPS with identical captures; moving brushes are still tested every frame. Normal saves and unrelated settings remain intact. The tablet is back in LostCrypt with its current eight-light / Hardware 50% selection. These are bounded comparisons, not sustained campaign-wide FPS guarantees.

November 2 save-menu correction: normal save-folder metadata is now published on the first native frame before the menu becomes ready; covered title screens no longer leave the Load page showing false Empty slots while waiting sixty frames. Installed on OPD2515; original checkpoint/manual save and both backups match the pre-update hashes, and both active Load buttons are verified. Tests must finish with a cold normal launch without `saveTest` extras. `Tools/RuneValidation/android_save_menu.py` checks normal startup visibility without loading or modifying a save.

November 2 shadow follow-up: [Video → Lighting & shadows now offers one/three/four/six/eight nearby shadow lights](../Docs/RunePort/ANDROID-SHADOW-LIGHTS.md), superseding the previous Windows-only restriction. Requires Remastered lighting. The matching eight-slot renderer and live/persistent setting are installed on OPD2515, with native 2520×1680/4× AA GPU and real-map checks. Existing three-light choice and normal data are preserved. Higher settings are maximums, depend on available authored lights and cost additional rendering time.

November 2 installation: the current signed ARM64 client is installed on OPPO OPD2515 over USB. Installed APK bytes match the local build; the previous signing certificate, optional Turnip payload, saves, configuration and preferences are preserved. The tablet is locked, so no launch/gameplay test was performed. The sound pack remains shelved and was not copied. Higher shadow-light choices are Windows-only; Android retains its three-light capacity. Evidence: `outputs/android-deploy-20261102` above this checkout. Older not-installed statements below are historical for their respective milestones.

November 2: [optional layered sound effects](../Docs/RunePort/AUDIO-REMASTER.md) are under Audio → Sound effects, Original / Remastered (WIP), restart required. The separate pack belongs in app-private `files/game/SoundPacks/OpenRune-Layered-v1`. Missing or mismatched files keep their originals. APK and private pack are prepared; neither is installed on the tablet for this update. Original stays the default.

November 1: [first-run setup](../Docs/RunePort/FIRST-RUN.md) is now the launcher entry point. A fresh install can import an accessible complete Rune Classic folder through the system picker or open its Steam store page. Existing valid `files/game` installations skip setup. This change is built, not installed/device-tested.

November 1 menu cleanup: [current category/section navigation](../Docs/RunePort/MENU-CLEANUP.md). Enhancements is removed; its options live in their parent menus. HUD controls are under Interface, developer tools/diagnostics under About & support, smoothing under Multiplayer → Connection settings, and original-visual comparison under Video → Effects & weather. Match rules and ordered map rotation have their own hosting pages. Existing preference keys and saved layouts are retained. Older menu paths below are historical.

November 1: [protocol 22 movement prediction/adaptive smoothing](../Docs/RunePort/NETWORK-PREDICTION-SMOOTHING.md) is installed on the OPPO, matching the updated Windows build. The actual Android Network settings page and a native-resolution/4× AA Windows-host join pass. The default discovery runtime cache is rebuilt to 22. Earlier milestone protocol and installation statements below are historical; use the linked checkpoint for current hashes and scope.

October 29 character selection: [36 characters / 37 skin combinations](../Docs/RunePort/CHARACTER-SELECTION.md), separate multiplayer/campaign choices, Android horizontal carousel and protocol 20. Mac validation and an Android build are documented there; no tablet installation is implied.

Snow is available in Video and Enhancements as **Off / Light / Heavy**. Off keeps original map weather; the other settings use bounded outdoor particles and roof shelter. See [snow behavior and Mac measurements](../Docs/RunePort/SNOW.md). Signed build ready; this addition has not yet been installed or tested on the tablet.

Public multiplayer: [HyperDHT hosting and Find games](../Docs/RunePort/HYPERDHT-DISCOVERY.md). Prepare `Tools/RuneDiscovery` with `npm ci` and `python3 build_android.py` before packaging. The runtime is app-local; public joining needs no invitation file. Both hosting directions now pass between laptop Wi-Fi and the OPPO on a cellular hotspot. Other NAT combinations and human internet combat remain unvalidated.


Connection/lifecycle follow-up: [Android multiplayer recovery](../Docs/RunePort/ANDROID-CONNECTION-RECOVERY.md).
Temporary outages release input and offer Resume after recovery; expired sessions
return to Multiplayer with rejoin and bounded handshake retries.

Visual effects: [fire, embers and combat particles](../Docs/RunePort/FIRE-EFFECTS.md).
Video and Enhancements share the live Original / Enhanced comparison setting.

Optional development experiment: [app-local Turnip driver comparison](../Docs/RunePort/TURNIP.md).
System remains the default. The driver override affects Open Rune only and
requires a restart; it does not replace the Android system Vulkan driver.

Current bloodlust checkpoint: [leap, shield charge and amber touch cues](../Docs/RunePort/BLOODLUST-MOVES.md). Double-tap Jump, or hold Block + Attack with an equipped shield, during active bloodlust with enough meter.

Current AI checkpoint: [tactical humanoid combat](../Docs/RunePort/TACTICAL-AI.md). Gameplay and Enhancements share Original/Tactical behavior, including the existing kicks and shield bashes. Laptop validation is separate from Android installation or FPS evidence.

Current multiplayer source checkpoint: [private multiplayer coverage and limits](../Docs/RunePort/PRIVATE-MULTIPLAYER-STATUS.md). Protocol/ALPN 17, native rules 17 and invitation format 4. Laptop automation covers 44 maps and 157 supported map/variant combinations. The installed Android build matches protocol 17; tablet/laptop LAN checks cover hosting, joining, rejoining and one representative map per mode. See the UI checkpoint for exact device coverage; the full map matrix remains laptop evidence. Older checkpoint versions below are historical.

Open Rune is the native ARM64 project documented in `../CLAUDE.md`. Commands below use an existing SDK/NDK/JDK. They do not install SDKs or download game assets. The original tested host is macOS ARM64; Linux host packaging paths must use its NDK prebuilt directory. Windows host building is not validated.

## Experimental multiplayer

The [Android multiplayer UI](../Docs/RunePort/MULTIPLAYER-UI.md) now includes
Host game, map/mode/rotation selection, per-player invitation sharing/saving,
Join with invitation and Rejoin. A separate foreground process hosts the match
on the device. The protocol 17 APK is installed and the real touchscreen/LAN flows pass.
The [native performance follow-up](../Docs/RunePort/NATIVE-HOST-PERFORMANCE.md)
adds supported packed HDR buffers and hidden-menu idling. Controlled rendering
improves about 7%; sustained native 4x combat remains thermally constrained.
The [lighting buffer follow-up](../Docs/RunePort/UNIFORM-LIGHTING-PERFORMANCE.md)
also reduces fixed-scene GPU time by 3–7% without changing lighting values.
This is not an Android player-capacity guarantee. Default builds
keep networking off. Device fixtures may pass `--es networkInvite NAME.rune-invite`
for a file already in app-private `files/multiplayer/`, with `saveTest` isolation.

## Physical controllers

USB/Bluetooth gamepads now route both sticks, triggers and buttons into gameplay,
with reconnect detection and a live controller name under Controls. See
[CONTROLLERS.md](../Docs/RunePort/CONTROLLERS.md) for mappings and device evidence.
A jumps, X/RT attacks, Y/L3 uses, LB/LT blocks, RB throws and R3 kicks. Start pauses.
Look sensitivity/inversion also applies to the right stick.
Connected gamepads automatically hide the touch movement/action controls and
touch-look area. The themed Menu button and HUD remain visible. Disconnecting
the last gamepad restores touch controls according to the saved setting.

## Materials, water and multiple shadows

The same page now contains **Texture artwork: Original / WIP 2× / WIP 4×**,
with loaded-map and memory readouts. See [texture pipeline](../Docs/RunePort/TEXTURE-PIPELINE.md)
for the private four-asset pilot, original-art fallback and installation layout.

Video and Enhancements share an additional Textures, water & shadows page.
See [feature scope and validation](../Docs/RunePort/MATERIALS-WATER-SHADOWS.md) for generated normal/roughness
maps, planar water reflections and up to three nearby shadow-casting lights.

## Lighting modes

Video and Enhancements share **Lighting mode: Original / Per-pixel / Remastered**.
Remastered adds stronger lighting, material highlights and nearby moving shadows.
See [REMASTERED-LIGHTING.md](../Docs/RunePort/REMASTERED-LIGHTING.md) for scope,
performance and limitations, and [PIXEL-LIGHTING.md](../Docs/RunePort/PIXEL-LIGHTING.md)
for the retained per-pixel path. Original-effects comparison includes the mode.

## Rumble

Controls → Rumble & vibration (also under Enhancements) provides On/Off,
intensity, Automatic/Controller/Device output and Test rumble. Output is selected
by Android capabilities, not controller brand. See [RUMBLE.md](../Docs/RunePort/RUMBLE.md)
for events, portability, device limitations and validation.

## Shield bash

Double-tap **Block** within 0.3 seconds while carrying a shield. The first press
still blocks immediately. A bash causes a small shove and brief stagger, with a
four-second cooldown; raised shields prevent its damage/stagger. Gameplay and
Enhancements share **Shield bash** (default On). The detector follows the normal
Block binding, including touch and Right Ctrl. See
[SHIELD-BASH.md](../Docs/RunePort/SHIELD-BASH.md) for behavior and validation.

## Combat kick

**Kick** on touch, **F** on keyboard and **right-stick click** on Android start a
combat kick. Compatible humanoid enemies can use it too. A raised, facing shield
reduces the full shove to small recoil and prevents damage. Gameplay and
Enhancements share the **Combat kicks** toggle (default On). See
[KICK.md](../Docs/RunePort/KICK.md) for eligibility, balance and evidence.

## Compact HUD and layout editing

See [HUD.md](../Docs/RunePort/HUD.md) for the new themed meters, touch/controller
layout editor, individual resizing, orientation, persistence and device evidence.
Open **Menu → Edit HUD**. Gameplay and Enhancements share the Open Rune/Original HUD
selector. The touch controls have their own [layout editor](../Docs/RunePort/TOUCH-LAYOUT.md), including both analog sticks, independent position/size/opacity and automatic hiding when a controller is connected.

## Rune menu system (October 25, 2026)

Video and Enhancements share **Camera & field of view**: a 70–120° slider,
automatic aspect correction, legacy framing and reset. See
[widescreen support](../Docs/RunePort/WIDESCREEN.md) for zoom/cinematic behavior,
current-angle readout and isolated app-only widescreen previews.

The Android title/pause menu now follows the approved dark-stone, ivory and amber
mockups. `RuneMenuView.java` draws the artwork, borders and logo around native
Android controls; `RuneSettings.java` owns the shared values. Video, Audio,
Controls, Gameplay and Enhancements are tabs, with the same underlying values
in both their category and Enhancements. Settings apply immediately and persist.
The old prototype top-row popups have been removed. Menu, Escape and Android Back
open the new pause menu. Volume keys retain Android behavior.

Video includes native/75% GSR rendering, Off/2x/4x MSAA, 30/60/120 FPS targets,
frame pacing, bloom/color, sharpening, 8x/16x anisotropy, shadow/midtone lift,
surface detail/depth, per-pixel lighting and the engine-FPS display. Audio exposes master/music/effects
volume, environmental sound and headphone HRTF. Controls exposes look sensitivity,
vertical inversion, touch-control visibility/opacity, Edit touch controls and the existing bindings.
Gameplay contains the **new-campaign** difficulty and developer/about screens.
The combat arena is under Developer tools. The About screen opens bundled notices.

`Compare original effects` temporarily disables bloom/color, clarity and world/audio
effects. Resolution, MSAA, target cadence, volume and saved effect choices stay
unchanged. Editing an effect leaves comparison mode. Restore defaults affects
only the current page and asks before applying the reset.

Pause freezes world time and simulation, blocks gameplay input and clears held
controls. The title's bootstrap scene cannot write checkpoints. Save/load shows
quick save (9), checkpoint (10, load only), and nine manual slots (0-8), including
real timestamps and map metadata when available. Existing saves and unsaved
sessions require confirmation before replacement. Manual saves reuse Rune's
original QuickSave eligibility check; successful saves/loads run on the engine
thread. Test launches continue to use `--save-directory` isolation.

Death opens the themed recovery screen after a short delay, with Retry checkpoint,
Load game and Return to title. It hides combat controls and prevents resuming a
dead session. See [DEATH-RECOVERY.md](../Docs/RunePort/DEATH-RECOVERY.md).

### Quality levels and compatibility

| Effect | Low | Medium (previous maximum) | High |
| --- | --- | --- | --- |
| Adaptive sharpening | 0.35 | 0.65 | 0.90 |
| Shadow/midtone lift | 0.35 | 0.65 | 0.90 |
| Surface relief shading | 0.25 | 0.45 | 0.70 |
| Surface parallax | 4 steps / 0.35 units | 8 steps / 0.70 units | 16 steps / 1.05 units |

All four also have Off. Relief remains limited to eligible static stone/wood
materials; it does not change collision or geometry. High depth has a real GPU
cost. The menu itself is Android-specific; the underlying engine changes also
build on the desktop host. Legacy enabled detail/depth settings map to Medium, preserving their old
strength. Anisotropy remains capped at the device-supported maximum of 16x.

Clarity's two-bit fields now accept level 3. `worldFeatures` keeps its existing
low bits (audio=1, detail=2, depth=4, HRTF=8); bit 8 enables per-pixel mesh lighting.
Bits 4-5 and 6-7 carry detail/depth
quality. A zero quality field with its enable bit set means legacy Medium.
Quality controls are saved in the existing Activity preferences; intent overrides
remain session-local unless the user edits that setting family.

Swappy stays initialized for live pacing on/off changes. Presentation work is
drained before switching paths. The selected target updates Swappy's interval;
when pacing is off, the existing CPU limiter uses the same target. This is a
frame-rate target, not a sustained-performance claim. The package ID/signing key
and the OPPO's Rune-specific refresh rule remain unchanged.

### Menu checks

`Tools/RuneValidation/MenuSettingsTest.java` checks preference migration, quality
packing, comparison restoration, override isolation and process-restart values.
Compile it with `Android/RuneSettings.java` against the installed Android SDK jar,
then run `org.runeclassic.port.MenuSettingsTest` on the host JVM.

`python3 Tools/RuneValidation/android_menu.py --device DEVICE --output OUTPUT`
exercises the actual touch UI, both appearances of settings, comparison, volume,
live MSAA/pacing, paused world time/location, hidden controls, manual/quick saves,
load and persistence. It uses `validation-saves/menu-ui`, backs up/restores Activity
preferences and stops the test process when complete. Add `--validation` only
with the validation-layer APK. Use `android_touch_pickup.py` for real Use/Throw
regression coverage and `android_present.py` for displayed FPS after warmup.

Observed on OPPO OPD2515, October 25: the actual menu regression passed,
including shared High settings, original comparison, mixer gain, live MSAA/pacing,
frozen world time/location, manual and quick saves, loading, and restart
persistence. Vulkan synchronization validation reported no errors or warnings.
The actual touch Throw/Use/food regression also passed. Native Android and macOS
host builds passed. Normal save files matched their pre-update SHA-256 hashes.
Evidence: [menu validation](../Docs/RunePort/evidence/menus/).

Short SinkingShip2 presentation samples at native 2520×1680 / 4x MSAA:

| Configuration | Displayed FPS |
| --- | ---: |
| Environmental audio only, Low clarity, original bloom/color, 120 target | 119.43 |
| Same baseline, live 60 FPS target | 60.01 |
| Same baseline, live 30 FPS target | 30.04 |
| Same baseline, restored 120 FPS target | 119.39 |
| Medium surface detail/depth, High clarity, enhanced bloom/color | 95.95 |
| High surface detail/depth, High clarity, enhanced bloom/color | 72.69 |

These are bounded opening-scene samples, not full-campaign guarantees or an
isolated benchmark of either surface effect. Existing user quality choices are
preserved; High is available but is not enabled automatically. No additional
settings-matrix testing is required for this menu milestone.

The artwork in `Android/assets/` was generated from the approved mockups, with
all controls and text removed; live text and controls are rendered by the app.
It is independent of the user's proprietary Rune game assets.

## Dependencies and provenance

**Clarity → World detail / sound** controls environmental reverb/muffling,
normal-map surface detail, optional parallax and headphone HRTF. Intent extra
`worldFeatures` uses bits 1/2/4/8 respectively and does not save an override.
See [WORLD-FEATURES.md](../Docs/RunePort/WORLD-FEATURES.md) for original-code
provenance, the limited material selection and measured performance costs.

The **Clarity** menu defaults to Low adaptive sharpening, 16x filtering and Low
shadow/midtone lift. It has independent settings and an Original comparison.
See [CLARITY.md](../Docs/RunePort/CLARITY.md) for rendering details and evidence.
For reproducible launches, integer intent extra `clarity` overrides preferences:
`0` restores Original and `13` selects Low / 16x / Low. It does not alter the
separate bloom/color or native/upscaled preferences.

- SurrealEngine base: `a041728695b929fb51b019f8387bb42cc19cbf19`.
- SDL2: release-2.32.10, exact commit `5d249570393f7a37e037abf22cd6012a4cc56a71`.
- NDK `27.0.12077973`, ABI `arm64-v8a`, Android API 28.
- Android platform SDK 35 and build-tools **36.0.0** (packager currently expects those exact directory names).
- JDK 21, CMake, Python 3, C++20 host compiler.
- Host desktop configure dependencies: see `../Docs/Building.md`. macOS configure fetches hash-pinned MoltenVK 1.4.2.
- Python game analysis dependencies are separate, in `../Tools/RuneRecovery/requirements.txt`.

## Build native libraries and diagnostic executable

From the repository root, set paths for the local machine:

```sh
export RUNE_SDK=/opt/homebrew/share/android-commandlinetools
export RUNE_NDK="$RUNE_SDK/ndk/27.0.12077973"
export RUNE_JDK=/opt/homebrew/opt/openjdk@21
export RUNE_SDL=/absolute/path/to/SDL2
```

For a new SDL checkout (skip if the pinned local checkout already exists):

```sh
git clone --branch release-2.32.10 --depth 1 https://github.com/libsdl-org/SDL.git "$RUNE_SDL"
git -C "$RUNE_SDL" rev-parse HEAD
# Expected: 5d249570393f7a37e037abf22cd6012a4cc56a71
```

Cross compilation requires a **host** zipdir utility. The original Mac already has it at `build/zipdir`. On another host:

```sh
cmake -S . -B build-host -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-host --target zipdir -j6
export RUNE_HOST_ZIPDIR="$PWD/build-host/zipdir"
```

Or reuse the original Mac's built utility:

```sh
export RUNE_HOST_ZIPDIR="$PWD/build/zipdir"
```

Configure/build (SDL is provided separately; configure downloads the SHA-256-pinned AndroidX frame-pacing 2.1.3 AAR from Google Maven):

```sh
cmake -S . -B build-android \
  -DCMAKE_TOOLCHAIN_FILE="$RUNE_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DSURREAL_ANDROID_SDL_SOURCE="$RUNE_SDL" \
  -DSURREAL_HOST_ZIPDIR="$RUNE_HOST_ZIPDIR"
cmake --build build-android --target runeclassic SurrealEngine -j6
```

RelWithDebInfo uses `-O2`; debug symbols do not mean the engine is unoptimized. `runeclassic` produces the Activity's shared native entry library; `SurrealEngine` produces a standalone executable used for rooted headless probes.

## Package the APK

Packaging also produces `notices.zip`, `lgpl-sources.zip` and
`distribution-manifest.json`. Distribute all four files together. Read
[distribution requirements and library replacement](../Docs/RunePort/DISTRIBUTION.md).
Rebuild libraries from the packaging checkout first; notices alone do not
satisfy the LGPL source and replacement requirements.

```sh
python3 Android/build_apk.py \
  --sdk "$RUNE_SDK" --java-home "$RUNE_JDK" \
  --sdl-source "$RUNE_SDL" --build-dir "$PWD/build-android" \
  --strip "$RUNE_NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-strip"
```

On a Linux host, use `linux-x86_64` in the strip path. Output: `build-android/apk/open-rune.apk`. The packager compiles the SDL Java Activity, builds DEX/resources, includes the native libraries and engine PK3/licenses, aligns and signs the APK, then verifies its signature. It uses the local `build-android/apk/debug.keystore`, generating one only when absent.

The existing device installation uses the original Mac's local development key. That key is excluded from GitHub. A different key will not update the installed app with `install -r`; preserve data and reuse the local key. Do not uninstall an existing data-bearing prototype without a backup and user agreement.

## Install and provide the user's original assets

```sh
adb devices -l
export RUNE_DEVICE=99cc517a
adb -s "$RUNE_DEVICE" install -r --no-incremental build-android/apk/open-rune.apk
```

The device may need to be unlocked to accept the ColorOS installation prompt. The first wireless/incremental installation waited indefinitely; USB plus `--no-incremental` worked. Do not disable global installation verification settings.

The app needs the user's game folder in its own private `files/game` directory. On the current OPPO this import is **already complete**. `import_game.py` performs the same root-assisted copy for a fresh installation and refuses to replace an existing copy:

```sh
python3 Android/import_game.py --device "$RUNE_DEVICE" \
  --source '/data/user/0/com.droiddeck.launcher/files/linuxfs/root/.local/share/Steam/steamapps/common/Rune Classic'
```

It reads the source and writes only the Open Rune data directory. It does not delete/reconfigure DroidDeck or alter original saves. A future user-facing asset picker is not implemented.

## Skeletal animation interpolation

Rune character/monster poses, upper-body animation proxies, transitions and
attached weapons now share fractional skeletal interpolation. Authored animation
rates, frame events and exact-frame combat sweeps are preserved. See
[implementation and checks](../Docs/RunePort/ANIMATION-NEXT.md). For a diagnostic
comparison, force-stop and relaunch with `--ei animationSmooth 0` (legacy) or `1`
(default smoothing). This does not persist a preference or change frame pacing.

## Launch and inspect

Normal icon launch opens the Open Rune title menu. Continue/Load game selects a saved campaign, New campaign starts the original story, and Settings contains Video, Audio, Controls, Gameplay and Enhancements. The combat arena is under Settings → Gameplay → Developer tools. Explicit diagnostic map launches bypass the title.

Video and Enhancements share Resolution & scaling: Native/75%/50%, Hardware Scaling or Snapdragon GSR, and actual render/display dimensions. See [system scaling](../Docs/RunePort/SYSTEM-SCALING.md). Video settings retain the selected frame target and pacing, MSAA and visual effects. Fresh defaults are native resolution, 120 FPS pacing and 4x MSAA. Frame rate & refresh now separates the FPS target from display refresh, with 144/165 and custom values; see [timing controls](../Docs/RunePort/FRAME-RATE-REFRESH.md). This OPPO now uses the user-requested 120 Hz package rule; see the [120 Hz timing follow-up](../Docs/RunePort/REFRESH-120-PERFORMANCE.md). Touch arrows move, dragging the right half looks, and labeled buttons perform actions. Physical controller mappings are in [CONTROLLERS.md](../Docs/RunePort/CONTROLLERS.md); Y or L3 uses items. Select/View activates a charged weapon's [Rune Power](../Docs/RunePort/RUNE-POWER.md). Dive swims down and Jump swims up.

Bloom/color is controlled in Video and Enhancements. **Compare original effects** temporarily disables visual/audio enhancements without rewriting saved selections. The HUD's **engine FPS** counter is not a measurement of unique displayed frames. Use the presentation-timestamp tool for that. Diagnostic launches may override the initial visual mode using `--ez enhanced true` / `false`; see [visual comparison](../Docs/RunePort/VISUAL-COMPARISON.md) for historical measurements.

Force-stop **only Open Rune** before changing launch extras; otherwise Android may deliver the new Intent to the existing Activity without restarting the engine.

```sh
adb -s "$RUNE_DEVICE" shell am force-stop org.runeclassic.port
adb -s "$RUNE_DEVICE" shell am start -n org.runeclassic.port/.MainActivity \
  --ei frames 120 --es map DM-Hildir.run
adb -s "$RUNE_DEVICE" shell run-as org.runeclassic.port cat files/native-run.log
adb -s "$RUNE_DEVICE" exec-out run-as org.runeclassic.port cat files/frame.ppm > frame.ppm
```

A bounded run intentionally exits after the requested frames. Use `--ei frames 0` for interactive testing. Private paths:

- `files/native-run.log`: stdout/stderr (replaced each launch).
- `files/frame.ppm`: last bounded framebuffer capture.
- `files/game`: separate original asset copy and Open Rune configuration/saves.
- `files/config/SurrealEngine`: engine local configuration/log path.

Automated movement test:

```sh
adb -s "$RUNE_DEVICE" shell am force-stop org.runeclassic.port
adb -s "$RUNE_DEVICE" shell am start -n org.runeclassic.port/.MainActivity \
  --ei frames 240 --ei forward 120 --es map VillageRuin.run
```

Controlled attack test (test fixture, not an enemy):

```sh
adb -s "$RUNE_DEVICE" shell am force-stop org.runeclassic.port
adb -s "$RUNE_DEVICE" shell am start -n org.runeclassic.port/.MainActivity \
  --ei frames 240 --ei fire 60 --es target Stool --ez traceCombat true --es map DM-Hildir.run
```

Repeat without `--ei fire 60` for the no-attack control. Expected final `destroyed=1` for attack, `destroyed=0` for control. The fixture is only spawned when the `target` extra is supplied.

Frame-time test:

```sh
adb -s "$RUNE_DEVICE" shell am force-stop org.runeclassic.port
adb -s "$RUNE_DEVICE" shell am start -n org.runeclassic.port/.MainActivity \
  --ei frames 600 --ei profile 480 --ei forward 560 --es map DM-Hildir.run
```

`profile` collects the requested number of samples after 60 warm-up frames. Log markers: `RUNE_PROFILE`, `RUNE_RENDER_PROFILE`, `RUNE_VULKAN_PROFILE`. `fence_ms` is the remaining CPU wait when retiring a submission, not total GPU work; timestamp intervals are reported separately. Timing queries are enabled only when profiling.

Diagnostic extras:

| Extra | Meaning |
| --- | --- |
| `--ei seed 1` | Reproducible random seed; normal launch uses time-based randomness |
| `--ei overlap 0` | Restore synchronous CPU/GPU operation; Android default is 1 |
| `--ei mailbox 0 --ei vsync 1` | Force the legacy FIFO selection; Android mailbox preference defaults to 1 |
| `--ei fps 120` | Optional sleep-based engine cap; default 0 is uncapped; this cap missed display deadlines in testing |
| `--ef scale 0.5` | Half internal width/height; default 1.0 is full native resolution |
| `--ei images 3` | Requested swapchain image count, clamped 2–4; driver actual count can differ |
| `--ei refresh 0` | Disable the window/surface rate request for comparison |
| `--ei msaa 0/1/2` | Off/2x/4x; default remains 4x |
| `--ez reverseLandscape true` | Request reverse landscape; this OPPO ignored the request and retained rotation 90 |
| `--ez validation true` | Require the packaged Khronos validation layer and enable synchronization validation |

Use one change per comparison. Distinguish engine FPS, unique displayed frames, and the hardware refresh rate. To read presentation timestamps independently, see `../Tools/RuneValidation/README.md`.

For development validation, optionally pass `--validation-layer /path/to/arm64-v8a/libVkLayer_khronos_validation.so` to the APK packager. The tested layer is Khronos 1.4.363.0, downloaded from its official release and hash-checked. Omit that option for the release APK; no global Android debug-layer setting is needed.

## Desktop regressions

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target SurrealEngine -j6
build/SurrealEngine.app/Contents/MacOS/SurrealEngine \
  --rune-probe --probe-map=SailingShip.run /absolute/path/to/RuneClassic
```

On Linux the executable is `build/SurrealEngine`. The same diagnostic flags (`--direct`, `--close-menu`, `--url`, `--maxframes`, `--capture-frame`, `--probe-forward-frames`, `--probe-fire-frame`, `--probe-target`, `--trace-rune-combat`, `--profile-frames`, `--render-msaa`, `--render-vsync`, `--render-overlap`, `--render-mailbox`, `--maxfps`, `--probe-seed`, `--vulkan-validation`) can be passed directly to the desktop executable. Headless native Android probes additionally need the native shared libraries and PK3 together under `/data/local/tmp`, appropriate `LD_LIBRARY_PATH`, and root access when reading DroidDeck's private original asset directory; use retained `android-device-probe.log` unless new changes require another run.

### Upscaler and pacing diagnostics

`--ez upscale true` selects GSR at 75% dimensions; `false` selects Native. `--ei pacing 0` restores the old uncapped presentation path for comparisons. Pacing defaults to 1. Both flags are Activity launch extras. Do not combine pacing with the legacy `fps` sleep-cap extra. The UI reports battery temperature and engine FPS; use `Tools/RuneValidation/android_present.py` for actual presentation intervals.


### Enemy combat arena

Tap **New fight** at the top left to start or retry one goblin encounter. This reloads DM-Hildir with a fresh Ragnar and grants a Viking shield before the goblin spawns. Attack uses the hand axe; hold Block to raise the shield. Protection ends when the shield breaks. New fight discards the current arena encounter, not a saved campaign. The screen stays awake while the game is visible.

The same package/signing key and OPPO refresh entry are retained. No new refresh exception is required for this update.

Bounded diagnostic examples (with the app installed and game assets already imported):

```sh
python3 Tools/RuneValidation/android_combat.py --device "$RUNE_DEVICE" \
  --case attack --output /absolute/path/to/analysis/combat-attack
python3 Tools/RuneValidation/android_combat.py --device "$RUNE_DEVICE" \
  --case block --output /absolute/path/to/analysis/combat-block
```

Run one device test at a time. Other cases: `unblocked`, `reach`, `stool-control`, `stool-attack`. Logs, JSON results and a screenshot when fighting begins are saved beside the output prefix. These are frame-based gameplay fixtures, not fixed-time performance benchmarks. Real-time timing can change the outcome of the repeated-attack case.

Activity extras: `--es enemy GoblinWarrior`, `--ei enemyFrame 240`, `--es shield VikingShield`, `--ei fire 60`, `--ei fireHold 8`, `--ei fireRepeat 60`, `--ei block 200`, `--ei blockHold 1600`, `--ez checkReach true`. Shield grant occurs at frame 120; default enemy spawn is frame 30. Reachability assertions need an unobstructed seed-1 arena fixture. Corresponding native flags are `--probe-enemy`, `--probe-enemy-frame`, `--probe-shield`, `--probe-fire-frame`, `--probe-fire-hold`, `--probe-fire-repeat`, `--probe-block-frame`, `--probe-block-hold`, `--probe-check-reach`.

See `../Docs/RunePort/COMBAT.md` for exact results and remaining gaps.

### Camera, campaign and wider combat

Ragnar and carried equipment now follow the original near-camera fade request.
`--ei cameraFade 0` disables it for diagnostics; default is 1. `--ez cameraProbe true`
logs camera distance, fade, map, player state and physics without changing them.
The Vulkan frame-capture path uses RGBA and the internal render dimensions; use an
Android screenshot to inspect the final GSR presentation and native overlays.

**Use** picks up a landed weapon through the original use/animation path. **Throw**
uses the original throw input. **Campaign / saves** offers the original Medium
campaign opening, quick save (slot 9), quick load and checkpoint load (slot 10).
Saving follows the original guards: alive, single-player, outside scripted or
uninterrupted states. Loading/new campaign asks before discarding the current
unsaved session. Native UI requests are queued and consumed between game frames.

For reproducible tests without altering normal save slots:

```sh
python3 Tools/RuneValidation/android_campaign.py --device "$RUNE_DEVICE" \
  --output /absolute/path/to/analysis/campaign --cases save-flow save-resume
python3 Tools/RuneValidation/android_campaign.py --device "$RUNE_DEVICE" \
  --output /absolute/path/to/analysis/campaign --cases limb-head limb-left limb-right throw drop dark-viking
python3 Tools/RuneValidation/android_stage_fps.py --device "$RUNE_DEVICE" \
  --stage combat --output /absolute/path/to/analysis/fps-combat-native
```

Other FPS stages: `village`, `underwater`, `transitions`. Native resolution is the
default. Combat replenishes health/durability only in the opt-in endurance fixture
so the sample stays active; it is a synthetic workload. The user prioritizes FPS
across stages, not extended heat comparisons. These tools do not change refresh
policy and leave the game visible after a presentation measurement.

`--es saveTest NAME` puts fixture saves and `.mapname` metadata in the app's
`files/validation-saves/NAME`. The equivalent native option is
`--save-directory=/absolute/path`. `--ez saveFlow true` exercises repeated saves,
a moving-state roundtrip and two original exits; `--ei resumeSlot 90` checks a fresh
process resume. `--es weaponProbe throw/drop`, `--ei limbJoint 9/15/20`, and
`--ez endurance true` are opt-in fixtures. Do not enable fixtures during normal play.

Read `../Docs/RunePort/CAMPAIGN.md` for tested coverage and remaining limits.

### Use button pickup

Stand near the item, stop moving and tap **Use** after the current animation finishes. The Android Left Control event now falls back to Rune's legacy Ctrl binding when Left Control is unassigned; Block's separate Right Control binding is preserved. Food follows the original health rules. The end-to-end regression is `Tools/RuneValidation/android_touch_pickup.py`; it taps real Android buttons and checks both axe recovery and food consumption.

### Renderer recovery and diagnostics

Android gameplay omits the unused Vulkan selection output by default; actual
selection frames retain their original path. Native resolution and 4x MSAA are
unchanged. HOME/resume now recreates a lost Android surface without restarting
the game; three validation-enabled cycles passed. Read RENDERER-FIXES.md under
Docs/RunePort for evidence, performance caveats and fixture instructions.
`--ei omitHit 0` restores the original output for diagnostics. In-pass color
resolve (`--ei inpassResolve 1`) and unused-store discard (`--ei discardHit 1`)
remain opt-in experiments. All three options require a fresh Activity launch.

## Gib physics diagnostic

`Tools/RuneValidation/android_gibs.py --device DEVICE --output OUTPUT` runs 24
controlled timestep cases using real world collision and original gib/limb
scripts. It requires an unlocked tablet and uses isolated saves. Physics behavior
is unchanged; the opt-in Intent is `--ez gibProbe true`. See
[GIB-PHYSICS.md](../Docs/RunePort/GIB-PHYSICS.md) for results and scope.

### Fixed-camera renderer comparison

`--es probeView 'x,y,z,yaw,pitch'` overrides the rendered camera only. It requires
`--es saveTest NAME`, leaves simulation running, and does not move the player or
establish campaign traversal. Values must be finite; angles use Unreal units.
For the first water wheel: map `Dwarf1wwheel.run`, view
`-1450,-2000,-650,52200,-3500`, native (`upscale=false`), `msaa=2`, seed 1,
worldFeatures 6055 (water off) or 8103 (on). No normal-launch camera behavior is
changed. This avoids navigation when repeating renderer comparisons.


Network performance diagnostics: `profile` also measures the interactive client,
with separate network/presentation/render timings and a 60-playing-frame warm-up.
`probeView` applies to both campaign and network clients with isolated saves.
`groupShadows` (0/1, default 0) compares the original shadow-atlas passes with a
single grouped pass. Pixel tests pass, but the measured arena view showed no
worthwhile speedup, so the experiment is not a shipping default. See
[the network performance audit](../Docs/RunePort/NETWORK-INTERACTIVE.md#follow-up-performance-audit-october-26).


### Native 4x renderer cost comparison

Opaque early-depth shaders and relaxed bounded lighting arithmetic are enabled.
`--ei opaqueEarlyDepth 0 --ei fullPrecisionLighting 1` selects the reference path
for one launch; no saved user setting changes. `--ei probeTickHz 120` advances
simulation by a fixed step for image comparison only and requires both `frames`
and `saveTest`. Normal gameplay and network timing remain real-time.

`Tools/RuneValidation/android_render_cost.py --device DEVICE --output NEW_DIRECTORY`
compares hall, water and combat at the OPPO's native 2520x1680, 4x MSAA. Its default
fixed-step images and engine throughput are separate from actual display cadence;
add `--real-time` to measure SurfaceFlinger. The device must stay unlocked and
untouched; each case restarts the app and uses isolated saves. See
[measurements and limitations](../Docs/RunePort/RENDER-LIGHTING-PERFORMANCE.md).

Gameplay typography: [readable fonts](../Docs/RunePort/READABLE-FONTS.md) covers the shared menu-body face for HUD/touch text and cached Noto Sans native canvas fonts.
