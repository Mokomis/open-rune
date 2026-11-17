# Open Rune ideas

Future concepts requested during playtesting. These are not implementation commitments or changes to normal gameplay.

## More simultaneous shadow lights

Implemented November 2 on [Windows](WINDOWS-SHADOW-LIGHTS.md) and subsequently [Android](ANDROID-SHADOW-LIGHTS.md): one/three/four/six/eight nearby shadow-casting lights. Existing user selections are preserved; Mac still has three slots. Both platforms pass eight-slot GPU readback checks, and real-map comparisons exercise four eligible sources. Broader sustained gameplay performance is not established.

## Climb Anywhere

Implemented as **Spider Monkeys** (November9,2026), a separate Law of Battle. See [wall climbing](SPIDER-MONKEYS.md) for controls, original-animation approach, surface exclusions, bot planning and validation scope.

## Limb Rebellion

Added October 28, 2026. Optional game mode in which severed limbs move independently and attack every player except their original owner. Keep separate from fixing unintended limb-following in normal play.


## Visible breath in cold conditions (idea, November 9, 2026)

Living creatures, including players and bots, exhale visible breath clouds in cold conditions. Undead do not. This is a cosmetic idea, not implemented yet. Use creature identity/state rather than skin appearance to distinguish undead, and share the existing particle budget to keep the cost low.


## Ocean foam (deferred, November 9, 2026)

Revisit foam for ocean scenes later. The user explicitly deferred this until after the current physics work; do not begin a foam pass now. The existing native Crest compression foam remains unchanged.
