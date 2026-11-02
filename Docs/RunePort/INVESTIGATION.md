# Open Rune — Rune Classic recovery and Android port investigation

Updated: October 24, 2026. This investigation includes native macOS and Android ARM64 builds using the files copied from DroidDeck on the OPPO Pad Mini. Native Android package and animation probes pass on the device. Android Vulkan rendering and automated walking are now verified. Touch controls and complete gameplay remain under test.

## Findings

Native decompilation works using the Mac's existing Ghidra 12.1.4 and Java 21. REA was run with task-local environment variables; no Codex integration configuration was changed. The device installation, saves, and DroidDeck settings were not modified.

The executable's SHA-1 is `b644bb7f6ce07e284fb81e1ed496607f77080771`. This exactly matches Rune Classic **1.11** in SurrealEngine's game database at commit `a041728695b929fb51b019f8387bb42cc19cbf19`. The game uses 32-bit x86 Windows binaries. They cannot execute as native ARM64 Android libraries.

The most useful discovery is that the shipped Unreal packages preserve original UnrealScript text. Recovery produced:

| Item | Verified result |
|---|---:|
| Embedded source text entries extracted across all copied packages | 3,015 |
| Distinct source text contents, by SHA-256 | 1,265 |
| Source texts in the main RuneI.u gameplay package | 725 |
| Source texts in the main Engine.u package | 107 |
| Total recovered text bytes | 8,316,470 |

The 3,015 total includes compatibility-version duplicates such as RuneI101.u and RuneI107.u. It does not mean 3,015 unique gameplay classes. Ragnar, RunePlayer, weapons, enemies, dismemberment behavior, menus, and import directives are available to read directly.

Each recovered text was obtained by parsing the package's name/import/export tables and its TextBuffer serialization, with object bounds, text length, terminator, and source-object ownership checks. The archive includes a per-file hash and package-offset manifest. All extracted files were reread and checked against that manifest.

**Recovery limitations:** embedded text has not been compared against compiled bytecode for behavioral parity. The text generally omits compiled class default properties; the original package data remains necessary. Native C++ implementation, original model authoring files, and a complete rebuildable game project were not recovered by extracting text.

## Native engine boundary

The copied files include the full System folder and the players.ums model package. Key local binary hashes matched fresh readbacks from the device:

| File | SHA-256 |
|---|---|
| Rune.exe | b040909d3c5d7923852c9b04540d1f66afef3f0bad13041569292ca895f08e51 |
| Engine.dll | 41458bac7e717a57effbe45d9351664ab79d72d7b821c3103018d057b7c585d0 |
| RuneI.dll | d8799cf0c5e44951f3aa36a743ae31d2f6d2de525958c445015a44e79a358ccd |
| players.ums | efc7e2ae001ac6c267c64d7710ecca1b0f81e5344d3a19568fd93a5ea9c9d770 |

The PE export tables retain useful C++ names: Engine.dll has 2,635 named/ordinal exports, RuneI.dll has 78, and Core.dll has 1,986. Export counts are not counts of implemented gameplay functions; they include constructors, data and compiler machinery. Ghidra identified 1,013 procedures in RuneI.dll under its default analysis profile.

Rune's central character resource is **USkelModel**, with named routines for serialization, animation decompression, skeleton updates, vertex transformation and joint collision checks. This is distinct from the USkeletalMesh class found in SurrealEngine.

The players.ums package parses successfully as Unreal package version 69. It contains one SkelModel named Ragnar, plus 163 Texture objects and 161 Palette objects. Ragnar's serialized SkelModel object is 7,763,867 bytes at package offset 2,638,114. A standalone reader now consumes the full Ragnar object exactly. It validates 25 mesh variants, 38 joints, 612 animation sequences and 15,594 frame-table entries. Triangle and weighted-joint indices pass range checks, and every compressed animation stream is consumed exactly. Mesh variant 0 has 659 vertices and 1,319 triangles.

The reader also produces a 12-frame untextured preview of the shipped walkforward7 sequence, plus a base-pose OBJ. The GIF was decoded back and four frames visually inspected. This validates a standalone diagnostic rendering path, not the original renderer or gameplay. Textures, material visibility flags, game-side animation blending, physics and runtime parity remain unverified. The preview uses only mesh variant 0.

## Upstream SurrealEngine compatibility assessment (before our changes)

The inspected upstream commit detects Rune 1.11, but detection is not gameplay support. A source search found no SkelModel implementation or registrations for Rune's joint APIs. Its ordinary SkeletalMesh loader should not be assumed compatible with Rune's custom resource format.

The recovered Engine/RuneI source declares 31 native or intrinsic interfaces with indices 600–699. Thirty have no matching class/function registration in the inspected native source. The remaining name, Actor.TraceTexture, belongs to a different interface: Rune uses index 666 with a texture-returning signature, whereas SurrealEngine registers a ten-argument iterator at index 1000. Matching a name therefore does not establish compatibility.

Examples of required interfaces:

| Interface | Purpose indicated by recovered source |
|---|---|
| Actor.GetJointPos / GetJointRot | Animated joint transforms |
| Actor.AttachActorToJoint / DetachActorFromJoint | Weapons and other attachments |
| Actor.ApplyJointForce / SetJointRot | Joint motion and control |
| Actor.FrameSweep | Weapon sweep support |
| Pawn.SkeletonLook | Skeleton-based looking |
| RopePointTick / SpringPointTick and related calls | Native physics-related entry points |

This is a bounded interface comparison, not an exhaustive port-work estimate. Existing UE1 calls also require signature, index and behavior checks. For example, Rune declares SweepActors at index 313, which also has another meaning in some UE1 versions.

## Evidence and decompilation

REA retained JSON evidence and Ghidra-generated pseudocode. Pseudocode is an analysis result, not original, directly compilable C++.

| Question | Result | Evidence |
|---|---|---|
| Can Ghidra analyze the shipped native gameplay library? | Yes | `ev_2cbf15777ce510b12ce6adb7f614ce4fb49e8db983e5d3cef417f904e035cc2d` |
| Are native RopePoint symbols identifiable? | Yes | `ev_8533b0a46a460f4558693dc7274cd0207e3bf4a83df951bbf9a4d567d0fe950b` |
| Can a named native function be decompiled? | Yes: RopePointTick | `ev_621a945e39510c380d89ddbada3ca59d77dc025403502fa91075b1f8212b43a4` |
| Can the custom model serializer be recovered? | Top-level sequence and array helpers recovered; model reader validated against Ragnar | `ev_f1b299bb1ebb695ff453426f0e19c73a9b4e43aba3ab29d467b7aee775a7df90` |

The selected RopePointTick implementation consumes its argument without visible physics calculations. This illustrates why names and old source comments alone are insufficient to reconstruct behavior. It is not a runtime test of rope behavior.

## Next implementation milestone

1. **Completed for Ragnar:** decode SkelModel, validate array counts and final object boundary, and consume every compressed animation stream.
2. **Completed:** recover a reference pose, render an animation in a standalone viewer, and integrate Rune model loading and CPU skinning into the native engine. Native and standalone base-pose vertices agree within approximately 0.000014 units.
3. **Partially completed:** joint APIs, attachment following, movement and a controlled stool attack work. An enemy encounter remains unverified.
4. **Partially completed:** Android ARM64 builds and is installed. Vulkan rendering, walking, controlled stool attack, touch attack, movement and drag look are observed on the OPPO. Audio initialization passes; audible output, controller input, enemy encounters and broader lifecycle behavior remain unverified.

Saving, level transitions, full campaign behavior and multiplayer remain separate validation work. Short Android frame-time measurements are recorded below; sustained campaign performance and latency remain unverified.

## Sources inspected

- Your device's installed Rune Classic files, copied read-only.
- [UE Viewer Rune model reader](https://github.com/gildor2/UEViewer/blob/a0bfb468d42be831b126632fd8a0ae6b3614f981/Unreal/GameSpecific/UnMeshRune.cpp), used as a format/conversion reference and checked against native serializer and animation-decoder evidence. Its MIT license and attribution accompany the recovery tooling. The animation decoder uses flag-controlled signed 16-bit components; position and scale use a factor of 1/256, consistent with the recovered native DecodeFrame routine.
- [SurrealEngine source at the inspected commit](https://github.com/dpjudas/SurrealEngine/tree/a041728695b929fb51b019f8387bb42cc19cbf19), particularly GameFolder.cpp, UE1GameDatabase.h, Package.cpp, UTextBuffer.cpp, PackageManager.cpp, NActor.cpp, USkeletalMesh.cpp and Docs/Status.md.

## Current completion boundary

File access, script recovery, native decompilation, validated model/animation decoding, and a native macOS ARM64 prototype are complete. The prototype loads and renders RagnarVillage and VillageRuin. VillageRuin logs in SarkRagnar, accepts normal W input in PlayerWalking state, moves approximately 470 units horizontally, lands, and completes a 240-frame bounded run without script errors or missing-native failures. The opening RagnarVillage cinematic correctly retains the player in Scripting state. A controlled desktop axe attack destroys a spawned stool while its no-attack control survives. This is not an enemy encounter. A native Android ARM64 executable passes the package, animation and geometry probes directly on the OPPO. A signed prototype APK is installed and completes a 120-frame DM-Hildir rendering test on the Adreno 829. VillageRuin walking moves approximately 470 horizontal units on Android. Touch controls, playable enemy encounters and full runtime parity remain under test.


Additional native evidence: DecodeFrame `ev_ced7d457b8ce5817534ea0c096130670eecfe07d1d25efbdee4df446cc5651bc`; FrameSweep `ev_7a86d4c67b7bc9d2073a6dc3c589067dd473b69676b1d492a76c87a0858e643c`. The recovery archive retains the serializer-array helper evidence as well.


## Native prototype implementation and verification — October 24

The local SurrealEngine adaptation builds successfully as a macOS ARM64 application. It loads Rune SkelModel resources, decodes animation data, skins vertices, and renders Rune mesh actors alongside level geometry. The original game packages and compiled UnrealScript supply gameplay and defaults; this is an engine adaptation, not a rebuild from recovered text alone.

Implemented native interfaces include joint lookup/counts, default joint/polygroup flags, joint positions, attachment metadata, animation-cache initialization, additive joint rotations, SkeletonLook, Rune Canvas line calls, and Rune TraceTexture surface queries. The joint rotation and look implementations were checked against retained Engine.dll pseudocode and recovered Pawn source. Attachment following, detachment and an initial collision sweep implementation are now present. Force/IK behavior, animation blending fidelity and other Rune-specific natives still require work; exact native parity is not established.

Compatibility fixes cover grouped menu texture lookup, uppercase package extensions, VM struct temporary storage, serialized struct arrays, UTF-16 configuration reading, and Rune's boolean decal return signature. Missing native callbacks now fail with a named function and script stack rather than disappearing into an empty callback exception.

Verified checks:

- Headless SailingShip loading: 1,904 actors; RagnarVillage loading previously passed with 2,023 actors.
- All 612 Ragnar animation streams validate; 25 mesh variants and 38 joints parse within the exact object boundary.
- Regression probes cover struct storage independence, grouped texture loading, native joint calls, additive pose deformation, SkeletonLook rate/guard behavior, and the actual UTF-16 User.ini name/key binding.
- RagnarVillage runtime: Entry and level initialization, player login, closed menu, rendering and clean shutdown after 240 frames. No script errors remained in the run after the Unicode/decal fixes. Graphics-driver blend warnings and a few unrecognized menu console commands remain.
- Normal W input resolves to MoveForward and creates an active input axis. In RagnarVillage, the player's Scripting state ignores walking as defined by recovered RunePlayer source; observed motion there is only settling onto the floor.
- VillageRuin reaches PlayerWalking. After implementing Actor.TraceTexture from native evidence, normal W input moved SarkRagnar from (-1661.296, -1631.387, 205.953) to (-1322.906, -1305.736, 128.000): about 470 horizontal units and 476 units total including the landing. The 240-frame run completed with no script errors or unknown-native failure. The captured frame shows the textured player, level and HUD. This is a movement smoke test, not an enemy encounter or full gameplay test.
- TraceTexture evidence: ev_fa8cfcc76de48c32cc3b220f62a84319f3717dfeb9d9adf80e46e772a54a262d, ev_b8c3b71f4dea64e6cf33742d6ae861fc4f9bacdd5e91903e60b2671bb44d285c and ev_11601cfa30ac8d72aedc8b7264a05466fd1954b8eb672e845ebd231680a0f2e6. Surface lookup was reconstructed using the existing engine polygon ray tracer; edge-case parity has not been established.

The local patch is supplied separately as `rune-native-engine.patch`, based on upstream commit a041728695b929fb51b019f8387bb42cc19cbf19. The upstream repository requests that AI-authored changes stay in a fork; no upstream PR has been opened.

The Android platform layer now uses SDL2 2.32.10 and NDK 27 to build ARM64 engine code and a separate prototype application, org.runeclassic.port. On-device headless tests pass, including Ragnar's 612 sequences and SailingShip's 1,904 actors. The signed APK is installed and rendered a captured 1024 by 768 frame of DM-Hildir with Ragnar, textures and HUD. Short DM-Hildir performance measurements are recorded below. The prototype does not bundle the original game assets. DroidDeck remains separate.


## Combat and attachments continuation — October 24

The multiplayer map DM-Hildir supplies a hand axe through the original game startup path. The first attack probe reached RunePlayerProxy.Attacking but had no animation and never entered weapon damage checking. Native execPlayAnim/execLoopAnim explicitly resolve an AnimationProxy's sequence from its owner's SkelModel. That resolution is now implemented and verified by a regression probe and runtime attack sequences such as X1_StandingAttackAreturn.

Native GetSkeleton selects joint group 0 from the main actor and group 1 from AnimProxy. The adapter now follows that split. A probe verifies that an upper-body animation changes upper-body joints while preserving lower-body joints. Fractional proxy pose interpolation, tween continuity, dynamic joint physics and exact native pose parity remain incomplete.

Native Actor.Tick emits FrameNotify events as animation frames are crossed. These callbacks are now dispatched, enabling the weapon's FrameNotify handler. FrameSweep now samples the parent attack skeleton at the requested integer frame and emits FrameSwept with the previous and new blade segments. The game now reaches SweepActors, which was previously never exercised because the proxy animation and frame callbacks were missing. The initial SweepActors iterator now clips against animated joint boxes where available, with capsule fallback and world occlusion. In a controlled desktop test, the stool survives without an attack and is destroyed after normal attack input; logs record the sweep hit. That target used capsule fallback, so this does not verify joint-hit masks against an enemy, dismemberment, or exact native sampling parity.

Attachment positions now follow the parent joint during ticking; attached mesh orientation uses the parent joint transform in rendering and joint position queries. This is based on TickSkeleton and EnsureLatestCoords evidence. Headless checks verify that attachment transform origins agree with GetJointPos and follow parent translation. A 120-frame DM-Hildir run with the hand axe attached completes without script errors; visual inspection confirms the axe appears beside the player's hand. Dropping/throwing and exact original runtime orientation parity remain unverified.

Additional retained evidence:

- execPlayAnim: ev_b5d4beabdfb7dc4b6212e06c4541c7d8cdfe8b9eec3008ed910c08f6c4870fb5
- execLoopAnim: ev_039383599ec24e4fdf21affc944129bc32c65295e2f98e039096e69aa2d49c18
- EnsureLatestCoords: ev_dead4e8aca49359709869358249e88073545cf40d491c0a5bc98adf48dc23897
- GetSkeleton: ev_72202f5a4ef2d9f230027c6ef94a29150ee29d65eecb560430a524aacabd7c11
- Actor.Tick: ev_3e35b15b15a3bbb7a2bc76c0d83fcc7e35df8700d1c04cae81269d1daf82ee41
- GetFrame: ev_ce14047faee03fc07128fd7d56e156550e47d18f602dc135bb1469e53b3875dd
- FrameSweep body: ev_7e25dfba9939fa77598b9402efec9a587ec4320a117d027356adb76238d61977
- TickSkeleton: ev_a3301960eb2abb58e187f0f0625e73e5c4a774b193fdcc159027d29f999f52d3
- SweepActors: ev_b2d2483ff2347c2aea99eae93cb0cd789ebab5cef654b2276132f4e136a8d153

## Native Android bring-up — October 24

The ARM64 Android executable ran directly on OPD2515, with no Wine or PC emulation. Its probe exited successfully with Unicode configuration, joint collision geometry, VM struct handling, grouped texture loading, AnimationProxy ownership, layered poses, attachment transforms, look/pose and native binding checks. It parsed all 25 Ragnar mesh variants, 38 joints, 612 sequences and 15,594 frame entries, and loaded SailingShip with 1,904 actors. This was a headless test; it does not establish GPU rendering or playable gameplay.

The APK contains the native engine and its SDL2/OpenAL/video dependencies. Installation completed over USB after the tablet was unlocked. Original game data was copied into the prototype's private directory. A 120-frame DM-Hildir Vulkan run completed cleanly, and its captured frame was visually inspected. A 240-frame VillageRuin run accepted normal walking input and moved SarkRagnar approximately 470 horizontal units. Touch movement, jump, attack, block and menu buttons plus right-side drag look are implemented; validation is ongoing. A relaunch failure from process-wide native registries was found and addressed by ending only the prototype process after SDL joins its engine thread on Activity destruction. Campaign progression, saving, audio, lifecycle, enemy joint collisions and performance remain open validation work.

Original DroidDeck configuration and save-file SHA-256 manifests match before and after the initial Android rendering and walking tests. Audio initialization succeeds, but audible output has not been independently verified.

## Installed Android prototype and control validation

`open-rune.apk` is installed as Open Rune, package `org.runeclassic.port`. The original assets are in its private data directory, separate from DroidDeck. The APK alone does not include those original game files. Launching the icon starts DM-Hildir with an axe and basic touch controls, without a frame limit.

On Android, the no-attack control completed 240 frames with the spawned stool intact. The attack run completed 240 frames with the stool destroyed and the axe returned to Active state. The hit used capsule fallback, not enemy joint collision. A separate physical-coordinate touchscreen press on the Attack button emitted a sweep hit on the stool; before/after screen captures show its removal. Touch movement and right-side drag look produced a visibly changed player/view position. Jump, block, menu behavior and simultaneous multi-touch remain unverified.

The relaunch fix passed successive control, attack and interactive launches without the duplicate-native-registration error. The prototype currently exits its own process after SDL joins the engine thread on Activity destruction; background/resume and interruption behavior still need broader tests. A landscape orientation override is present, but the device's portrait capture shows the game and controls at the top with unused black space below. Display sizing/orientation remains a known prototype issue. Short engine frame-time measurements are recorded below; end-to-end latency has not been measured.

The app is left running in DM-Hildir with the temporary stool fixture removed by starting a fresh session. The source patch includes Android platform and packaging changes; the APK is signed with a local development key.

## Initial frame-rate investigation

The installed Android build uses compiler optimization (-O2); it was not an unoptimized debug build. Added opt-in benchmark timing separates game tick work from scene rendering and renderer submission/presentation waits. Each walking trial ran 600 frames, discarded 60 startup frames and collected the next 480 frames in DM-Hildir. The player moves forward and eventually meets level geometry, so these are short reproducible smoke tests, not representative campaign benchmarks. The reported FPS derives from engine wall-clock frame sections, not independently counted displayed frames or GPU timestamp queries.

| Trial | Engine FPS | Mean frame ms | p95 frame ms |
|---|---:|---:|---:|
| Existing refresh behavior, 4x MSAA | 60.05 | 16.65 | 18.84 |
| Per-app 120 Hz request, 4x MSAA | 68.31 | 14.64 | 18.55 |
| Per-app 120 Hz request, antialiasing off | 69.14 | 14.46 | 18.17 |
| Existing refresh behavior repeated, 4x MSAA | 60.00 | 16.67 | 19.22 |
| Installed final default, 120 Hz request and 4x MSAA | 68.79 | 14.54 | 16.93 |

The APK now requests 120 Hz for its own window and native surface. System-wide display settings were not changed. The final trial averages about 15% more engine frames than the repeated baseline, while retaining 4x MSAA because disabling it barely helped. This is a short observed improvement, not a sustained-performance guarantee. In the final trial, tick work averaged 2.00 ms and rendering including submission/presentation waits averaged 12.54 ms. The latter includes 5.75 ms before renderer unlock and 6.78 ms in unlock. These CPU wall-clock sections do not by themselves distinguish GPU execution from synchronization or display waits.

The updated APK was installed; the desktop build and SailingShip regression probe also pass. Remaining work includes finer renderer profiling, frame pacing and animation smoothness, sustained measurements and testing heavier scenes. No claim of steady 120 FPS is supported.


## October 24: native-resolution display, renderer overlap and actual 120 Hz presentation

See [the performance validation report](PERFORMANCE.md) for the current implementation and evidence. It supersedes earlier display/FPS conclusions: the original Android drawable was incorrectly 1024x768, and an Android-reported 120 Hz mode did not prove the hardware was refreshing at 120 Hz. The updated native-resolution build plus the Rune-only refresh rule measured 120.0028 displayed FPS over 37.6408 seconds with 4x MSAA. The 6000-frame engine sample averaged 174.97 FPS. Full campaign, enemy combat and long thermal behavior remain unverified.
