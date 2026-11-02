# Private multiplayer on a laptop

Public discovery is also available: use `--public` with hosting or `--join-key HOST_ID` to join a discovered host. Install the pinned dependencies in [RuneDiscovery](../RuneDiscovery/README.md) first. The Android menu embeds that runtime and exposes Find games. See the [device checkpoint](../../Docs/RunePort/HYPERDHT-DISCOVERY.md) for testing limits.


Build Open Rune with `OPENRUNE_ENABLE_NETWORK=ON`, then use Python 3 and
`private_session.py`. This launcher creates fresh saves, settings and logs for
every session. Game assets are read from your legally supplied Rune Classic folder.
Keep the whole `.app` bundle together on macOS; the executable needs its resources.

## Host and play

Run from the repository root, replacing the two asset/session paths:

```sh
python3 Tools/RuneMultiplayer/private_session.py --host \
  --binary build/SurrealEngine.app/Contents/MacOS/SurrealEngine \
  --game /path/to/rune-classic --session /path/to/new-private-session \
  --address 192.168.1.25 --players 4 --mode dm \
  --maps DM-Hildir,DM-Thorstadt
```

Use your laptop's reachable local IPv4 address. Omit `--address` for laptop-only
loopback tests. The session directory must be new. The launcher starts a separate
host and your game window; player 1 belongs to that window. Give each friend a
different file from the session's `invitations` directory. An invitation contains
its private access key, destination and player slot. Share it only with its player.
It remains usable for reconnecting while that host runs, including after map travel.
Each player needs matching Open Rune and game assets. Invitations from previous
experimental protocol builds are intentionally rejected.

## Join directly

```sh
python3 Tools/RuneMultiplayer/private_session.py \
  --join /path/to/player2.rune-invite \
  --binary build/SurrealEngine.app/Contents/MacOS/SurrealEngine \
  --game /path/to/rune-classic --session /path/to/new-join-session
```

The invitation supplies the address and slot; there is no public directory.
Connections require direct UDP reachability. This implementation does not supply
NAT traversal or a relay. Close your game window to leave; use the same invitation
and a new session directory to reconnect. An abandoned connection times out before
its slot becomes available. Closing a player-host's window stops its host too.

## Headless host

Replace `--host` with `--headless`. All invitations are available to remote
players. The host remains available when the lobby empties. Stop it with Ctrl-C;
only its own child processes are stopped. Logs are in `host/session.log` and
`player/session.log`. `--seconds N` optionally bounds a session.

## Modes and rules

| Option | Meaning |
| --- | --- |
| `--mode dm` | Deathmatch on any shipped DM map |
| `--mode tdm --teams 2` | Team Deathmatch; 2, 3 or 4 teams |
| `--mode arena --arena-team-size 1` | Arena queue; 1v1 through 4v4 where the map supports it |
| `--mode headball --teams 2` | HeadBall; 2 teams on every HB map, 3/4 on supporting maps |
| `--limbs-score` | HeadBall also awards points for severed limbs |
| `--players N` | 2–16 invitation slots, including the local host player |
| `--score-limit N` | Frags, team points or Arena rounds; 0 disables |
| `--time-limit N` | Match seconds; HeadBall starts its clock after pump countdown |
| `--intermission N` | 2–60 seconds before the next map |
| `--maps A,B` | Repeating rotation; variant support is checked before invitations |

At least one match limit must be enabled. The lobby starts with two connected
players, or enough for the selected team/Arena variant; it need not fill every
invitation slot. Teams follow fixed invitation slots and retain the map's authored
colors. Arena allows additional players to wait in its original queue.

Arena maximum team sizes: AEternal/BladePit 3; Cathedral/TeamVent/Widowmaker 2;
Champions/SmeltingPot 4; Theatre/TimeToVent 1. Prefix these names with `AR-`.
Team Deathmatch supports three teams on 25 maps and four on 24.
DM-Team_chasm and DM-Team_ships support two teams; DM-TEAM_MadKing supports
up to three. The other 24 DM maps accept all three team counts.

Three/four-team HeadBall maps: HB-Aqueduct, HB-DeCapital, HB-Montezuma,
HB-Montezuma2. HB-HelRaiser uses Blue and Gold teams.

Keyboard: WASD or arrows move; mouse looks; left mouse attacks; right mouse
blocks; F kicks; Space jumps; Ctrl uses/picks up; Q or middle mouse throws;
E activates Rune Power; 1–4 select fists/sword/hammer/axe; Enter respawns when dead.
Escape releases or recaptures the mouse. Existing native dual-wield/bloodlust
rules remain authoritative on the host.

## Validation and limits

See `Docs/RunePort/PRIVATE-MULTIPLAYER-STATUS.md` and its evidence directory for
exact automated coverage and measured capacity. Sixteen is the original game's
admission ceiling and the enforced private-host maximum, not a promise of smooth
play on every machine or network. Human combat feel, audio quality, fairness and
extended real multiplayer sessions still require playtesting. The laptop tests
use bots and explicit objective fixtures; they do not simulate human strategy.

The original integration left Android untouched. Subsequent work installed the
matching protocol 17 client with hosting/joining menus and tested it across LAN.
See `Docs/RunePort/MULTIPLAYER-UI.md` and `ANDROID-CONNECTION-RECOVERY.md` in that
same directory for current device and recovery coverage. Campaign saves and
original assets remain preserved; clients still need matching builds.
