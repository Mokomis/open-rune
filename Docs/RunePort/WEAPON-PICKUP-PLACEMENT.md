# Runtime weapon pickup placement

Runtime-spawned weapons can inherit Pickup with PHYS_None. Sleeping/respawning
also returns to that state without necessarily restoring gravity. Summoning
them above the ground therefore creates a motionless suspended weapon, often
accompanied by the original purple RespawnFire effect.

Rune now distinguishes actors spawned during play from authored map actors.
When a runtime weapon becomes an unowned visible Pickup with no physics, a
single support check decides whether to resume ordinary falling and Landed
handling. Ownership, hiding and sleeping reset the check for its next appearance.
Held/carried weapons, thrown weapons already using physics, replicas, stabbed
weapons and authored map placements retain their original handling. No constant
floor trace is added for settled weapons. Transient lifecycle flags are not saved;
the resulting real positions and physics use the existing save/network format.

Mac and Windows single/network/team fixtures verify unsupported spawned weapons
settle, respawns settle again, and authored placements remain unchanged. Existing
corpse/weapon ownership regressions also pass on Windows. This targets the
reproduced unsupported-spawn case; a weapon that still floats while attached or
after an unrelated detachment needs its own evidence rather than being assumed
to have this cause.

Evidence: `outputs/visibility-slam-weapons-20261103`. The native fixture is
`--rune-network-probe --rune-corpse-probe=single --rune-slam-probe`, with isolated
saves and configuration. Its log reports `RUNE_PICKUP_PLACEMENT_OK` separately
from slam/combat assertions.
