# Open Rune

<img src="Android/res/drawable-nodpi/open_rune_mark.png" alt="Open Rune" width="160">

Open Rune is an independent native adaptation of [SurrealEngine](https://github.com/dpjudas/SurrealEngine) for Rune Classic. It uses the original game data and compiled UnrealScript through an extended engine; it is not a decompilation of the original executable into a complete C++ game project.

**You must provide your own lawfully obtained Rune Classic game files.** Original maps, models, textures, sounds, music and gameplay scripts are not distributed here. This public repository starts with a clean source snapshot; original-game recovery material and the private development history are excluded.

## Platforms and features

- Native ARM64 Android client and hosting, plus a native Windows client/host and Mac development builds. Linux and iOS are not validated releases.
- Themed menus, touch/controller input, editable Android HUD and touch layouts, character and skin selection.
- Optional modern lighting, shadows, materials, ambient occlusion, effects, snow and scaling; original presentation remains available.
- Kicks, shield bash, dual wield, bloodlust leap/shield charge and tactical humanoid combat.
- Open Rune multiplayer with Deathmatch, Team Deathmatch, Arena and HeadBall, bots, direct invitations and HyperDHT discovery. Requires matching Open Rune builds; original Rune server compatibility is not implemented.
- Bounded client movement prediction, adaptive smoothing and authoritative combat, retaining authored wind-ups and the two-tick late-block window.

Development has used AI-assisted coding and image tools, automated validation and human playtesting. Campaign completion, all-device compatibility, broad NAT coverage and sustained performance are not claimed.

## Build and use

- [Android build/device guide](Android/README.md)
- [Windows build guide](Windows/README.txt)
- [General desktop build instructions](Docs/Building.md)
- [Hosting and joining](Tools/RuneMultiplayer/README.md)
- [Menu hierarchy](Docs/RunePort/MENU-CLEANUP.md)
- [Prediction/smoothing and measured limits](Docs/RunePort/NETWORK-PREDICTION-SMOOTHING.md)
- [Project overview and development history](PROJECT_OVERVIEW.md)

The current source uses protocol/ALPN 22, native rules 20 and snapshot schema 6. Older milestone numbers in the documentation are historical. The source repository is publicly readable and cloneable without an account; local SDKs/toolchains and separately licensed game data are still required to build and play.

## Licensing and distribution

See [LICENSE.md](LICENSE.md), [NOTICE.txt](NOTICE.txt), the bundled component notices and [distribution materials](Docs/RunePort/DISTRIBUTION.md). This fork does not change the rights granted by those component licenses. Distribute matching LGPL source/replacement materials alongside each binary release. The engine licenses do not license the original Rune game data.

Not affiliated with or endorsed by the original game's owners or SurrealEngine contributors. [Upstream README](Docs/Upstream-README.md).
