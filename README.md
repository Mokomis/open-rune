Current build: **0.1.0-dev.7**. See the [November 8 client consolidation](Docs/RunePort/CLIENT-SYNC-20261108.md) for deployment, tests and platform limitations. Surreal is the active engine; the Unity experiment is archived.

# Open Rune

Release status: **unreleased v1**. Both development repositories are private. Public source publication is deferred until the owner is ready to release v1. This is the clean distribution-source snapshot. Original-game research, recovered scripts and upstream history are excluded. Keeping this repository private is a release-stage decision.

<img src="Android/res/drawable-nodpi/open_rune_mark.png" alt="Open Rune O/R emblem" width="160">

**Open Rune** is a native adaptation of SurrealEngine for Rune Classic 1.11, with Android ARM64, Windows x64 and macOS ARM64 clients. It runs the original game data and compiled UnrealScript in an extended native engine, with modern controls, optional visual enhancements, new combat mechanics and experimental private multiplayer. It remains a private development fork; campaign coverage and human multiplayer playtesting are incomplete.

**Read the [project overview and development history](PROJECT_OVERVIEW.md)** for how the port was built, what comes from the original game and SurrealEngine, what this fork adds, and what remains unfinished.

For builds and current deployment scope, start with [Android build guidance](Android/README.md), [Windows build guidance](Windows/README.txt) and the [client deployment checkpoint](Docs/RunePort/CLIENT-SYNC-20261103.md). Original game files are required.

- [Open Rune branding and logo](Docs/Brand/README.md)
- [Build and device testing](Android/README.md)
- [Private multiplayer coverage and limits](Docs/RunePort/PRIVATE-MULTIPLAYER-STATUS.md)
- [Laptop hosting and joining](Tools/RuneMultiplayer/README.md)
- [Tactical enemy behavior](Docs/RunePort/TACTICAL-AI.md)
- [WIP texture pipeline](Docs/RunePort/TEXTURE-PIPELINE.md)
- [Adaptive sharpening, filtering and shadow controls](Docs/RunePort/CLARITY.md)
- [Investigation and measured results](Docs/RunePort/INVESTIGATION.md)
- [Upstream project README](Docs/Upstream-README.md)

The primary tested device is the OPPO Pad Mini OPD2515. Native Vulkan rendering at 2520×1680 with 4× MSAA is a validation baseline; fresh Android settings now default to 50% Hardware Scaling. Original artwork and optional WIP replacements can be switched in Video. See [scaling behavior](Docs/RunePort/SYSTEM-SCALING.md).

Selected encounters, natural opening transitions and save/load paths have bounded validation. Laptop automation covers 44 multiplayer maps and 157 supported map/variant combinations, including 16-player match loops; human fairness, audio and endurance playtesting remain. The November 8 dev.7 Mac, Windows and Android builds share multiplayer protocol/ALPN 25 and native rules 34; peers must use matching builds. See the [client deployment checkpoint](Docs/RunePort/CLIENT-SYNC-20261103.md) for scope and verification. Performance depends on scene, effects, scaling and device conditions; a 120/144 FPS target is not a sustained-FPS guarantee. See [campaign coverage](Docs/RunePort/CAMPAIGN-ROUTE.md), [multiplayer evidence](Docs/RunePort/PRIVATE-MULTIPLAYER-STATUS.md) and [measured renderer work](Docs/RunePort/RENDER-LIGHTING-PERFORMANCE.md).

## Source and assets

The engine derives from [SurrealEngine](https://github.com/dpjudas/SurrealEngine), based on commit `a041728695b929fb51b019f8387bb42cc19cbf19`. SDL2 is an external pinned dependency, documented in the build guide. See [LICENSE.md](LICENSE.md) and [Rune model attribution](Docs/RuneModel-THIRD-PARTY.txt).

The full original game installation, saves, personal settings, local toolchains and signing key are excluded. You need your own installed Rune Classic 1.11 assets. Original-game binaries, recovered UnrealScript and native pseudocode are excluded from this source snapshot. It does not grant rights to distribute Rune assets. Keep this repository private until the owner releases v1. Build `open-rune.apk` with the Android guide. Earlier private prereleases and diagnostic archives retain their historical artifact names.

Upstream requests that AI-authored changes stay in a fork: see [NO-AI Code Rule.md](NO-AI%20Code%20Rule.md). Do not submit this work upstream.

The inherited desktop CI workflow is manual-only in this repository; pushing the handoff does not launch a multi-platform build matrix.
