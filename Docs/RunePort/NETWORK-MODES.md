# Private multiplayer game modes

October 28 integrated implementation: [complete map/variant matrix and current limits](PRIVATE-MULTIPLAYER-STATUS.md), [laptop hosting instructions](../../Tools/RuneMultiplayer/README.md), [protocol 17 changes](PRIVATE-MULTIPLAYER-PROTOCOL.md). The October 26 checkpoint below is historical; its Android results do not validate the new protocol.

Historical mode validation: October 26, 2026. Protocol/ALPN **16**, native rules **16**, snapshot **6**, invitation **3**, following [Rune Power activation](RUNE-POWER.md). Both client and host must update together. Networking remains opt-in and private.

## Implemented mode rules and validation

| Mode flag | Representative map | Desktop and Android coverage | Android native 4× AA sample |
| --- | --- | --- | --- |
| `dm` | DM-Hildir (DM-Thorstadt used only for rotation) | Four players, kill/suicide scoring, frag/time limit, draws, results, two-map rotation | 114.72 / 98.55 FPS |
| `tdm` | DM-Team_ships | Red/Blue scoring and win, ally damage/knockback/stun protection, respawn team retention, same-map restart | 73.98 FPS |
| `arena` | AR-Champions | Physical queue-zone entry/exit, countdown, champion versus three successive challengers, wins/losses, manual and original-script respawn, same-map restart | 100.98 FPS |
| `headball` | HB-Agora | Networked Use activates both team pumps; countdown; networked Throw releases an equipped head; original goal-zone entry scores and consumes it; kills do not award points; respawn and same-map restart | 71.10 FPS |

One representative map per additional mode, as requested. FPS samples are roughly 17 seconds at 2520×1680, 4× MSAA and a 144 Hz panel. No sample had a frame interval over 50 ms. They are different views, not matched networking comparisons or sustained combat measurements. No all-map, all-variant or 120 FPS guarantee.

Detailed checkpoints: [Deathmatch](NETWORK-DEATHMATCH.md), [Team Deathmatch](NETWORK-TEAM-DEATHMATCH.md), [Arena](NETWORK-ARENA.md). HeadBall evidence is in [network-headball](evidence/network-headball/). All normal save/preference hashes and the per-app refresh rule were unchanged after Android checks. The app is left at its title screen after fixture cleanup.

## Hosting

Use the existing command-line invitation host with `--rune-net-mode=dm|tdm|arena|headball`, `--rune-net-players=2..4` and an isolated `--save-directory`. Default maps are the listed representatives; DM retains the two-map default. `--rune-net-maps` accepts a comma-separated rotation with the matching DM-/AR-/HB- prefix. Invitations are private per-player files opened through the Android Multiplayer menu.

`--rune-net-frag-limit` is the personal/team score cap for DM/TDM/HeadBall; zero disables it. Arena also accepts `--rune-net-round-limit`, using the original Arena internal match counter. `--rune-net-time-limit` is seconds; zero disables it, but one limit must remain enabled. HeadBall's clock starts after both teams are ready and the original countdown finishes. `--rune-net-intermission` controls the result screen before restart/travel.

The host uses original loaded Rune game classes and scripts; assets are unmodified. HeadBall's redundant login rebalancing is disabled because invitation slots already assign alternating balanced teams. Current team modes use Red and Blue. Team Arena and four-team HeadBall variants are not separately validated.

## Shared fixes

- Reliable status includes mode, team totals, Arena roles/queue/countdown and HeadBall readiness/countdown. Contracts reject mismatched modes.
- Authored Mover/Polyobj and Pump objects bind to matching local map actors rather than brush-less copies. Moving-brush interpolation refreshes collision only when the transform changes. A desktop probe moved/restored an authored brush and checked collision bounds on every client.
- Original-script respawn increments the authority life generation and clears stale defense/input histories. Countdown-unresponsive players cannot start native combat; native kicks/bash respect same-team and HeadBall pre-start protection.
- Dedicated servers can decode sound duration for original script timing without dereferencing a nonexistent playback device. This fixes the HeadBall map startup crash.

## Test boundary and remaining work

The HeadBall fixture equips an original head and throws it through the network action path, then places that thrown head inside the authored goal zone. Goal entry, score, destruction and lifecycle are real original-game callbacks; it is not proof of an unaided human throw into the goal. Arena deaths are also bounded fixture calls. Human combat fairness remains separate.

All 12 standalone groups pass in Release and ASan/UBSan. Independent replication fixtures pass 1,675 valid / 4,135 invalid records. A 21-second sanitized decoder fuzz smoke completed 30,775 executions without a finding. Desktop and Android builds pass.

This remains a four-slot fixed-roster private match: leaving/disconnecting ends the session. Late join/reconnect, vacant-slot continuation, public discovery/NAT, Windows socket integration and 8/16/32-player validation are unfinished. Inventory selection, combat audiovisual effects, dynamic severing/skin replication and broader map-specific world events still need work. Mode-rule validation is not full multiplayer release readiness.
