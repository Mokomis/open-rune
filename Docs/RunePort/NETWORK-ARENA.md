# Arena checkpoint

October 26, 2026. Protocol/ALPN 13, native rules 13, snapshot 5. Extends [Team Deathmatch](NETWORK-TEAM-DEATHMATCH.md).

`--rune-net-mode=arena` selects the original `Arena.ArenaGameInfo` with default `AR-Champions`. The server runs the authored queue-zone entry/exit callbacks, countdown, champion/challenger selection, teleportation, 200-health fights, round-win/loss scoring and respawn. The client displays queue positions and round phases. The original scripts remain unmodified. `--rune-net-round-limit` selects the Arena match limit; the time limit remains available. A one-map rotation restarts the session on that map without reconnecting.

The shared replica layer now binds authored Mover/Polyobj geometry by verified map actor name, class and package. Transform interpolation refreshes collision bounds when the brush changes, without client gameplay callbacks. Authority life generations also detect original-script respawns, clear old defense/input history and reject prior-life actions. Native kick/bash cannot bypass Arena's same-team protection or the Unresponsive countdown state.

## Evidence

[network-arena](evidence/network-arena/) contains the bounded automated checks. Only AR-Champions was tested.

- Four rendered desktop clients: physical placement inside the authored QueueZone, three successive challengers, correct champion wins and each challenger's loss, respawns and all countdown/fight/post-match states. A direct-callback prototype was replaced because it skipped queue-zone departure. Test deaths invoke original Died; this is not natural combat or human fairness proof.
- Four authored brushes bind with geometry and collision present on each client. The follow-up desktop probe moves and restores one authored brush and checks its rendered collision bounds on all clients. It also calls the original ServerReStartPlayer for the third defeated challenger and verifies life 2 / health 200. These are explicit probes; the map's brushes stayed stationary during its ordinary round cycle.
- Android plus three desktop clients: queue, fights, results, timer-based match end, same-map restart with unchanged authenticated connection, and inspected scoreboard UI. Normal saves/preferences/app refresh rule unchanged.
- Android native 2520×1680, 4× MSAA: 100.98 displayed FPS across 17.22 seconds, p99 13.89 ms, maximum 20.83 ms, zero intervals over 50 ms, panel 144 Hz. Includes the opening queue/countdown/teleport sequence; not sustained combat or a matched performance comparison.
- Standalone Release and ASan/UBSan tests: 12/12 pass, including independent wire fixtures. Added bounded Arena status validation and authored-mover identity validation.

Team Arena on a larger team map is not separately validated. HeadBall is the next mode. Fixed roster, invitation setup and no reconnect/public lobby remain unchanged limitations. Campaign saves are isolated from every fixture.
