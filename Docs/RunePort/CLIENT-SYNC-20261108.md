# Client consolidation — November 8, 2026

Release identity: **0.1.0-dev.7 / build 7**. Network protocol/ALPN **25**, gameplay rules **34**. Matching client/host builds remain required. Android, Windows and macOS are rebuilt from the Windows primary source; no Unity runtime or bridge is included.

## Included source

- All retained work since the November 3 checkpoint, including Laws of Battle, kick/rush/leap behavior, stair and combat presentation, cinematic physics/recovery, map look-focus optimization, canvas-alpha fades and the Windows custom water candidate. Giant leap remains disabled for everyone in campaign gameplay.
- Android’s [circular touch controls](TOUCH-CONTROLS.md), with previous layout recovery and the existing move/resize/opacity editor, controller hiding and camera-centre double-tap jump.
- The multiplayer runtime feature mask now preserves material lighting and environment probes.
- Shield-bash impact/pose evaluation safely skips previews without an active level. This fixes the package/model-probe crash found during the Unity experiment.

## Platform choices

The previously qualified shader specialization and in-pass resolve remain enabled for Mac and the tested Qualcomm Adreno 829 configuration. Windows keeps its previous defaults because the measured gain did not meet the retention threshold. Other GPUs/Turnip keep their previous defaults. The optional Turnip payload is preserved. No resolution, AA or visual-quality reduction is introduced.

The new custom water path remains Windows-default only: it has not received equivalent Mac/Android qualification. Other platforms include the shared source and retain their prior default water path. This is not a claim that every optional feature now has equal platform validation.

## Validation and deployment

Windows application and ARM64 Android builds pass; the macOS ARM64 application builds and is installed. Windows 15 network tests, discovery and production-texture tests, physics checks, plus native package/model and combat/playback fixtures pass. Mac package/model and native combat/playback fixtures pass. Android is signed with the original certificate and installed in place; installed bytes are verified. Normal saves/settings are unchanged at installation; after the UI checks, only the requested touch-layout preferences changed. Normal graphics settings are retained. No multiplayer host is left running.

Full evidence, rollback APK/app backups and distribution hashes are kept outside Git under `Consolidation-20261108` on the Mac and `C:/OpenRune/outputs/consolidation-20261108` on Windows. These bounded checks do not certify all-map gameplay, public networking, sustained FPS or complete campaign coverage.

## Unity archive

The Unity engine migration is shelved. Preserve its source, candidates, reports and comparisons under `C:/OpenRune/archive/unity-20261108`; do not merge Unity-only bridge/caching/render paths or the rejected skinning-worker prototype into Surreal. Its standalone tablet test apps and game-launch shortcuts are removed after archival verification. Only the production Surreal package remains installed for Open Rune testing.

Both GitHub repositories remain private and unreleased. The research repository retains existing private history. The clean distribution-source repository excludes recovered game scripts, original assets, analysis evidence, private runtime credentials, generated artwork, build products and signing keys. No upstream contribution or public release is made.
