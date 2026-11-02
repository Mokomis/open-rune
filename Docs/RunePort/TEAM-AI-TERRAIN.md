# Team bot activity and terrain reconciliation — October 31, 2026

## Changes

The subsequent teammate-traffic correction and moving collision replay are
documented in [BOT-TRAFFIC.md](BOT-TRAFFIC.md). That policy replaces the initial
moving flank goal described below.

Team bots choose a collision-checked approach beside a teammate rather than
continually yielding sideways in the same attack lane. Nearby teammates already
pressuring an enemy reduce that target's priority. Action recovery delays the
next attack but allows ordinary movement and footwork. An armed, healthy TDM bot
can rally to a visible teammate's attack animation, without learning the position
of any unseen opponent or reading input buttons. Reaction delays, attack wind-ups,
resources, hit rules and the 33 ms defensive grace window remain in force.

TDM navigation retains a route long enough to traverse it, remembers a bounded
set of temporarily rejected goals, and gives ordinary jumping/ledge/door recovery
a chance before abandoning a missing map-graph connection. Patrol arrival checks
include height; a bot directly above a destination on another floor has not
arrived. A bounded memory of searched patrol areas discourages repeating the same
empty floor. The navigation policy changes are confined to TDM; other modes retain
their previous route timing and rejection behavior. Campaign ScriptPawn tactical decisions are unchanged by these movement fixes.
The optional normal-combat leap/rush rule is documented separately in
[COMBAT-AVAILABILITY.md](COMBAT-AVAILABILITY.md).

Local multiplayer presentation now follows bounded wall slides during small flat
corrections. It can follow the existing up/across/down step route even when a low
obstacle's endpoints have the same floor height. A parallel wall contact at the
collision skin is re-swept a quarter unit outward, with clearance validation and
a fresh sweep that still detects the next obstruction. The exact movement
prediction sweep and authoritative physics remain unchanged. Presentation does
not fire Touch/Use/HitWall, move other actors or change damage/inventory.

## Verified coverage

The focused checks use original owned DM-Hildir geometry and isolated saves.
Editor markers are projected to valid standing positions. Duplicate server and
replica pawns in the single-process fixture are removed from the terrain scan and
restored before combat tests; they do not coexist on a real client.

- Baseline: 301 of 7,647 correction sequences remained pinned; worst remaining
  gap was approximately 150 game units. With the fix: zero pinned sequences and
  a maximum remaining gap of about 1.1 units. Ordinary walking/stair replay also
  has zero stalls and no gaps over three units.
- Team fixtures verify forward progress into a flank, movement during action
  recovery without an early second action, identical decisions after swapping
  red/blue labels, and support prompted by a visible ally's attack animation.
- A rendered Windows replay at native window pixels and 4x MSAA passes with
  75 ms in each direction plus jitter/loss. Both ascent/descent sections contain
  264 frames, exceed 650 units of travel and 200 units of rise, and have zero
  reverse steps. A single foreground rendered client and a headless second peer
  avoid background-window/display-sleep throttling. This is not tablet FPS data.
- Native combat regressions pass, including the 12 melee-window cases, dual
  wield, anticipation, bash pose/audio/stagger, bloodlust moves, mode protection,
  rune power, inventory/food, appearance restoration, severed limbs and corpses.
- The ordinary bot escapes the raised-geometry navigation fixture in 10.5
  simulated seconds. The HeadBall roof-start fixture scores via normal movement
  and a real thrown head in 8.87 seconds. These fixtures are separate from full
  match/readiness coverage.

A later match replay exposed a standing capsule trapped under a low overhang.
The bot now considers exact wall-tangent directions and a modest collision-swept
step/drop (at most 96 units below its feet; harmless shallow water only for player
swimmers). It briefly retains that escape direction, validating each next step,
instead of immediately turning back toward its unreachable patrol goal. Movement
still uses normal authenticated input and authoritative physics. Two exact logged
positions escape on Windows and Android; the floor-level fixture previously
remained stranded for the full 180 simulated seconds and now escapes in about
11.2 seconds on Windows / 11.4 on ARM. This is a recovery policy, not a scripted
teleport or a campaign/map-wide navigation guarantee.

Full-match results, executable/APK hashes and installation status are recorded in
`outputs/terrain-team-fix-20261031/checkpoint.json`. Intermediate failed candidates
and under-sampled graphics attempts are retained and are not successful coverage.
Original assets, ordinary saves/settings and prior binaries/packs are preserved.
No public match is restarted by this work. No commits or pushes were requested.
