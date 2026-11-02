# Open Rune — Gib and severed-limb physics audit

October 25, 2026. Measurement only; production physics behavior is unchanged.

## Method

`Tools/RuneValidation/android_gibs.py` launches native 2520×1680 with 4× MSAA on OPD2515,
with `--ez gibProbe true` and an isolated save directory. `RuneGibProbe.h` runs 24
cases in DM-Hildir: GibRedMedium, GoblinBHead, goblinlarm and goblinrarm, each
released from rest or tossed at (100,0,240), each with controlled 1/30, 1/60, 1/120
second actor ticks for 8 simulated seconds. This is a timestep comparison using
the installed ARM64 engine, not a 30/60/120 display-FPS benchmark.

The real level geometry, original script states, production physics and collision
are used. Other actors stay frozen during the synchronous test. Actor contacts
are disabled to isolate world contacts and avoid accidental pickups. Initial
location/rotation/spin/velocity match; a scoped random seed restores the prior
random-generator state after each case. Spawned fixture actors are destroyed
after each case. Collision radii differ by class, so arms can land on the nearby
structure while smaller gibs fall to the floor. Compare rates within a class,
not absolute landing positions between different classes.

Two separate runs produced identical result rows. Evidence:
[evidence/gib-physics](evidence/gib-physics). All 24 cases settled; all 18 limb/head
cases reached Pickup. No nonfinite motion occurred. The final velocity field can
remain nonzero after a script selects PHYS_None: that is stored velocity, not
continued movement. HitWall callback counts include nested script calls and
Landed forwarding; they must not be labeled distinct physical bounce counts.

## Measured results

Times below are when the actor first selected PHYS_None, sampled at the selected
outer step boundary. Thus some of the variation is sampling granularity.

| Object / action | Settle at 30 | At 60 | At 120 | Maximum final-position spread |
| --- | ---: | ---: | ---: | ---: |
| Red gib / drop | 0.967s | 0.950s | 0.908s | 0.000 units |
| Red gib / toss | 1.200s | 1.133s | 1.133s | 0.790 units |
| Goblin head / drop | 1.733s | 1.717s | 1.608s | 0.000 units |
| Goblin head / toss | 1.367s | 1.417s | 1.417s | 1.420 units |
| Left arm / drop | 0.800s | 0.750s | 0.775s | 0.000 units |
| Left arm / toss | 2.200s | 2.167s | 2.150s | 2.192 units |
| Right arm / drop | 0.800s | 0.750s | 0.775s | 0.000 units |
| Right arm / toss | 2.200s | 2.167s | 2.150s | 2.192 units |

Toss apex differs by about 1.04 units between 30 and 120. Landing time, intermediate
trajectory, contact callbacks and settling are not exactly rate independent.
No major failure appeared in this bounded scene; it is not proof for every
terrain, high-speed impact, water zone, moving platform or enemy severing case.
No original Windows-runtime comparison was performed. Actual Use pickup remains
covered by the earlier animation/weapon fixtures, not this contact-disabled test.

## Interpretation and upgrade boundary

`UActor::TickPhysics` consumes the current elapsed time in chunks no larger than
20 ms; it is not a fixed 50 Hz simulation. At 120FPS it normally gets an 8.33 ms step.
`TickFalling` updates velocity and then moves using that velocity. Integration,
contact resolution and per-step rotation rounding can vary with timestep.
The observed variation does not establish which part needs changing.

A focused audit should examine remaining-time handling after impact and whether
script-modified velocity/physics are consistently honored. The current falling
path calls HitWall, then can use a movement delta calculated before the script
changed velocity. That is a source-level concern, not a confirmed isolated bug
or permission to rewrite shared movement without a regression comparison.

Potential benefits of targeted improvements are more consistent trajectories,
less bounce/settling jitter and fewer high-speed collision artifacts. None of
those latter artifacts was established by this fixture. A modern rigid-body
solver could add richer tumbling/contact shapes, but the present evidence does
not justify replacing the engine's shared physics.

Severed heads/arms inherit the game's weapon/pickup behavior. Physics callbacks
can transition them into Pickup, play sound, change collision and affect combat.
A wholesale replacement risks those contracts as well as player movement,
projectiles, movers, water and campaign triggers. Keep original gameplay physics
as the default; any future visual enhancement should begin with cosmetic debris
that cannot be picked up or damage actors, behind a reversible comparison toggle.
For gameplay-relevant fixes, preserve script events and saves, then compare
combat, throw/pickup, traversal, water, movers and save/load before enabling them.
This can reduce regression risk; it cannot guarantee that no game behavior breaks.
