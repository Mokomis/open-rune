# Multiplayer map navigation review — November 4, 2026

All44 local Qwen reports have been reviewed against their original requests,
responses and deterministic map records. The Mac independently loaded all44
installed multiplayer maps: map hashes, navigation-node fields and serialized
reachspecs match the Windows inventory exactly. Original game assets were not
edited. This is complete static coverage, not44 full-match playthroughs.

## Report recovery and rejected claims

21 original reports were valid JSON.23 were malformed: most ended at the900-token
output limit; two also contained invalid abbreviated evidence arrays. Complete
quoted concerns were recovered (119 across44 reports), and all44 reviewed
reports are valid structured JSON. Missing response tails were not invented.
Original model text, prompts, statuses, finish reasons and hashes are preserved.
The reviewed versions reconstruct their facts from raw map data and distinguish
those facts from model hypotheses. No new inference was required.

Important corrections:

- `NavigationPointList` membership does not imply connected routes.
- `R_WALK|R_JUMP` is flag9. The distribution counts edges, not flagged nodes;
  it does not denote doors, missing movers or generic interactive obstacles.
- An upstream entry identifies the SAME directed A-to-B edge from B's reverse
  search. Recovering that entry does not authorize a B-to-A edge.
- The original collector's `ActorReachable(nav,true)` could reject from graph
  data before testing movement. It is not an independent physical spawn-exit
  test. The revised Mac diagnostic separately simulates walking to the sampled
  standing position.89 of178 graph-filtered failed spawn samples have at least
  one successful floor-walk sample. This remains bounded initial-state sampling,
  not proof that every exit works.
- Weak graph fragmentation, ArenaStart separation, a mover's initial state and
  stale indices in a map with stripped reachspecs do not alone prove broken
  gameplay. Original round, door, lift, teleport and jump rules remain intact.

## Verified native defects and repairs

The native reverse search trusted only each destination's fixed incoming array.
Five maps have valid outgoing directed links missing from those arrays.35
Viking-sized query cases were compared with a separate forward shortest-path
oracle. Before repair only3 matched; afterward all35 match:

| Map | Correct before | Correct after |
|---|---:|---:|
| AR-BladePit |0/4|4/4|
| AR-Cathedral |0/13|13/13|
| AR-Theatre |3/9|9/9|
| DM-Sanctum |0/2|2/2|
| DM-Team_ships |0/7|7/7|

`RuneNavigation::Incoming` builds a cached runtime reverse view of valid original
incoming entries plus endpoint-validated original outgoing links. It adds no
geometric edges and changes no original arrays. Pruning, cylinder dimensions,
player-only filtering and original special handling remain in the native query.
Explicit scripted destinations outside the linked list retain their original
incoming-array behavior. Cache state resets with map unload/load and invalidates
when a navigation actor is destroyed.

Four maps also contain stale or mismatched slot references: AR-Theatre,
DM-Alfheim, DM-Bothvar and DM-Thorstadt. The native walking reachability,
reverse-route search and random-destination expansion now skip out-of-range
positive slots rather than abandoning valid later slots; reject wrong-owner,
wrong-destination, deleted and missing endpoints before dereferencing; and retain
the negative array sentinel. A six-case isolated graph fixture verifies those
contracts, directed-edge preservation and restoration of all temporary arrays.
No missing serialized graph is reconstructed by trusting its stale indices.

The preceding shared body-sized wall adjustment, precise waypoint arrival,
stalled-plan recovery and bounded ScriptPawn local walking fallback remain.
They preserve original target acquisition, HomeBase/HuntDistance and scripted
states. These native NPC routines differ from `RuneBot::Brain`, the multiplayer
input-bot routing system; the work does not replace that system.

## Mac runtime coverage

- All44 map inventories agree with Windows, including final corrected floor-walk
  sampling and unchanged source hashes.
- All35 targeted reverse-route queries and six defensive graph cases pass.
-18 actual original-FSM pursuits pass on Alfheim, Bothvar and Thorstadt, selected
  from geometry for which the bounded walking fallback found a route.
- The Hildir regression passes12 wall cases, five original authored-route
  pursuits, six local pursuits, the stalled-plan watchdog and save/reload/goal
  cleanup. These are selected cases, not every room or character.
- Six tactical Viking/Goblin/Dwarf cases (seeds73/17), including kick, shield bash,
  group participation and defense, pass.
- Actual multiplayer input-bot team support/recovery, raised-corner escape and a
  HeadBall throw into its authored scoring zone pass. The HeadBall goal occurred
  at19.15 simulated seconds despite its fragmented authored graph.
- A bounded1200-frame rendered Hildir encounter at native2816x1762 and requested
  4x MSAA has10 AI attacks, one shield bash and180 cumulative incoming damage
  with fixture-only health/durability replenishment. Final capture was reviewed.
  Its900-frame profile is80.89 FPS,1.15ms tick and11.22ms rendering. This is a
  short Mac smoke test with other headless checks overlapping part of it, not a
  sustained multiplayer benchmark, displayed120 proof or measured speed gain.

The first render attempt left the classic menu open and is excluded as combat
proof. A separate generic Vulkan diagnostic hit an unsupported integer-format
blend/resolve path on MoltenVK; normal native4x combat rendered successfully
without that diagnostic. Its failed log is retained; no renderer change was
made for it. An early bot command invoked the general network fixture rather
than the bot fixture; corrected explicit bot invocations pass.

LostCrypt's selected starts are on lift-separated floors. No suitable six-case
obstructed walking fixture was found there. The negative test confirms that the
walker neither invents a cross-floor route nor relocates the NPC. This does not
validate lift AI; proper lift/door and objective traversal still requires its
own runtime evidence. Static warnings on the other maps are retained for later
focused investigation, with no unsupported asset repairs.

## Artifacts and deployment

Detailed per-map reviewed reports and original evidence are outside Git under
`outputs/navigation-review-20261104` above the checkout. `reviewed/review.md`
is the per-map decision table; `reviewed/*.json` retains individual corrections
and runtime references. `verification.json` records the final source/build/test
hashes and limits. `Tools/RuneValidation/navigation_integrity.py` provides the
new repeatable10-case regression; `navigation.py` retains the earlier coverage.

This is a Mac-built/tested candidate. The installed Windows and Android clients
were not updated. Protocol/ALPN25 and snapshot schema7 are unchanged; candidate
native rules29 remains distinct from the preceding installed rules28 set.
No commit or push was performed. The completed Qwen batch verified inference
shutdown; its original reports and worker configuration remain intact.
