# Open Rune

Release status: **unreleased v1**. Both development repositories are private. Public source publication is deferred until the owner is ready to release v1. This checkout retains private original-game research; its history must remain private.

<img src="Android/res/drawable-nodpi/open_rune_mark.png" alt="Open Rune O/R emblem" width="160">

**Open Rune** is a native ARM64 Android adaptation of SurrealEngine for Rune Classic 1.11. It runs the original game data and compiled UnrealScript in an extended native engine, with modern controls, optional visual enhancements, new combat mechanics and experimental private multiplayer. It remains a private development fork; campaign coverage and human multiplayer playtesting are incomplete.

**Read the [project overview and development history](PROJECT_OVERVIEW.md)** for how the port was built, what comes from the original game and SurrealEngine, what this fork adds, and what remains unfinished.

For development, start with [CLAUDE.md](CLAUDE.md). It contains checkpoint links, build/deploy guidance, known failures, evidence locations and next priorities.

- [Open Rune branding and logo](Docs/Brand/README.md)
- [Build and device testing](Android/README.md)
- [Private multiplayer coverage and limits](Docs/RunePort/PRIVATE-MULTIPLAYER-STATUS.md)
- [Laptop hosting and joining](Tools/RuneMultiplayer/README.md)
- [Tactical enemy behavior](Docs/RunePort/TACTICAL-AI.md)
- [WIP texture pipeline](Docs/RunePort/TEXTURE-PIPELINE.md)
- [Adaptive sharpening, filtering and shadow controls](Docs/RunePort/CLARITY.md)
- [Investigation and measured results](Docs/RunePort/INVESTIGATION.md)
- [Native evidence and runtime logs](Docs/RunePort/evidence/)
- [Recovery tools and recovered UnrealScript](Tools/RuneRecovery/README.md)
- [Upstream project README](Docs/Upstream-README.md)

The primary tested device is the OPPO Pad Mini OPD2515. Native Vulkan rendering at 2520×1680 with 4× MSAA is a validation baseline; fresh Android settings now default to 50% Hardware Scaling. Original artwork and optional WIP replacements can be switched in Video and Enhancements. See [scaling behavior](Docs/RunePort/SYSTEM-SCALING.md).

Selected encounters, natural opening transitions and save/load paths have bounded validation. Laptop automation covers 44 multiplayer maps and 157 supported map/variant combinations, including 16-player match loops; human fairness, audio and endurance playtesting remain. The installed Android build predates multiplayer protocol 17 and cannot join the new host until rebuilt. Performance depends on scene, effects, scaling and device conditions; a 120/144 FPS target is not a sustained-FPS guarantee. See [campaign coverage](Docs/RunePort/CAMPAIGN-ROUTE.md), [multiplayer evidence](Docs/RunePort/PRIVATE-MULTIPLAYER-STATUS.md) and [measured renderer work](Docs/RunePort/RENDER-LIGHTING-PERFORMANCE.md).

## Source and assets

The engine source retains upstream history from [SurrealEngine](https://github.com/dpjudas/SurrealEngine), based on commit `a041728695b929fb51b019f8387bb42cc19cbf19`. SDL2 is an external pinned dependency, documented in the build guide. See [LICENSE.md](LICENSE.md) and [Rune model attribution](Docs/RuneModel-THIRD-PARTY.txt).

The full original game installation, saves, personal settings, local toolchains and signing key are excluded. You need your own installed Rune Classic 1.11 assets. Recovered UnrealScript and native pseudocode are private research material from the user's installation, not a newly licensed game-source release. Keep this repository private. Build `open-rune.apk` with the Android guide. Earlier private prereleases and diagnostic archives retain their historical artifact names.

Upstream requests that AI-authored changes stay in a fork: see [NO-AI Code Rule.md](NO-AI%20Code%20Rule.md). Do not submit this work upstream.

The inherited desktop CI workflow is manual-only in this repository; pushing the handoff does not launch a multi-platform build matrix.
