# Enemy AI and multiplayer bots

October 29–30, 2026. This work changes decision-making; it does not increase health,
weapon damage, movement speed or hit range. Original assets and saves stay intact.

## What was there

Campaign enemies run the original UnrealScript state machines. ScriptPawn owns
perception, enemies, orders, navigation, hunting, pain, death and cinematic states.
Vikings, goblins and dwarves additionally have Open Rune's optional native tactical
combat controller. It takes over eligible authoritative `Fighting` states only.
Kicks, shield bashes, dual wield and bloodlust are separate shared combat systems.

The multiplayer host runs original mode rules plus Open Rune's authoritative
movement/combat replication. The automated opponents previously used for live
matches were stress-test clients: they cycled inputs rather than perceiving and
planning. The new normal `--rune-net-bot` client uses a tactical controller.
Explicit `--rune-net-bot-stress`, `--rune-net-bot-idle` and HeadBall fixture inputs
remain available for reproducible network tests; they are not intelligent bots.

| Mode | Bot decisions | Authority retained by the game |
|---|---|---|
| Deathmatch | Explore, acquire equipment, fight visible opponents, recover and respawn | Hits, damage, cooldowns, scoring and respawn eligibility |
| Team Deathmatch | Fight opposing teams; useful pickups and navigation | Team membership, friendly-fire rules and team scores |
| Arena | Reach the queue through normal movement/teleporters; wait for the round; fight only as an assigned contestant | Queue admission, challenger/champion assignments, countdown and round progression |
| HeadBall | Reach the team's ready pump, fight after the start, collect scoring heads/allowed limbs and throw toward a permitted goal | Pump use, countdown, limb-scoring rule, actual throws and scoring zones |

## Campaign family audit

| Family | Existing behavior and protected boundaries |
|---|---|
| Viking and dwarf | Weapon-specific strikes, inventory recovery, shield handling, pain/death; improved only in ordinary Fighting |
| Goblin | Melee, rock throwing, mining/partner scripts, alert and dodge states; only ordinary melee Fighting is intercepted |
| Giant/baby crab | High/low fighting, shell, flipped and rider-on-back states; retained |
| Zombie | Knockdown/get-up, head-dependent survival, Sark transformation and crucified scripted poses; retained |
| Sark family | Authored fast weapon combos and creature-specific behavior; Conrack also has boss/scripting states; retained |
| Snowbeast | Distinct bite/swipe/lunge attacks and their own attack notifications; retained |
| Aquatic creatures and ambient animals | Swimming/fleeing, pickup and ambient controllers; retained |

This is based on the recovered RuneI state machines and the native interception
points, not a claim that every campaign encounter was played. Replacing those
specialist states wholesale would change puzzles and enemy identities. Their
original animations, damage notifications and scripted transitions remain intact.

## Fairness

Bots are ordinary authenticated clients. They submit player inputs and actions;
they cannot set positions, award hits, teleport themselves or alter health.
They do not read another player's button presses. Perception uses a bounded
range, facing/proximity and an unobstructed ray. An obscured opponent leaves a
last-seen position that expires after 2.2 seconds, not a live through-wall target.
A threat gets one defense roll rather than repeated rolls until it succeeds.

| Difficulty | Minimum reaction | Successful defense chance |
|---|---:|---:|
| Easy | 420 ms | 45% |
| Medium (default) | 300 ms | 65% |
| Hard | 220 ms | 80% |
| Very Hard | 180 ms | 85% |

These are decision limits, not a promise that every eligible defense succeeds.
Facing, equipment, current actions and server validation still apply. Attack
wind-ups and the existing 33 ms late-block adjudication window are unchanged.
Bots have limited turning speed, finite guard windows, recovery pauses and
special-move cooldowns. Jump, kick, bash, cross-block, leap and charge go through
the same eligibility and resource checks as human actions.

## Navigation

Use the map's reachspec graph, with bounded searches and short collision/support
checks during movement. Floating editor markers are projected onto standing
positions. Authored teleporter links are followed by walking into the teleporter;
the original server-side Touch event performs any transport. Missing walking
links can be inferred from swept floor samples and cached for the map. A bounded
local floor search covers disconnected entrances. Water, pain zones and HeadBall
goals are not treated as safe walking destinations. Stuck detection replans; it
never relocates a pawn. Patrol goals persist long enough to traverse routes
between floors; waypoint tolerance accounts for normal movement momentum.
Walking checks include solid scenery, use the full
standing body height, and validate the floor connection before joining a graph
route. Local search distinguishes stacked floors. Ordinary jump/ledge-grab and
swimming inputs provide bounded recovery; collision still belongs to the server.

HeadBall carriers use the actual BSP zone openings, not the ZoneInfo editor icon
(which can sit below the floor). They approach a throwing position and solve an
arc using the original head mass, throwing speed and map gravity. Turning toward
a shot is bounded just like combat aiming. No direct score or head relocation is
used. A candidate throwing position must itself be safe and see the opening;
internal portal faces and roof surfaces are rejected. Carriers settle on the
ground before the delayed animated throw. Unarmed fighters outside Arena seek
known weapon racks instead of remaining in shield/kick stalemates. Visible
snapshots still determine pickup availability. Static map geometry is shared navigation knowledge; opponent perception
remains visibility-limited.

## Campaign changes and boundaries

The tactical controller now varies valid authored Viking/dwarf attacks, changes
position when wounded, recognizes recovery opportunities and avoids an ally's
attack lane. Pending kicks/bashes and bloodlust moves count toward local group
pressure, so special moves cannot silently bypass that coordination. Recovery
has small variation above its existing minimum. Wounded repositioning has a
cooldown so enemies do not retreat forever.

The existing Original/Tactical menu switch remains. Enemy acquisition, dialogue,
triggers, equipment recovery, hunting, pain, death and saved scripts retain their
original ownership. Native tactical state is transient and re-acquired after
loading. Bosses, cinematics, players and network replicas are excluded.

Crabs' high/low/flip combat, zombies' resurrection/crucifixion states, Sark-specific
combat and other specialized creatures retain their original controllers. This
pass does not claim a rewrite or full campaign playthrough of those creatures.

## Characters and operation

Normal bots choose among installed character/skin combinations at connection time and retain that identity across respawns/travel; duplicates are possible because the choice is random. Explicit
`--character` / `--character-skin` remain available for fixtures. The human's
selected character is unchanged. Map travel resets decision state; normal
respawn clears the previous opponent and movement intent.

`Tools/RuneMultiplayer/private_session.py` supports `--bots N` and
`--bot-difficulty 0..3`. It reserves the bot invitation slots before starting
public discovery, leaving at least one human slot, and owns/cleans up the bot
processes. Use normal bots for play; use the stress option only for stress tests.
No public match is restarted by this development work.

## Validation

Results and exact executable hashes are recorded outside Git in
`outputs/ai-overhaul-20261029` on the Mac and `C:/OpenRune/outputs/ai-overhaul-20261029`
on Windows. A client exiting successfully proves transport/lifecycle only, not
that it navigated or completed an objective. The final evidence must separately
record combat, Arena progression and HeadBall scoring.

Campaign runtime cases: DarkViking, GoblinWarrior and Dwarf, two seeds each; natural
weapon damage, kick/bash impact, shield absorption, reaction delay, disarm,
knockback handoff, Original toggle, boss/scripted guards, save/load and group
participation. Extra assertions confirm alternate authored attacks. Pure policy
checks cover reaction bounds, failed defenses, wounded reposition cooldown intent,
counter openings and group limits under sanitizers.

`Tools/RuneValidation/bot_goal.py` is an accelerated diagnostic: it grants a head
and starts a round, then requires the ordinary bot to reach an opening and score
using real input/physics. It uses isolated temporary configuration/saves and
returns failure if the original scoring zone does not award a goal. It does not
replace the full readiness/acquisition/match checks. `--upper-start` covers a
carrier starting on a goal roof. The native `--rune-bot-navigation-probe` fixture
reproduces the raised-geometry trap on DM-Hildir and requires normal movement to
escape. These fixture starting positions and granted inventory do not exist in
the production bot path.

## Build and validation checkpoint

The Windows source of truth is `C:/OpenRune/work/SurrealEngine`; the Mac controls
that machine and cross-compiles Android from a targeted source mirror. No public
server was started and no tablet update was installed during this work. Existing
user changes, original game assets and saved games were preserved. There were no
commits or pushes.

Final Windows executable SHA-256:
`738377d047ee78f2bcc34618be155cbf3437b73df2d9eb54a4bd385077b31abb`.
The four isolated headless matches below ran concurrently, four bots per match,
180 seconds each. They are gameplay tests, not rendering benchmarks.

| Check | Result and scope |
|---|---|
| Deathmatch, DM-Ruins, seed 17 | PASS. All four bots engaged; authoritative deaths and randomized appearances. Simulated 150 ms RTT, ±15 ms jitter, 3% loss and 2% duplication. |
| Team Deathmatch, DM-Hildir, seed 73 | PASS. All four bots engaged; final team scores 3–3. |
| Arena, AR-BladePit, seed 73 | PASS. Normal queue admission, fighting and resolved rounds; all four bots made combat requests. |
| HeadBall, HB-Agora, seed 73 | PASS. Both ready pumps, real start, combat/deaths, acquired heads, accepted throws and actual scores: red 4, blue 6. No granted heads or forced scoring in this match. |
| Goal-roof regression, same final executable | PASS. Fixture supplies a head and starts the round; normal bot navigates down and scores at 8.87 simulated seconds. |
| Raised-geometry navigation regression | PASS on v13. Ordinary movement escaped the trapped position; relevant navigation code unchanged in v16. |
| Native combat, final executable | PASS. Actual planner leap double-tap/charge-held-block admission, dual wield/cross-block, kick/bash/rage, mode protections, inventory/food, appearances, corpses and sever ownership. |
| Campaign tactical cases | PASS, DarkViking/GoblinWarrior/Dwarf × seeds 73/17. Relevant campaign production code unchanged since campaign-v5. Includes authored attack variants, damage, reaction delay, block/specials, disarm, knockback, Original toggle, save/load and group limits. |
| Bloodlust regression | PASS. Repeated leaps, charge impacts, shield reduction, walls/grates/stairs, enemy rage and cancellation/save guards. |
| Pure policy | PASS under address/undefined-behavior sanitizers. |
| Launcher | Private three-bot plus local-slot smoke passed. The stopped saved Windows snow-match launcher was backed up and switched to the final versioned executable, with both old stress-bot flags removed. It was not started. |
| Android | ARM64 engine and APK build/signing pass; previous signer and packaged discovery/driver/runtime assets retained. Not installed or tested on the tablet. |

Peak recorded bot-decision CPU time was 42.71 ms, including bounded path planning,
with debug logging and four concurrent matches. Decisions run at 10 Hz in separate
headless bot processes. This is not a frame-time/FPS guarantee or a 32-player
performance result.

Final signed Android artifact on the Mac:
`outputs/ai-overhaul-20261029/open-rune-ai-20261030-final.apk`, SHA-256
`eb3ae326f371145b0a0feb967c3fa3a72cb626bf9695ae78913a3fbf0559e0fa`.
Earlier candidate APKs and failed diagnostic runs are preserved as historical
artifacts; they are not the final passing checkpoint.

The saved Windows launcher now refers to
`build-windows/SurrealEngine-AI-20261030.exe`, matching the final Windows hash.
The regular freshly built `SurrealEngine.exe` contains the same changes. Source,
manifest and validation archives are outside Git in the evidence directory.

Human difficulty tuning, every-map navigation, specialist-creature redesign,
full campaign playthrough and large-player-count endurance remain outside this
checkpoint. Original assets, save formats, attack wind-ups and the 33 ms block
adjudication window were not changed.
