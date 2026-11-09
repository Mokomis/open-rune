# Existing-map bot traversal — November 4, 2026

This is a bounded compatibility and anti-stuck pass for the 44 shipped
multiplayer maps. It is not a learning system or a new universal gameplay
planner. Original maps, player movement, combat timing and damage are retained.
Mac candidate only; no Windows or Android installation or Git publication.

## Shared corrections

* Existing route-rejection memory, vertical arrival checks and exploration
  memory now apply in every multiplayer mode, rather than only Team Deathmatch.
* Passenger-platform discovery includes Rune's separate Polyobj/AttachPolyobj
  hierarchy. Narrow chains and other nonrideable geometry still fail the
  collision/top/landing checks. Tiny startup keys on the same floor are merged.
* A lift approach uses the closer boarding threshold even when ordinary route
  navigation considers its waypoint reached. An unreachable approach times out
  instead of indefinitely suppressing ordinary movement recovery.
* Riders move inward when an overhang obstructs their remaining vertical path.
  A returned lift can be retried by normally walking off and boarding again,
  after its original hold/delay time. No script callback is invoked directly.
* At the destination, the bot can choose another verified exit. Ordinary jumps
  with checked clearance handle lips/short-hold platforms. Riders seek extra
  solid-ground clearance when room permits, rather than stopping at the edge.
* An empty upper landing cannot call a stand/bump-only platform. After observing
  an ordinary return interval without movement, a bot abandons that attempt;
  it does not wait for a hypothetical passenger forever.
* Visible duplicate or unreachable supplies receive a bounded number of normal
  Use attempts. A failed pickup no longer bypasses the progress watchdog forever.

Bots still submit normal authenticated player inputs. Host collision, basing,
pickup eligibility, scripts and action timing determine whether an action works.
There are no teleports, scripted position corrections, hidden-opponent reads or
map-specific combat advantages. Existing block windows and wind-ups are unchanged.

## Validation and limits

Private evidence: `outputs/bot-lifts-allmaps-20261104`, outside Git. Its
`verification.json`, `coverage.md` and `CHECKPOINT.md` record final hashes,
per-map results and limitations. The final Mac build passes all 44 one-minute
exploration samples; the longest stationary interval is 6.17 seconds. All 74
meaningful lift-direction cases on 37 platforms across 11 maps pass. All 44
original map hashes match the pre-change inventory.

Two 90-second authenticated four-bot matches pass on Deathmatch/Ruins and
Arena/BladePit, including combat, varied characters, queue admission and round
resolution. Focused team support/recovery, an actual HeadBall goal, raised-corner
escape and opposing/grouped allied crowd checks also pass. These selected checks
are not every mode on every map. No new rendered FPS measurement is claimed.

`bot_lifts_allmaps.py` inventories all selected maps and runs every discovered
meaningful lift stop pair in independent server/client engine processes.
Authoritative physics is 60 Hz, encoded snapshots 30 Hz and bot decisions 10 Hz.
The original mover scripts must actually board, carry and release the player.
The host independently verifies travel, proximity to the goal and cleared basing.
This deterministic local file exchange is not a socket/latency or FPS benchmark.

`bot_roam_allmaps.py` runs one simulated minute of normal exploration from one
ordinary spawn per map, using the same independent-world arrangement. It does
not force a path, grant equipment or manufacture movement. Original deaths and
respawns are permitted; obsolete input during death/respawn is discarded like
the live server. It records travel, deaths and the longest stationary interval.
That catches prolonged stalls in the sampled session, not every room or secret.
The exploration fixture uses TeamGame movement on each map; it is not a claim
that all four modes were completed on every map.

The original initial discovery scan can reject complex raised exits or
conditional machinery before creating a route. DM-HelEvator's raised multi-stop
mechanism, the Team_ships crow's nest, some Cathedral exits and gong/item-driven
secrets remain outside certified lift coverage. Discovery success alone is not
an actual traversal pass, and passing exploration from one spawn does not
establish access to every player-accessible location.

The user's bounded-time scope prioritizes concrete sticking bugs over building
a general puzzle solver or claiming exhaustive player-equivalent access. Any
remaining exceptions must be reported rather than silently called complete.
