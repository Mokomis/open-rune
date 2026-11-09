# Open Rune: dual-wield prototype

November 4 pose adjustment: crossed-guard grip targets are four model units
higher in chest-local up, retaining the two-bone solve, weapon crossing angle
and existing blend. This shared pose applies to eligible player/NPC guards and
network replicas. The Mac build passes the existing dual runtime suite (including
20-to-7 chip damage, interpolation, swings, save/load and death loot); a native
2816×1762/requested4× normal-camera guard capture was reviewed. Fixed-camera
attempts pointed into map geometry and are excluded from visual evidence.
Evidence: `outputs/dual-guard-height-20261104`. Windows/Android are not updated.

October 27, 2026. Local play and server-authoritative private multiplayer for Ragnar and compatible Vikings.

## Equipment and controls

Gameplay and Enhancements / Combat share **Dual wield: Off / On**. With it on,
a compatible held weapon and a second compatible inventory weapon equip as a
pair. The actual second inventory actor is attached to the left hand. No
cosmetic duplicate is created. An owned shield stays in inventory, hidden and
inactive, until dual wielding is turned off or incompatible equipment is drawn.

The initial conservative weapon allowlist is Viking Short Sword, Roman Sword,
Hand Axe, Goblin Axe and Rusty Mace. Large swords, large hammers, special
non-stow items, bosses and incompatible skeletons are excluded.

Attack uses the original attack/recovery rhythm, alternating committed strikes
between hands. Returning to the ready position is not another strike. Block
raises both weapons into an X. No extra touch or controller button is required.
Kick remains available; shield bash requires a visible equipped shield and
cannot use the shield stored while dual wielding.

Compatible Vikings can use pairs from their inventory. For fresh Dark Viking
actors, a deterministic one-in-three actor-index selection grants one real Hand
Axe at first combat if the primary weapon is compatible. This does not run again
on save restoration. Other enemies, bosses and scripted scenes keep their
existing loadouts. The tactical controller uses a closer distance for the light
weapon pair and can choose crossed defense. Its usual reaction delay remains.

## Collision and defense

Both the visible skeleton and original frame sweeps use the mirrored pose and
the same left-hand grip transform. The grip is derived from the authored right
weapon attachment, preserving weapon geometry rather than mirroring the mesh.
The original weapon scripts still calculate damage and deduplicate victims.
Fractional frame interpolation is retained for both hands.

Crossed defense takes 35% of positive blunt/sever/bluntsever damage, rounded up,
and 30% of supplied momentum. It covers a front-facing cone (dot >= 0.5), takes
0.12 seconds to raise, and cannot protect during an attack, special move, pain,
cinematic or recovery. Nested inherited damage handlers apply the reduction
once. Rear attacks and other damage types retain original behavior.

A crossed kick still causes chip damage and a 24-unit swept recoil, followed by
0.3 seconds during which the guard cannot be raised. A shield remains stronger:
its existing blocked-kick response is unchanged. A shield bash can still stagger
a crossed guard. No new shield-bash attack is invented for two weapons.

The crossed pose uses two-bone arm constraints with the skeleton's own segment
lengths and smoothly blends into the current upper-body animation. Original
movement and lower-body animation remain in charge.

## Lifecycle and current scope

Switching, pickup, throwing, stowing, limb severing and death release the offhand
through the original attachment/inventory operations. A pending offhand swing
is canceled if the setting is disabled. Saved inventory and attachment links
restore the pair; loading cancels a pending left-hand attack instead of replaying
an untracked hit. Existing save formats and original game assets are unchanged.

[Multiplayer integration](NETWORK-NATIVE-COMBAT.md) now replicates the real
offhand, handedness and guard state under server authority. Multiplayer menus
say **Host rules**; clients do not run damage or equipment simulation. Mixed weapons
currently use the primary weapon's authored attack rhythm with the striking
weapon's own damage; bespoke paired combos and a dedicated offhand picker are
not implemented.

## Validation

`--rune-tactics-probe=dual` requires an isolated absolute `--save-directory`.
The desktop runtime fixture uses owned DM-Hildir assets and verifies:

- Alternating actual weapon sweeps: 20 damage from the right short sword and 30
  from the left axe in the controlled stationary-victim fixture.
- Fractional sampled positions for both hands.
- Front guard: a 20-point hit becomes 7; rear attacks do not receive protection.
- Reduced crossed-kick recoil and a recovery opening.
- A stored shield cannot enable shield bash; disabling dual wield restores it.
- Disabling during a left swing cancels the pending attack.
- Mid-left-swing save/load restores the same inventory item without replaying
  the attack, and does not grant another Viking weapon.
- Throw detachment, real tactical Viking attacks from both hands and death loot.

The original single-weapon tactical regressions separately pass for Dark Viking,
Goblin Warrior and Dwarf with dual wielding disabled. Laptop Vulkan rendering
at 4x MSAA was inspected from the front. The preview includes the existing
near-wall camera fade; it is not a new transparency effect.

Evidence is in `evidence/dual-wield/`. Headless simulation is not displayed FPS.

## Android validation

Installed on OPPO OPD2515, October 27. The ARM runtime combat suite passed, as
did real touch Attack (both weapons), crossed Block, and the shared Off/On menu
setting. All 197 preexisting save/backup files and preexisting preferences were
unchanged; the new dual-wield preference was added. The 144 Hz app rule remains.

A 33.41-second replenished-health combat sample at native 2520x1680 / 4x MSAA
presented 78.98 FPS, median interval 13.90 ms, p95 20.84 ms, with no interval over
50 ms. This is a bounded synthetic fight, not a 120/144 FPS claim or a comparison
against single-weapon combat. Original map effects and lighting remain enabled.

The user caught floating spare weapons in the first arena fixture. DM-Hildir
uses RuneMultiPlayer's respawn policy even in local play: SpawnCopy preserved the
world pickup for respawning. Both the fixture and first-combat enemy loadout grant
now set RespawnTime=0 before SpawnCopy. The fixture asserts that the granted actor
is the original item, preventing extra pickup copies. Authored map pickups retain
their original respawn behavior. Desktop combat regressions pass after this fix.


The corrected APK was installed and the actual touch/menu suite passed again in
the arena using RuneMultiPlayer rules, including the single-item grant assertion.
A fresh isolated dual-wield fight is paused for human testing without automated
attacks or health replenishment.

## Death loot, spare drawing and spin follow-up

The user reported upright corpse weapons, confusing stored weapons after throwing,
and an upper-body-only spin. All three are addressed in the October 27 follow-up:

- Falling physics now stops immediately if HitWall changes physics mode or destroys
  the actor. Previously, Weapon.Settling correctly reached its resting angle and
  changed to Pickup/PHYS_None; the same physics step then incorrectly dispatched
  Pickup's inherited Landed, zeroing pitch. A failing regression reproduced both
  corpse weapons upright; both now retain the original settling rotation.
- Local Ragnar automatically draws a remaining real inventory weapon after the
  throw/drop animation finishes, as requested. The previous offhand is preferred;
  the original retrieve animation is used. Intentional stowing, swimming, death
  and scripted scenes do not trigger an automatic draw. Stored-slot references are
  cleared when a weapon is equipped offhand or removed from inventory, and restored
  when the offhand is stowed. No fake belt/back duplicate is created.
- When the original scripts play the same attack on torso and legs, a left-hand
  strike mirrors that entire pose. Previously only group 1 was reflected, turning
  the torso against the original hips during spins. The spin animation remains;
  ordinary separate locomotion/upper-body animation remains separate.

Desktop and ARM runtime checks now include exact resting rotations, ownership
release, both corpse weapons recovered through the original Use/pickup animation,
automatic spare draw after Throw and Drop, no carried weapon or stored reference
after the last weapon is dropped, and reflected torso/hip matrices across authored
attack samples. Both real weapons still hit and both retain fractional animation
sampling. The three original single-weapon tactical regressions pass separately.

Rune's existing duplicate-class pickup restriction still applies: a corpse weapon
is a usable pickup, but Ragnar cannot pick up another of a class he already owns.
These are bounded local fixtures, not complete campaign coverage.

The corrected APK also passed the actual Android touch/menu suite at native
2520x1680 with 4x MSAA, followed by a real Throw tap, automatic axe draw and a
real Attack tap producing the axe sweep. A fresh isolated fight was left paused
without automated input or replenished health.

## Left-leg recovery follow-up — October 27

Repeated left-hand attacks exposed a one-frame lower-body reflection change:
the torso proxy entered idle while the legs were still in the authored attack
return. The whole-body mirror condition became false immediately, moving a toe
about 32.48 world units in one 120 Hz simulation step.

A 0.22-second recovery blend now returns the lower-body pose to its unmirrored
state. Full-body spin attacks retain their authored mirrored poses; damage,
cooldowns and attack timing are unchanged. The transient pose clock is updated
by local simulation and network-replica presentation, and survives snapshots
only for the same actor life/offhand identity. No wire-format change is needed.

The six-attack local regression measures maximum late-recovery ankle/toe
movement at 4.48 units per 120 Hz step, down from 32.48. Existing full-body spin,
physical weapon hits, fractional sampling, guard, save/load and death-loot
regressions pass. The existing native multiplayer combat regression passes
(5 snapshots, 837 ticks); this is not a new multiplayer visual-playthrough claim.
Full local logs are under `outputs/fire-effects/dual-motion-{before,after}.log`
and `network-motion.log`, outside Git.

### Practice fixture duplicate-axe correction

The rendered arena fixture previously granted a handaxe even when RuneMultiPlayer
had already supplied one. Starting that fixture while dual wield was disabled
left the new, unattached inventory item visible 100 units above the player.
The fixture now reuses owned weapons of the requested class and keeps newly
granted spare weapons hidden until attached. At frame 180 it asserts that every
visible inventory weapon is attached. The desktop rendered check reports
`handaxe0 reused=1`, `RUNE_DUAL_ATTACHMENTS_OK`, and bounded completion. This
changes diagnostic loadouts only, not authored map pickups.

The Android menu test also now distinguishes selector-dialog On/Off buttons
from similarly named controls behind the dialog. Its earlier failed re-enable
assertion was a test-target selection error, not an engine equip failure.

Android follow-up: real touch attacks from both weapons, Off/On re-equipping and
crossed blocking passed at native resolution / 4x MSAA after the harness selector
fix. The subsequent fixture-only APK update passed its on-device reuse and
visible-attachment assertions. The final motion recording/FPS measurement was
stopped at 2% battery; the numerical recovery result above remains the laptop
regression, not a new Android motion-video claim. A fresh isolated manual session
was opened without automated attacks at saved Hardware Scaling 50% / 4x MSAA.
Pause was requested, but ADB disconnected before confirmation. All saved
preferences match the pre-test snapshot.
