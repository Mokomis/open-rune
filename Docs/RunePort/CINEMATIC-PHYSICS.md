# Cinematic physics prototype

November 4, 2026. Optional shared native gameplay using vendored Jolt Physics
5.6.0 (MIT; pinned revision and archive hash in Thirdparty/JoltPhysics). Original
remains the default. This is a playable prototype, not campaign-wide validation.

## Kick knockdown chance — November 4 follow-up

Eligible unblocked living kicks in Cinematic and Chaos now make one 50% roll
for an articulated knockdown; the other outcome retains upright knockback
through the original AddVelocity path. The host decides after committed damage,
so replicas follow its physical state instead of rolling independently. Original
mode, shields, crossed-weapon protection, recovery immunity and scripted/boss
exclusions retain their existing behavior. Bash, rush, slam and loose-object
kicks keep their existing physical responses.

The Mac matrix passes campaign and multiplayer-authority cases in all three
modes, sampling 256 kicks against each of a player and a humanoid NPC per case.
Cinematic/Chaos samples produce 120–125 knockdowns out of 256; Original produces
none. Both outcomes complete normal landing/recovery checks, and physical/standing
states survive snapshot encoding. Actual native/requested4× mob-kick animation
and raised-shield checks, plus the crossed-guard runtime regression, pass.
Evidence: `outputs/kick-knockdown-chance-20261104`. Native rules advance to 30;
protocol25/schema7 are unchanged. Windows and Android are not updated.

## Playing

Settings → Gameplay → Cinematic physics offers Original, Cinematic and Chaos.
The same choice is in hosted Match rules; an online client follows its host.
Use **Physics playground** on that page for an isolated Hildir practice arena:
an active opponent, a throwable stool and a prototype spike hazard. Its saves
live separately from normal campaign slots. Starting a normal game restores the
normal save directory. Retry restarts the arena. Ordinary playground play does
not lock bloodlust or continuously restore health.

Cinematic enables articulated humanoid knockdowns, recovery, physical corpses
and selected small props. Chaos increases their momentum, spin and bounce.
Existing kick, shield bash, rush and slam supply impulses through the shared
combat path. Facing shields absorb most shove; crossed weapons provide a weaker
guard. Original damage, friendly-fire and scoring rules remain authoritative.

Use near a compatible settled corpse or an untagged stool, keg or bucket to
carry it. Use sets it down; Attack or Throw releases it after a wind-up. Normal
weapons, food, switches and scripted objects retain interaction priority. Carried
equipment is stowed and restored through existing attachment paths. These props
are selected conservatively: tagged/triggered scenery and brush movers are not
converted into throwable objects.

## Implementation and boundaries

Physics steps at 60 Hz with separate render interpolation. World BSP and moving
brushes provide collision; normal pawn movement, lifts, doors and campaign
scripts remain under the original engine. Humanoid bodies use twenty constrained
parts, reference bone lengths and authored model offsets. Physics contacts are
queued before calling game scripts. Budgets bound live knockdowns, corpses and
props; retired corpses return to original behavior rather than being deleted.

Living knockdowns suspend ordinary actions and recover only at a validated
standing position. Bosses, scripted sequences, large/incompatible creatures and
unsupported skeletons retain original behavior. Normal death animations and
original script callbacks still run. The physical carcass now inherits the dying
pawn's exact final pose in the same level tick; settled bodies start asleep,
without a fresh collapse. There is no instant live-ragdoll-to-dead-body handoff
yet. The original limb/gib system remains separate; individual severed
parts have not all been converted to Jolt. Prototype spike/fire registration is
explicit and uses contact proximity/velocity rules; this is not a complete
hazard library or a retrofit of every visual trap.

The server alone simulates physics and applies damage. Authenticated snapshots
carry bounded physical poses; clients interpolate them without their own damage
simulation. Protocol/ALPN is 25, snapshot schema 7 and candidate native rules 30. Matching
clients and hosts are required. Ordinary snapshots retain their compact layout.
Transient rigid bodies are released before save/map unload, keeping the existing
save format. Loading reconstructs eligible physical objects rather than restoring
a serialized Jolt world.

## Validation

Evidence is retained outside Git at outputs/cinematic-physics-20261104.
The native harness checks Cinematic and Chaos on Hildir and LostCrypt, accepted
Use/Throw interaction, joint constraints, impacts, recovery, replica poses and
actual save/load. Independent world tests cover floor collision, continuous
collision, sleeping, a moving lift and a 96-body stress case. Network tests cover
bounded pose decoding and malformed input; two encrypted clients also exercise
bash, knockdown and recovery under 35 ms each-way delay and packet loss.

The OPPO repeated-combat run rendered 8,400 frames at native 2520×1680 with 4× AA;
its 7,200-frame profile averaged 116.79 engine FPS (8.56 ms mean, 10.25 ms p95).
This is a roughly one-minute fixture measurement, not a guarantee for every map,
32 players, prolonged thermal conditions or every graphics configuration.
Physics interpolation was subsequently restricted to presentation; final matching
build checks and deployment evidence are recorded in verification.json.

No original packages or normal saves are intentionally modified. The Android
menu harness drives actual touch navigation and checks preference/save restoration.

Final matching Mac/Windows native matrices pass all five cases per platform.
The Mac-host/Android-client bash fixture observes physical knockdown and recovery
on both clients. Its tablet profile is 87.38 FPS against 89.61 FPS with Original
physics (900 frames, native/4× and the same saved visual settings). This is a
small fixture comparison, not an isolated GPU benchmark. The final Android UI
check passes actual touch navigation and exact preference/save restoration; all
111 baseline normal Save/System files are unchanged. The installed APK matches
the packaged hash. Windows runtime files were backed up before deployment.


## November 4 playtest corrections

A naturally killed ragdoll can retain an actor pivot below the floor. Corpse
Use/visibility now targets its visible pelvis/neck center, preserving reach and
world occlusion. The old fixture manually reset body physics, position and
animation completion; the replacement leaves the physical corpse intact and
approaches it with the player before using the ordinary Use/Throw routes.

Carcass state scripts now continue while physics owns movement. Previously the
initial Dying state could be suspended indefinitely, retaining its
JointDamaged ignore and preventing normal gib/cleanup behavior. Original
carcass ground-alignment callbacks yield to Jolt, while state transitions,
gibbing, destruction and cleanup continue. Natural death/pickup/throw and full
gibbing standing, during knockdown and after corpse adoption are regression cases.
Enhanced gore no longer emits extra flesh fragments merely because an intact
pawn died or its replacement carcass appeared. Real severed limbs and gibs
retain their enhanced fragments, blood trails and stains.

In Cinematic/Chaos, a kick without a living target can punt a visible loose
corpse or eligible prop in front of the kicker. It reuses the authored kick
notify, range/cone/occlusion limits, cooldown and host authority. Kicked objects
can hit opponents through the existing physical-contact damage path; friendly
fire/team and pre-match protections remain. Carried, hidden, deleted and
replicated-only objects are excluded. Unblocked living kicks receive a stronger
560-unit horizontal/180-unit upward physical shove (before mass scaling), and a
separate stock contact thud. Shield/crossed-weapon protection and normal damage,
wind-up and block-window rules remain.

Follow-up evidence: outputs/cinematic-followup-20261104. The stricter Android
fixture verifies an actual Use tap, visible carry, then a Q/Throw tap before the
body's release, with no injected engine pickup/throw calls. Its clean native
2520×1680/4× run averaged 105.78 engine FPS over 1,800 profiled frames; this short
moving-camera fixture is not directly comparable with the previous repeated
combat benchmark. Current normal Save/System files and preferences are checked
and restored separately from isolated test data.


## Wall slams and breakable props — November 4

Cinematic and Chaos now react to hard environmental impacts. A living ragdoll
thrown or knocked into a solid wall or moving brush can take one additional
3–14 blunt damage through the original damage/scoring callbacks. The same
knockdown cannot accumulate repeated limb-contact damage. Recovery gains a
bounded 0.45-second delay, with the existing six-second hard recovery limit and
post-recovery resistance retained. Floor landings, slow/glancing contact,
friendly-fire exclusions, protected actors and pre-match HeadBall rules are
respected. A raised shield or crossed block still resolves at the original
contact before any knockdown is created; the 33 ms block window is unchanged.
Stock stone-contact sound and a small original HitStone effect mark a wall hit.

An eligible destroyable stool, dwarf stool, keg or bucket can shatter after a
hard throw/kick collision or after being struck by another combat-driven body.
The original DecorationRune damage/destruction callbacks provide sound and wood
debris; HitWood provides the bounded impact cue. A passive prop does not shatter
just because it is first activated or settles. Indestructible flags, scripted
tags/events and noneligible scenery remain protected. At most two props break
per physics update, with impact particles throttled globally. Debris uses the
original cleanup/physics rather than adding an unbounded Jolt fragment budget.

Physics contacts retain pre-solver point velocities and normal impact speed.
Gameplay callbacks run after the solver. One strongest surface contact per actor
avoids duplicate damage from twenty articulated parts. Broken props disappear
through the existing reliable actor lifecycle; wood debris and supported hit
effects replicate through existing snapshots. Packet format stays 24/schema7;
native gameplay rules advance to 27, requiring matching clients/hosts.

The isolated live playground replenishes a destroyed prop, cycling stool, keg
and bucket. This replenishment does not run in normal maps. The diagnostic
`--physics-environment` fixture uses an authored Hildir wall, and optional
`--physics-repeat` performs six impact/break cycles in the Android check. The
isolated `--physics-impacts=0` comparison leaves body/prop physics enabled while
disabling these added consequences; it is not a new menu preference.

Evidence: outputs/cinematic-impact-20261104. Native tests exercise campaign and
multiplayer in both modes, recovery bounds, floor exclusion, normal impact
speed, original debris, replica removal/effects, indestructible props and tagged
scenery. The initial visual camera clipped inside the wall; those captures and
profiles are retained as failed visual evidence and excluded from the final
visible-scene performance comparison.


Final safety follow-up: pickup, gentle Use-drop and interrupted carry clear the
previous prop's attack owner/armed state. Only a new kick or actual throw arms
it again. Carried or gently dropped props cannot apply incidental contact damage
or break because of a previous throw. Regression cases execute Use → Use on a
previously thrown keg, then let the now-destroyable keg land without breaking.
Contact selection also keeps an unrelated floor hit from hiding a meaningful
combat-body hit on a passive prop.

The visible OPPO native2520×1680/4× comparison completed six cycles per run:
112.09 engine FPS with added wall/break effects, 109.34 with those consequences
disabled. The small difference includes sequential variation and different body/
prop motion; it is not evidence of a speed gain. Original mode regressions and
Mac/Windows 13-case matrices pass. Final installed hashes, preservation and
matching-build checks are recorded in outputs/cinematic-impact-20261104/verification.json.

The final drop-safety build repeats all six wall/break cycles on the tablet at
native/4×: 111.78 FPS over 4,200 profiled frames (8.95 ms mean, 9.56 ms p95).
Normal data/preferences remain byte-identical. Matching Windows runtime files
were verified and backed up before replacement.


## Seamless death handoff — November 4

Original `InitFor` copies the dying pawn's appearance and attachments. A native
post-script observer captures its final authoritative pose, transform and
velocity, then adopts the carcass at the end of that same level tick. Original
death animations, inventory drops, scoring, events, gib paths and cleanup retain
their script order. The old quarter-second scan is still a fallback for existing
or loaded carcasses; it no longer causes the fresh-death pose reset.

A body already at rest starts with every rigid part asleep; it stays in that
pose until disturbed. Airborne handoffs retain velocity and receive no synthetic
spin impulse. Deaths based on moving brushes remain dynamic. Enhanced-gore
cosmetic settling yields to physical ownership, avoiding two competing settling
systems. This does not replace the earlier knockdown-to-death-animation path,
add physics to incompatible creatures or certify all campaign death scripts.

Some authored death poses use nonreference joint translations. Protocol/ALPN 25
adds physical kind 3 with sorted, unique, bounded sparse bind corrections
(position in 1/64 units, positive scale in 1/4096 units). Unchanged bones and
ordinary snapshots retain their existing encoding. Corrections are host-authored,
limited to 64 bones and checked before installation; malformed counts, indices,
duplicates, scales and truncated payloads are rejected. This preserves the corpse
shape on remote clients as well. Native rules are 28; snapshot schema stays 7.
Earlier clients/hosts intentionally cannot join these builds.

Evidence: `outputs/cinematic-death-20261104`. Mac and Windows each pass the
19-case physics matrix, including naturally killed Viking, Goblin and Dwarf
handoffs under single-player and multiplayer rules in Original/Cinematic/Chaos,
settled-pose continuity, airborne inherited velocity, snapshot joint agreement,
full-gib exclusion, pickup/throw and save/load. The Mac rendered fixture captures
the adjacent pre/post-handoff frames and verifies subsequent pickup/throw at
native window pixel resolution with 4× AA. All 15 network tests and the Original
single/network/team corpse regressions pass. The matching Android build is installed in place with its original signing key.
The actual OPD2515 touch fixture passes natural death/handoff, settled-body sleep,
Use pickup and Throw release at native 2520×1680 / 4× AA. Its 1,800-frame sample
averages 109.29 engine FPS; this short fixture is not a sustained-performance or
displayed-120 guarantee. All 111 baseline Save/System files and both preference
files remain byte-identical. A normal cold launch returns to the menu, with no
diagnostic extras or automatic controls left active.
