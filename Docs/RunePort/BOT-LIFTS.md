# Multiplayer bot lift traversal — November 4, 2026

The later existing-map anti-stuck pass is recorded in
[BOT-MAP-TRAVERSAL.md](BOT-MAP-TRAVERSAL.md). It extends the initial three-map
milestone below with Rune Polyobj support, broader actual rides and normal
exploration checks. The six-case section is historical coverage, not the latest
all-map result.

Mac candidate only. Windows and Android have not been installed or updated for
this work. The candidate remains protocol/ALPN 25, snapshot schema 7 and native
rules 29. No new wire format, combat timing or original map edit is required.

## Behavior

The input-driven multiplayer `RuneBot::Brain` can approach a verified landing,
wait for a passenger lift, board, ride and walk completely off at its destination.
It uses ordinary movement and Use inputs; the original authoritative mover
scripts control activation, motion, collision, basing and return timing. It does
not teleport, trigger script callbacks directly or modify movers.

Discovery accepts vertical, nonrotating `UMover` platforms with a traced walkable
top, adequate capsule space and at least two stops with clear walking landings.
It excludes narrow chains, tall door shapes, hazardous landings, damage-only
activation, creature-only bumps and once-only movers. Supported activation
includes standing, player bump, Use and active proximity/Use triggers connected
directly or through bounded, known `Dispatcher` event chains. Empty event/tag
names do not establish a connection. Unknown script chains remain unsupported.

Landings are sampled in 16 directions with floor, body, head and corridor checks.
Ordinary step-up and bounded step-down clearance are allowed. Jumping or ledge
grabbing out of a shaft is not implemented. Discovery is cached for five seconds
while idle so changed doors can be reconsidered, and is retained during a ride.

An active trip persists through ordinary objective/combat reconsideration.
Riders infer support from replicated platform geometry and the grounded flag;
they do not require the host-only `ActorBase` pointer. Boarding does not reset
back to the staging point, and riders with sufficient edge clearance need not
fight over the exact center. Existing teammate side-passing is suspended during
riding and departure. Completion requires the whole capsule clear of the lift,
grounded on the exit level. Death, physical knockdown and bounded trip timeouts
release the plan; failed entries are temporarily rejected.

## Mac evidence

Private evidence is under `outputs/bot-traversal-20261104` outside the repository.
`Tools/RuneValidation/bot_lifts.py` runs independent server and client engine
processes using real maps, original mover scripts, normal bot decisions and
actor/snapshot/input codecs. Physics runs at 60 Hz, snapshots at 30 Hz and bot
decisions at 10 Hz. The fixture stages an initial landing and fixed objective;
optional previous-rider setup uses normal landing physics. It does not manually
call a mover's activation/interpolation callbacks.

Six cases cover:

| Map | Platform | Case |
| --- | --- | --- |
| DM-LostCrypt | Mover8 | Ascend |
| DM-LostCrypt | Mover10 | Taller ascent |
| DM-LostCrypt | Mover8 | Descend after a previous rider |
| DM-LostCrypt | Mover8 | Wait while the platform is away |
| AR-Cathedral | Mover5 | Player-bump activation and ascent |
| HB-Aqueduct | Mover0 | Proximity trigger through Dispatcher and stepped exit |

Each case requires the host to confirm actual platform basing, more than 100
units of vertical travel and departure near the goal without remaining based
on the platform. The client must finish with no authoritative base pointer.
This deterministic local file-exchange harness tests separate replicated worlds;
it is not evidence of UDP latency/loss recovery or rendered multiplayer FPS.

All 44 multiplayer maps also complete initial-state discovery without errors.
That scan identifies candidate platforms; it does not prove traversal of all
maps. The native navigation-integrity regression and a separate 90-second,
four-bot authenticated loopback Team Deathmatch on Hildir pass. The latter
checks combat, engagement, deaths and varied characters, not lift coverage.

## Remaining navigation work

* DM-HelEvator's main mechanism has repeated keys, multiple stops and raised
  exits beyond ordinary walking clearance. It is rejected rather than assigned
  a fabricated route. It needs explicit ledge/jump planning and real traversal
  tests before support can be claimed.
* Shot/damage/key-dependent switches, arbitrary event chains, rotating or
  horizontal platforms and one-shot machinery need separate handling.
* Jump-only routes and conditional teleporter/door routes need representative
  end-to-end tests. Existing authored teleporter handling is retained, but this
  pass does not newly certify it.
* Multi-rider lift crowding and every map bottleneck are not certified by the
  single-rider lift matrix. A successful initial-state discovery scan is not an
  all-map gameplay pass.

The earlier [native navigation audit](NAVIGATION-AUDIT.md) concerns scripted
enemy path queries as well as input-bot checks. This lift behavior is specifically
for the multiplayer input bot and does not claim new campaign ScriptPawn lift AI.
