# Bot defense, special moves and Rune Power — October 31, 2026

## Behavior

The optional **Leap & rush without bloodlust** switch appears in Gameplay and
Enhancements → Combat tactics & moves on Android, and in Gameplay and
Enhancements on Windows. It defaults Off. With it On, eligible players and enemies
can use giant leap and shield rush outside bloodlust without spending meter. The
shared four-second cooldown, double-jump gesture, shield/pose requirement for
rush, wind-up, collision and recovery still apply. During active bloodlust with
sufficient energy, the original half-meter cost remains. The master Bloodlust
moves switch still gates the feature. Rush is unavailable while dual wielding.

For multiplayer this is a host rule chosen before starting a match. Both platform
host launchers pass it to the separate authoritative process. A joining client's
local preference cannot grant readiness or permit an illegal move: readiness is
replicated from the server and every action is validated there. The wire protocol
is now version 21 because older clients assumed readiness always required active
bloodlust and would reject a valid zero-meter snapshot. Update host and clients
together. No combat timing or damage rollback policy changed.

Bots recognize attacks from both held weapons, use short prepared guards as well
as reactive blocks, and can hold the crossed-weapon guard. Shield-bash decisions
use the real contact distance rather than requesting a bash from outside its
reach. Bots can seek visible useful power runes/refills and activate the main-hand
weapon's original power when they have enough energy and a suitable opportunity.
Original pickup queries, weapon costs, animation requirements and powered effects
remain authoritative; there are no free starting runes in ordinary matches. A
capacity rune remains useful at capacity 200 when energy is depleted.

Android multiplayer previously omitted Rune Power entirely from its HUD values.
It now supplies the availability bit, current energy and capacity. Windows' match
status also displays current/capacity. Campaign HUD population and the existing
Rune Power touch/controller/key action remain in use. Some shipped multiplayer
maps have no power runes: the inspected AR-BladePit and HB-Agora starts each have
zero, whereas DM-Hildir and DM-Ruins each have two. A meter at zero on those
rune-free maps is not an activation failure.

## Defense ordering correction

Reliable RaiseShield/LowerShield can arrive before or after their complete held
input. Previously a dual-wield bot's accepted raise could disappear on the next
VM tick because bAltFire still contained the older input. The authority now keeps
a separate guard-input watermark. A fresh reliable press/release updates held
block immediately; older datagrams/actions cannot undo a newer live state. This
does not acknowledge movement that has not been simulated. Historical defense
records still resolve contacts within the existing 33 ms / two-tick window.

## Verification and limits

All tests use owned original assets and isolated saves/configurations. Evidence
is under `outputs/combat-completeness-20261031`; shared-source Windows evidence
is mirrored from `C:/OpenRune/outputs/combat-completeness-20261031`.

- A failing-before/fixed-after planner fixture catches guard disappearing before
  its input datagram. Additional cases deliver press, release and held datagrams
  out of order. The real bot chooses the guard; the crossed pose reaches a wire
  round-trip and replica, and crossed contact still reduces rather than removes
  damage (20 becomes 10 in the fixture).
- Real planner commands produce shield bash with visible wind-up and physical
  recoil against a guard. Existing unblocked bash checks verify sound, damage,
  stagger and matching replica skeletons.
- The real planner performs leap and rush with full bloodlust and with zero
  bloodlust under the new rule; authority admission, gesture context, cooldown,
  meter preservation, shield requirements, recovery and disabling the rule are
  checked. A native fixture provides equipment/positions/availability only.
- Real rune Touch/SpawnCopy/PickupFunction fills energy, a player and bot each
  activate a powered weapon through the authority, cost/effect/meter reach the
  replica, and a maximum-capacity rune still refills energy. The complete native
  suite passes on both Windows and the tablet's ARM executable (430 snapshots,
  4,093 ticks). ARM headless checks are not FPS measurements.
- The 12 real-engine melee-window cases and five focused network tests pass,
  including independent wire oracles and the unchanged defensive grace window.
- Installed Android graphics/input checks use 2520×1680, scale 1.0 and four samples.
  Actual touch Rune Power spends 25 energy (120 → 95). Campaign pickup/activation/
  refill logs verify 100 → 120 capacity, 120 → 95 energy, then refill to 120.
  The visible meter and Rune Power button were inspected. A private Windows-host /
  Android-client session displays 0/100 power and admits a touch double-jump leap
  at zero bloodlust under the host rule. These are short correctness checks, not
  a sustained combat-performance or campaign-completion claim.
- Gameplay and Enhancements expose the same saved switch; toggling through one
  is reflected in the other. Settings persistence/reset and host-argument tests
  pass. The new rule was restored Off, and all pre-existing Android preferences
  were preserved. Test equipment/energy never enters ordinary saved games.

Full-match observations and any remaining navigation limitations are recorded in
the accompanying checkpoint/report. Bot telemetry counts requested decisions;
accepted actions/contact/effect are separately checked by the controlled native
fixtures and host logs. Do not equate transport success with every bot finding a
fight, or an opening/map check with a completed playthrough.

## Final match observations

The final normal eight-bot Team Deathmatch on DM-Hildir runs for 240 seconds with
all eight bots attacking. It records 113 attack requests, eight bashes, seventeen
crossed guards and seven power activations. The impaired eight-bot run (150 ms RTT,
15 ms jitter, 3% loss, 2% duplicates) completes transport and combat correctly,
but one bot only defends and continues roaming without attacking during those
240 seconds. Its strict every-bot-attacked check remains failed. Its log shows
continued navigation rather than the fixed stationary overhang/seam stall; this
is a remaining encounter/navigation limitation, not a claim that all AI routing
is finished. Earlier DM, Arena and HeadBall mode smoke checks also pass; only
one representative map per mode was used.

With the normal-combat rule enabled, an impaired four-bot DM run produces twelve
leap requests; the native tests separately prove actual admitted rush and leap
execution/contact. Not every randomly equipped bot has the shield and animation
support needed to rush. A failed every-bot-attacked check in that earlier run is
retained in evidence, rather than relabeled as a complete AI pass.

Final Android APK SHA-256:
`21cee4514a017f95f8d55d7516a3cd4c60e1bc0477f43a331752646ce3699d0c`.
Windows `SurrealEngine-CombatAI-20261031.exe` SHA-256:
`d9a7cb46d155c294227f647f0c683c526e2e96af45c8226e792afc119a09f79d`.
The installed APK hash matches the packaged file. Stopped Windows browser/match
launchers point at this build; no public match was started. Original assets,
ordinary saves, previous binaries and pre-existing Android settings were kept.
No commits or pushes were made.

## Discovery follow-up

The first public host attempt exposed a missed discovery-runtime version bump:
its native game protocol was 21 while the JavaScript runtime/topic still expected
20. Private native/direct-invitation tests did not exercise this public gate.
`Tools/RuneDiscovery/protocol.cjs` now uses 21 and derives its topic from that
constant. A regression compares it to the native header. All five discovery tests
pass on the Mac source mirror and Windows, and a fresh public DHT search verifies
the actual Windows match with seven occupied slots / one available slot. The
matching Android bundle/APK is prepared; deployment status is recorded separately
in `outputs/ai-team-match-20261031-normal-moves/session.json`. The earlier APK hash
above remains the previous installation, not proof of this follow-up's deployment.

The first Android protocol-21 bundle rebuild also exposed an external-dependency
path packaging failure in Bare. The corrected installed build and subsequent
menu changes are documented in `outputs/android-discovery-menu-fix-20261031`.
Find games was verified on the tablet, including reopening and refreshing the
actual Windows listing. Packaging now rejects escaping module IDs and stale
protocol metadata. Primary-action button styling no longer resembles a stuck
controller selection, and browsing is not gated on the campaign world loading.
