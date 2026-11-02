# Open Rune — Combat kick (October 25, 2026)

Ragnar and compatible humanoid enemies can kick. **Kick** on touch, **F** on a
keyboard or **right-stick click** on Android starts the move. **Combat kicks**
in Gameplay and Enhancements is one shared, persistent setting, enabled by
default. Disabling it disables player and AI kicks and cancels pending damage.
The original Use interaction remains separate.

## Combat behavior

The actor must be alive, walking, free of an attack/proxy action and in the
normal walking/player or fighting/enemy state. The original `neutral_kick`
animation and its `UseNotify` determine the hit instant. The original
`Uninterrupted` state controls animation commitment and recovery. Body/proxy
notifications produce only one hit. No original packages, scripts or assets
were edited; no mod code or animation was imported.

At impact, the nearest eligible pawn must still be within reach, inside the
forward cone and visible through the world collision trace. NPCs target only
their current enemy. Cinematic player targets are excluded. A miss consumes the
attempt. Cooldowns are 2.2 seconds for Ragnar and 4.5 seconds for enemies; an
impacted target has 1.2 seconds of protection from another kick impact.

- Unblocked: five base blunt damage through original JointDamaged, then original
  AddVelocity with 440 horizontal / 110 vertical impulse. Original difficulty
  adjustments still apply. Mass scales impulse between 0.25 and 1.25.
- Blocked: a living shield must be Active (raised) and face the attacker. There
  is no health or shield-durability loss. A small swept horizontal movement of
  six units at standard mass over 0.12 seconds and the shield's original impact
  sound replace the full impulse. No forced airborne state is introduced.
- Enemies consider kicks occasionally at close range, between weapon actions.
  Their existing InDangerFromAttack logic also recognizes a pending enemy kick,
  allowing the original defend/dodge logic to respond.

Compatibility requires an **exact** `neutral_kick` sequence with its original
UseNotify. Generic GetSequence silently falls back to another sequence and must
not be used as evidence that a model supports kicking. Asset probes identify
Ragnar, DarkViking, Conrack and Sark as compatible; GoblinWarrior and SnowBeast
are not. They can still be victims. This is not proof that every humanoid class
has a suitable animation or that every AI variant has been playtested.

## Validation and limits

The installed ARM64 build was tested on OPPO OPD2515 at native 2520×1680 with
4× MSAA. `android_kicks.py` uses actual Android touch input for player kicks and
shield holds, with isolated fixtures for controlled enemy impacts. The enemy
fixture pauses ordinary AI; separate live combat observes autonomous decisions.

Four controlled cases passed in both directions: one impact, damage/full shove
without a shield, no damage/small recoil with a raised shield, and recovery to
PlayerWalking/Fighting. The final fixture also checks front versus rear shield
coverage, lowered shields, disabled starts, duplicate-start rejection and
original AI threat recognition. Android R3 event injection passed; a physical
controller was not used for that check.

Runtime state (pending hit, cooldown, recoil) is transient and does not alter the
save format. Loading mid-kick cancels the pending native hit; original animation
state can finish normally. Mid-kick save restoration has not been separately
validated. Existing isolated campaign save/load and ordinary pickup checks are
recorded with the evidence below. Multiplayer replication is not implemented.

Final installed-build results:

| Check | Result |
| --- | --- |
| Ragnar → enemy, unblocked | 100→95 health, 83.82 units maximum displacement |
| Ragnar → enemy, blocked | 100→100 health, 6.00 units |
| Enemy → Ragnar, unblocked | 100→93 health, 99.53 units |
| Enemy → Ragnar, blocked | 100→100 health, 6.00 units |
| Android right-stick event | One player hit, 100→95 health, normal recovery |
| Shared menu setting | Both tabs agree; Off persists after restart, disables touch/R3; On restores kick |
| Touch Throw/Use | Axe recovered; food consumed |
| Isolated save-flow/resume | Both pass |
| Autonomous DarkViking encounter | Four kick starts, three hits; normal weapon combat continues |
| Native 4× displayed FPS | 119.59 over 14.28 s; p95 8.333 ms, max 16.665 ms, no gaps over 50 ms |
| Preservation | All 10 ordinary/campaign save hashes unchanged; preferences restored byte-for-byte; Rune refresh rule remains 3 |

FPS used SurfaceFlinger presentation timestamps; hardware reported 120 Hz.
Bloom/color and the user's saved detail settings were enabled for that final
encounter. Endurance kept combatants alive for observation; this is a short
arena sample, not a controlled performance comparison or campaign benchmark.
Final APK SHA-256: `9480d27dfe77f6a19d7a9d0fc2dac4e1f0d87619e26b4f8d99428ccc11110042`,
matching the installed APK. The tablet was returned to the campaign-village-ledge
title menu, native 4×, with Combat kicks enabled by its default.

Evidence: [evidence/kick](evidence/kick). Performance results and the final
installed-build identity are recorded there. Short encounter samples are not
full-campaign or sustained-120-FPS proof. Ordinary campaign saves and unrelated
preferences must remain unchanged. Keep this implementation in the private fork.
