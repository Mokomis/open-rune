# Teammate passing and crowded combat — October 31, 2026

The multiplayer client bot previously rebuilt a sideways attack-lane goal relative
to its own changing position every think. Nearby teammates could therefore keep
circling one another. Ordinary route movement had no matching pawn traffic policy:
terrain sweeps intentionally exclude pawns, because their moving capsules must not
be baked into the static route graph.

`RuneBot::Brain::avoidTeammates` now checks visible, living nearby teammates using
normal snapshot position, velocity and capsule size. Bots follow a moving convoy
at a safe speed, retain a short passing goal, and use a stable actor-ID priority
when one must yield. Opposing traffic prefers each bot's own right. An obstructed
step waits briefly instead of changing side every 100 ms. Candidate steps and
passing endpoints must retain ordinary terrain, floor and hazard clearance;
neighbor motion is checked over a short horizon. No collision exemptions, pushes,
teleports, hidden opponent positions or input reads are used.

An ally occupying an attack lane still suppresses the attack. It now leaves the
approach to the shared traffic policy instead of repeatedly moving a flank goal
with the bot. Deathmatch, committed special moves, airborne motion and stationary
guarding are excluded from this steering. Campaign ScriptPawn AI is unchanged.
Both team labels use the same policy; priority does not depend on team color.

## Validation and limits

`--rune-bot-crowd-probe` uses three actual moving player pawns, normal authoritative
input and VM physics, real pawn collisions, and original DM-Hildir floor geometry.
Eight seed triples and both team assignments exercise opposing traffic and a
follower. All 16 final cases clear; the slowest takes 7.08 simulated seconds versus
8.73 for the old policy. The old policy has a faster average on this open crossing;
this change is intended to remove congested circling, not speed up every encounter.

`--rune-bot-group-probe --crowd-case=N` uses three allies approaching the same
stationary enemy. Each must actually request an attack or offensive special; just
moving through the area is insufficient. Four seed triples with both team labels
produce eight independently launched cases. The old policy completes none within
25 simulated seconds, leaving one or more bots circling. The final policy completes
all eight in 14.42–18.82 simulated seconds. Each process starts from fresh map/VM
state so preceding attacks cannot contaminate the next placement. The default
single group case is zero; cases 0–7 are selected explicitly by the runner.
Travel is counted only until each bot reaches its individual objective.

Team-lane/recovery/support checks and the native combat regression suite also pass.
Full-match results, executable/source hashes and live deployment state are recorded
in `outputs/bot-crowd-20261031/checkpoint.json`. This is representative TDM coverage,
not a guarantee that every authored bottleneck in every map is navigable.

## Reproducible builds

The private Windows builder compiles the common library and links to a unique EXE,
PDB and import library, leaving live executables and `SurrealEngine.pk3` untouched.
When switching baseline/candidate source backups, refresh **RuneBot.h as well as
its implementation/probes** before building. Copying a backup can retain an old
timestamp: stale dependent objects can then disagree on `Brain` layout. Earlier
mixed-object builds and the baseline full-match teardown failure are not accepted
as valid full-match comparisons. The final build recompiles RuneMultiplayer and
all changed bot/probe units together. Faster sidestepping and enemy-anchored flank
experiments were rejected; they are not deployed.
