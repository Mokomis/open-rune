# Open Rune — Shield bash

Double-tap **Block** within 0.3 seconds while holding a usable shield. The first
press blocks immediately; holding the button does not repeatedly bash. The move
is also available to compatible shield-bearing humanoid enemies. Gameplay and
Enhancements share the default-on **Shield bash** switch. Disabling it leaves
ordinary blocking and the independent kick setting available.

## Implementation and balance

The original model's exact `H3_DefendAttack` sequence supplies the shield strike.
The shield reaches furthest forward at frame 1 of 8. Impact occurs at that frame,
after a 0.22-second transition and approximately 0.061 seconds of motion. Total
commitment is about 0.65 seconds. The original Uninterrupted state owns recovery;
body and animation proxy use the same timing. Original weapon-swipe notifications
are suppressed only for this move and its stagger animation. Existing game
packages/scripts and animations are unchanged; no mod assets or code were used.

An unfinished commented-out Shield.Bashing stub exists in the original script;
it is not enabled or copied. New integration provides:

- Four-second cooldown for player and enemy, spent even on a miss.
- Two base blunt damage, subject to the original difficulty rules.
- A small, collision-swept horizontal shove: 20 units at standard mass over
  0.16 seconds. No upward launch impulse.
- Roughly 0.35 seconds of stagger using the original `S1_painFront` animation.
  It interrupts an ordinary weapon attack and returns to the original combat
  state. Stagger protection lasts two seconds from impact.
- A raised, facing shield prevents damage/stagger and reduces recoil to six
  units. A lowered shield or rear-facing shield does not counter the bash.
- Actors marked bIsBoss or heavier than 200 mass receive reduced recoil and no
  forced stagger. Targets without the required pain animation are not forced
  into an unsupported animation.

The attacker must be in normal grounded combat, possess an intact owned shield,
and be free of an active weapon attack, kick, bash or stagger. Impact rechecks
reach, facing cone, world visibility and the target's normal combat state.
Scripted scenes, death, climbing and unrelated actor states are not interrupted.
The shield is lowered during the committed bash, leaving a counterattack window.
An AI observer's existing defend/dodge logic recognizes the wind-up.

The input detector follows the resolved `bAltFire` button binding, including the
Android Block button and keyboard Right Ctrl. It counts press/release cycles,
not key repeats. Pausing clears its pending double-tap. Cooldown, pending impact,
and stagger immunity are transient across save/load; save formats are unchanged.
Loading an action cancels its pending native hit while the saved animation can
finish normally. Mid-action restoration is not separately validated. Multiplayer
replication is not implemented.

## Validation

Evidence is retained in [evidence/shield-bash](evidence/shield-bash). The controlled
fixtures isolate actors and pause ordinary AI; actual touch double-taps start
player bashes, and an actual held Block counters enemy bashes. A separate encounter
keeps original AI enabled and replenishes health for observation. These fixtures
are not a complete campaign test or proof for every humanoid model.

Exact sequence checks identify Ragnar, DarkViking, Conrack and Sark as compatible;
GoblinWarrior and SnowBeast do not gain this move. Real impacts are exercised with
Ragnar and DarkViking. Short displayed-FPS measurements use SurfaceFlinger at
native 2520×1680 with 4× MSAA, not the engine FPS counter.

Final installed-build controlled results:

| Case | Health | Displacement | Staggers | Duration |
| --- | --- | --- | --- | --- |
| player-unblocked | 100→98 | 20.00 units | 1 | 0.358s |
| player-blocked | 100→100 | 6.00 units | 0 | 0.000s |
| mob-unblocked | 100→97 | 20.00 units | 1 | 0.358s |
| mob-blocked | 100→100 | 6.00 units | 0 | 0.000s |
| player-resistant | 100→98 | 6.00 units | 0 | 0.000s |
| player-heavy | 100→98 | 1.71 units | 0 | 0.000s |
| player-immune | 100→98 | 20.00 units | 0 | 0.000s |

Every controlled hit occurred exactly once. Duplicate starts and cooldown retries
were rejected. No-shield and disabled starts were rejected. Facing, rear and
lowered-shield guards passed; held Block activated ordinary defense without
bashing. Both attackers and victims recovered from the committed animation.

The final autonomous encounter produced three DarkViking bash starts and three
hits. Its original AI remained active; fixture health replenishment prevented
an early death. SurfaceFlinger measured **118.67 displayed FPS over 14.33 seconds**,
median 8.334ms, p95 8.334ms, maximum 16.668ms, and zero gaps over 50ms. The hardware
reported 120Hz. Saved visual effects were enabled. This short arena sample does
not establish sustained120FPS or isolate a performance change from the earlier
kick sample.

Installed APK SHA-256:
`9955995b46aaa12ac4da202368d99f36e81bd6a8c20e87b6c78e71f63c47f00f`.
The installed APK matches the packaged bytes and uses the existing signing key
and package identity. The Rune-only OPPO refresh rule remains `3`.

The final APK also passes shared-menu toggle persistence, disabled-bash behavior,
and re-enabling through touch. Kicks (unblocked and shield-blocked), actual touch
Throw/Use weapon recovery and food consumption, ordinary weapon combat, held
shield protection through shield breakage, and isolated campaign save/load pass.
The desktop headless model/native/animation suite passes. A physical controller
and every humanoid variant were not separately tested.

All ten normal/campaign saved-file hashes are unchanged. Original preferences
were restored byte-for-byte after testing. The tablet is left at the original
campaign-village-ledge title menu, native resolution with 4× MSAA, with Shield
bash enabled by default. Source and evidence stay in the private fork.
