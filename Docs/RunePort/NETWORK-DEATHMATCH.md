# Four-player Deathmatch and map rotation

October 26, 2026. This extends the interactive client into a complete private
Deathmatch match cycle. It supersedes the two-player/one-map limit in
[NETWORK-INTERACTIVE.md](NETWORK-INTERACTIVE.md). It does not establish all game
modes, all maps, campaign co-op, or original Rune server compatibility.

## Match behavior

The host supports 2–4 invitation slots. All players must authenticate, agree on
content/rules, create their actors and install the initial world before the
60 Hz simulation starts. The existing 30 Hz snapshots, fractional body/torso
interpolation, grounded prediction, kick/bash and 33 ms defense window remain.

Rune's original server-side GameInfo/Killed/ScoreKill path supplies scores:
kill +1, suicide/world death -1, and victim deaths +1. The client never submits
a score. A reliable bounded status record carries all player scores, match
phase, remaining seconds, frag limit and winner (zero means a draw). The host
owns the timer and match completion. The original automatic travel and limits
are disabled for this session so they cannot race the new map barrier.

At the frag/time limit the host freezes simulation, publishes the final world
and scores, shows an intermission countdown and loads the next configured map.
Tied top scores produce a draw. All clients must install the next map before
play resumes. Scores, lives, actors, input/action history, prediction, combat
queues and snapshot baselines reset. Equipment is supplied by the next map's
normal login/spawn path. No campaign saves are used.

Desktop displays the standings in its multiplayer overlay. Android's themed
pause menu shows the live ranked scoreboard; its editable Score/time HUD shows
the local score and clock. Intermission displays the winner/draw and next-map
countdown. A player cannot submit combat/respawn requests during intermission.

## Running a private match

Networking remains opt-in (`OPENRUNE_ENABLE_NETWORK=ON`). For example:

```
SurrealEngine --direct --save-directory=/private/host-saves \
  --rune-server=/private/new-invitations --rune-net-address=192.168.1.10 \
  --rune-net-players=4 --rune-net-maps=DM-Hildir,DM-Thorstadt \
  --rune-net-frag-limit=20 --rune-net-time-limit=600 \
  --rune-net-intermission=8 /path/to/Rune
```

Time limit is in seconds; zero disables either limit, but at least one must be
nonzero. Intermission accepts 2–60 seconds. Default roster remains two players
for existing private-session launchers; explicitly select four above. Default
rotation is Hildir/Thorstadt. Map names must be safe `DM-` package names; up to
32 rotation entries are accepted and their contracts are hashed before publishing
invitations. Acceptance is not a claim that every map's movers/triggers work.

Share each private `playerN.rune-invite` with that player. New invitations use
format 2, adding the player count; format 1 is still parsed as two players.
The current host/client builds must match. Android uses Multiplayer → Open
invitation. This remains a command-line host, not an in-game hosting browser.

## Protocol boundary

Protocol/DTLS ALPN is **11** (`open-rune-net/11`), native rules revision **11**.
Snapshot payload format remains **5**. Both ends must update together.

Reliable control type 9 is match status (up to 32 bounded score entries).
Type 10 is a map-travel offer with the next map epoch and content contract.
The authenticated DTLS session and transport epoch stay alive during travel;
a separate increasing replication epoch guards every map-specific record.
The client hashes its own next map/scripts before accepting and acknowledging
the new contract. The server waits for each client's new baseline installation.
Old-map inputs, actions, snapshots, actor records, results and scores are rejected
before they can touch the new world. A leave record uses the stable session
epoch so leaving works even in the loading barrier. Map loading has a 45-second
barrier timeout; no reconnect or content download is implied.

## Validation

[Retained evidence](evidence/network-deathmatch/) records successful four-client
frag-limit cycles, time-limit draws under the stated impairments, and the prior
two-client movement/kick/jump/throw/death/respawn fixture with a delayed join.
All 12 standalone groups pass in Release and ASan/UBSan. The expanded fuzz
decoder completed 10,415 executions in a 21-second smoke run without a finding.

The OPPO passed two transitions as player four while showing all four scores,
original kill/suicide results and the unchanged connection. Short actual displayed
FPS samples measured **114.72** on Hildir (17.46 s) and **98.55** on Thorstadt
(17.64 s); both were native 2520×1680, 4× AA, with no interval above 50 ms.
The panel stayed at 144 Hz. These stationary spawn views are not matched
before/after optimization measurements or sustained combat results. All normal
save/backup and preference hashes matched, as did the per-app refresh rule.

An initial cold four-window launch exceeded its short fixture duration. A
scene-prewarm experiment dereferenced an absent view actor and was removed;
subsequent final runs use the normal loading path. An Android USB interruption
aborted the first device attempt; the fixture now retries transient reads and
always cleans up desktop peers before device-dependent cleanup. The repeated
complete Android run passed. Failed attempts are not counted as passes.
The fixtures use disposable save folders and private temporary invitation files.
`network_match_cycle.py` runs four independent rendered clients and verifies
original kill/suicide scoring, frag-limit or tied time-limit completion,
intermission, Hildir → Thorstadt → Hildir, score reset and unchanged authenticated
session IDs. `--impaired` adds 75 ms each way, ±15 ms jitter, 3% loss and 2%
duplication independently to each client.

`android_match_cycle.py` uses the OPPO as player four with three desktop clients,
native 2520×1680 and 4× AA. It captures two maps, the Android scoreboard and
actual SurfaceFlinger presentation intervals. These are short representative
map views, not sustained combat or all-map performance guarantees.

Protocol tests cover status/travel bounds and truncation, delayed engine map
acceptance, stale old-map messages, unchanged session IDs and leaving during a
travel barrier. Independent Python byte fixtures check the new wire records;
the fuzz decoder includes both. Existing network tests also cover movement,
action admission, snapshots, clocks, authentication and impaired delivery.

## Remaining scope

This is still a private fixed-roster match: one departure/disconnection ends the
session for everyone. Late join/reconnect, continuing with vacant slots, public
hosting/discovery/NAT and Windows socket integration remain unfinished. Team
Deathmatch, Arena and HeadBall rules/objectives are not implemented by this
milestone. Movers/world events, inventory selection, combat audiovisual feedback,
dynamic severing/skin replication and broader map behavior still need work.
The four-player fixture does not establish 8/16/32-player capacity or human
melee fairness. No upstream contribution or public hosting change was made.
