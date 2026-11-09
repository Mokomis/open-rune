# Open Rune — tactical humanoid combat

October 30 follow-up: [AI overhaul](AI-OVERHAUL.md) adds authored attack variation, wounded repositioning and observed recovery opportunities, and includes kick/bash/bloodlust moves in group pressure limits. Normal multiplayer bots now use a separate authenticated-input planner and random characters/skins. See the follow-up for the current mode checks, build artifacts and scope; the October 27 notes below describe the first implementation.

October 27, 2026. First tactical combat implementation; original packages and
recovered scripts are unchanged. This is a bounded combat improvement, not a
replacement for all campaign AI or proof that every encounter works.

## Controls and scope

**Gameplay → Enemy tactics → Original / Tactical** selects the behavior at
runtime; Tactical is the new default. Enhancements → **Combat tactics & moves**
contains the same setting plus the existing kick and shield-bash switches.
Android saves one shared preference. Desktop comparison uses `--rune-tactics=0`
or `--rune-tactics=1`.

Original restores the original combat decisions; the independent kick and bash
switches still control those previously added moves.

The controller operates only on authoritative, living, walking ScriptPawn
enemies in the Viking, Goblin and Dwarf families, during their ordinary Fighting
state and with a supported attack animation. Bosses, player characters, network
replicas, stopped script frames, cinematics and other scripted states retain
their existing behavior. Unarmed Vikings/dwarves return to the original
equipment behavior. Goblins retain their authored unarmed swipe.

The original state machine still handles acquisition, navigation, searching,
pain, death, scripted orders and transitions. The native controller suspends
only the eligible Fighting state body and its combat Timer. Other events,
physics, skeletal interpolation and animation notifications continue normally.

## Combat decisions

- Approach to the actor's melee reach with shorter final steps for short-range
  creatures. Movement checks the swept path, ground support and destination
  zone; the normal walking collision solver performs the actual movement.
- React to visible swings and special-move wind-ups after a delay. A defense
  roll is made once per observed threat, so a failed roll is not retried every
  frame until it succeeds. No input buttons or hidden player commands are read.
- Commit to an authored weapon strike at its original animation rate and facing,
  then recover. Ordinary damage/pain/death can still interrupt it. This first
  pass uses the basic authored attack, not a new combo system.
- Use an intact shield to block; otherwise attempt a safe retreat. A held shield
  has a finite defensive window and a recovery opening.
- Compatible enemies kick to make room at very close range and bash a visible
  held guard. Both use the existing move implementations, animation impacts,
  toggles and cooldowns. A raised facing shield still prevents the full impact.
  Creatures without the required animations do not acquire these moves.
- Nearby tactical fighters limit concurrent committed attacks, flank around
  blocked lanes, and periodically step aside during recovery. Recovery steps
  do not shorten attack recovery or teleport actors.

Decisions run at approximately 10 Hz, staggered when control is acquired.
Animation and physics still update at their normal rates. Local coordination
examines at most 64 pawn-list entries per decision, not the complete actor list
each rendered frame. On loss of visibility the controller stops tracking the
live target position and returns to the original hunting behavior.

| Saved campaign difficulty | Minimum reaction | Weapon recovery | Defense success roll | Nearby committed attackers |
| --- | ---: | ---: | ---: | ---: |
| Easy | 0.42 s | 0.70 s | 45% | 1 |
| Medium | 0.30 s | 0.52 s | 65% | 2 |
| Hard | 0.22 s | 0.40 s | 80% | 2 |
| Very hard (existing Open Rune menu option) | 0.18 s | 0.35 s | 85% | 2 |

Observation cadence, availability of a shield, position and an existing committed
action can make the actual reaction later. These probabilities apply to an
observed threat, not every attack in the game. **Existing health, damage, food
and other numeric difficulty modifiers are unchanged in this pass.**

## Lifecycle and compatibility

Tactical state is transient and owned by each pawn. Save/load does not serialize
new decisions or native pending actions and does not change the save format.
Loaded combat resumes through the original animation/state paths before native
control can be reacquired; exact mid-swing trajectory continuity is not claimed.
Turning Tactical off lets an already committed native weapon animation finish,
then restarts the original Fighting state. A scripted state change always takes
priority. Existing network replicas never run this controller; this change does
not introduce multiplayer bots or a new network protocol.

## Validation

`Tools/RuneValidation/tactical_ai.py` runs isolated, bounded engine simulations
on DM-Hildir with the owned game assets. Its JSON summary records the executable
hash, seed, per-case result and wall time. Simulation runs at 120 Hz; this is not
measured display FPS. Slot 94 and automatic checkpoints stay in temporary save
directories. The fixture replenishes health where needed to sustain combat.

The scenarios cover natural weapon hits for DarkViking, GoblinWarrior and Dwarf;
Viking kick and blocked bash impacts; controlled visible-cue reaction timing;
boss/player/replica/script-state exclusions; disarming and Original-mode return;
loading an isolated save during combat; and three fighters taking part while
respecting the two-attacker limit. The defense timing test injects an observable
attack cue and fixes its success roll; it is not a human combat playtest.

`tactics_policy.cpp` separately checks perception, reaction delays, mistakes,
spacing, cooldown availability, shielding and coordination decisions under
AddressSanitizer/UndefinedBehaviorSanitizer. `MenuSettingsTest.java` checks that
tactics persistence/reset does not alter difficulty. The new native translation
units are also syntax-checked with networking disabled.

A separate rendered laptop encounter uses native 1× rendering and 4× MSAA with
isolated configuration/saves. The screenshot is outside Git under
`outputs/tactical-ai-preview/`; logs are in `evidence/tactical-ai/`. It is a visual
and runtime check, not OPPO performance evidence. No tablet installation or
Android display-FPS result is established by these laptop checks.

Remaining scope includes human tuning of combat feel, authored combo selection,
broader species behavior and campaign encounter coverage. Do not describe this
as a complete AI rewrite or a replacement of difficulty stat scaling.

### Installed Android validation — October 27

The signed APK was installed in place on OPPO OPD2515; its installed SHA-256
matches `ccbd63125e5e35ee1a8504ee7a22f8143a4379c0b0c3fa387e5ae5d046d96a1e`.
All six standalone ARM64 runtime cases passed on the tablet (seeds 73 and 17,
DarkViking/GoblinWarrior/Dwarf). These include kicks, shield-blocked bashes,
0.392–0.450-second controlled defensive responses, preserved kick momentum,
save/load recovery, and three participating fighters with at most two committed
attackers. The initial probe invocation completed its combat assertions but
failed to write its shutdown log because its isolated configuration directory
was missing; creating that directory resolved the harness error. No production
code change was required.

Rendered real-time encounters used native **2520×1680, 4× MSAA**, saved visual
effects and texture selection, and the existing 144 FPS target / 144 Hz OPPO
policy. USB was connected. SurfaceFlinger timestamps, rather than the HUD's
engine counter, produced these results:

| Encounter | Seconds | Displayed FPS | p95 interval | Longest interval | Gaps >50 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Tactical DarkViking | 99.84 | 93.00 | 13.89 ms | 27.77 ms | 0 |
| Original DarkViking | 42.57 | 88.94 | 13.89 ms | 41.67 ms | 0 |
| Tactical GoblinWarrior | 31.54 | 85.66 | 13.89 ms | 62.51 ms | 1 |
| Tactical Dwarf | 31.51 | 85.75 | 13.89 ms | 55.57 ms | 1 |

These are separate evolving encounters, not identical-workload benchmarks or a
claim that Tactical increases FPS. The original comparison suggests no obvious
large tactical-AI penalty in this scene. The shorter goblin/dwarf samples each
include one long interval. APK/probe staging and save verification overlapped
portions of the session; no GPU clocks or thermal settings were changed.
This does not establish locked 120/144 FPS or full-campaign coverage.

All three tactical rendered encounters recorded continuing attacks and player
damage. Endurance mode replenishes health/durability, so these are workload and
behavior checks, not survival/balance results. The on-device Enhancements combat
page was visually reviewed; switching Original restored original fighting,
and switching back persisted Tactical across restarts and appeared in Gameplay.
Existing preferences were unchanged; only `menu.tactics=1` was newly saved.
All 191 pre-existing save/backup hashes (including prior isolated fixtures)
matched after the tests. The per-app refresh rule remains 4 (144 Hz).

Raw device evidence is in `evidence/tactical-ai/android/`; screenshots and local
backups are outside Git in `outputs/tactical-ai-android/`.

A separate 3,600-frame armed encounter in DM-Hildir's default game mode recorded
930 cumulative player damage and 210 enemy damage with health replenishment;
the Viking's tactical bash hit the raised shield with `blocked=1`. The earlier
single-player-mode attack fixture started Ragnar unarmed, so it establishes
shielded defense but is not evidence of player weapon attacks. The final handoff
is a paused, isolated `tactical-ai-practice` encounter with no automated inputs
or endurance replenishment, using native resolution and 4× AA.

### Earlier laptop build checks

The final build passed all six runtime cases (three enemy classes, seeds 17 and
73), policy sanitizer tests, menu preference tests and networking-disabled syntax
checks. Both Viking runs recorded a 0.392-second response in the controlled
defense fixture, all three group members participating with at most two committed
attackers, and preserved incoming kick velocity of 440 units/second when tactical
control released. Save/load and shield-blocked bash checks passed.

The final laptop capture is 2048×1492 at native scale with 4× MSAA and was visually
reviewed. Desktop and Android ARM64 builds completed; the APK's v3 signature was
verified. `evidence/tactical-ai/build-validation.json` records final executable
and APK hashes and validation limits. That record predates the Android installation
and device results above.


### November 4 navigation follow-up (candidate, not deployed)

Native wall recovery used one-unit trial/destination offsets, and normal point
movement could finish before a capsule cleared a corner. Rune walking NPCs now
use body-sized, collision-checked side steps and precise point arrival. Jumping
requires a demonstrated low obstacle, headroom, landing support and bCanJump.
The original Engine.dll PickWallAdjust export was inspected for its clearance
contract; this is a scoped walking correction, not a replacement for its flying
or swimming branches. Pawn pursuit and player movement keep their arrival rules.
Tactical walking now uses full-height clearance and releases to original Charging
when intended movement makes no actual progress; committed moves and knockback
remain protected.

Hildir has disconnected authored route groups. LostCrypt has navigation actors
but no linked NavigationPointList. This does not prove every absent route is a
bug: some require doors, lifts or jumps. Spawned ScriptPawns and multiplayer
input bots have different pursuit layers; RuneBot already has supplemental
routing. A shared ScriptPawn fallback tries bounded, incremental local walking
search only after the original actor route fails. Original target acquisition,
HomeBase/HuntDistance, scripted states and door/lift behavior remain responsible
for gameplay. It follows LastSeenPos when its enemy is hidden. No teleportation,
collision exemptions or fabricated jump/lift links are introduced.

Searches share a cooperative 2 ms engine-tick budget (checked periodically, not
a hard scheduling deadline), with node/range limits and retry backoff. Private
hidden movement-goal actors preserve the existing MoveToward/save contract;
source destruction cleans them up. Protocol/ALPN25 and snapshot schema7 stay
unchanged; native rules29 require matching hosts/clients when deployed.

Mac isolated navigation regression passes 12 wall cases, five existing pursuit
routes, six additional room cases, the stuck-plan watchdog and save/reload/goal
cleanup. Six Viking/Goblin/Dwarf tactical cases across seeds17/73 also pass.
These are accelerated real-map simulations, not rendered FPS or all-map proof.
Evidence: outputs/ai-navigation-20261104/final-navigation and tactics-local.
Android ARM builds; installation and rendered device checks are pending. Windows candidate build, physics-world checks and the same five navigation
regression groups pass; installed clients remain on the preceding version. The all44-multiplayer-map graph inventory is complete; bounded Qwen review
is complete, reviewed and stopped. See [the verified repairs and limits](NAVIGATION-AUDIT.md). See
Tools/RuneWorker/Reference/20261104 and outputs/navigation-worker-20261104.
This is static triage, not confirmation of44 playable navigation routes. Preserve worker reports; static warnings require runtime confirmation.
