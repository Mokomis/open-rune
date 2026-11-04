# Corpse carrying and throwing

November 3, 2026. Shared native gameplay, Mac validation; Windows and Android
have not been deployed for this change.

## Controls and behaviour

Use near an intact, settled humanoid corpse to lift it above the head. Normal
weapons, shields, food, runes and switches keep their existing Use priority.
Attack or Throw releases the body after a 0.32-second wind-up; Use sets it down.
There is a 0.35-second minimum carry preparation and a 1.2-second player recovery.
Selected equipment is parked by its attachment pose, retaining inventory/slots
and returning automatically when carrying ends. A dual pair is first stowed by
the existing dual-wield path. No duplicate equipment is spawned.

Walking speed and acceleration are 62% while carrying. Blocking, weapon attacks,
kick, bash, leap, rush and grabbing are unavailable until the body is released.
Falling, water/swimming, interrupted states, lost arms, death or disconnection
release it. A low overhead obstruction also ends the carry. Decorative corpses,
visible sever caps, non-humanoid skeletons, excessive scale, mass or collision
size are rejected. Ordinary pickup limbs and HeadBall heads retain their original
behaviour; a held non-stowable item also prevents corpse pickup. Bodies must finish the original death animation before pickup.

Carried bodies use a relaxed reference pose, laid across the carrier with arms,
head and lower legs hanging down. Only joint rotations change: reference joint
positions, scales and bone lengths are preserved. Both hands use the shared
two-bone solver, and the torso underside rests at the palms. The throw uses the
carrier's authored throw animation where available. Flight retains the relaxed
body pose; landing restores the original authored ground pose. This remains
rigid body flight, not a full articulated ragdoll.

## Physics, combat and bots

Flight is authoritative, bounded to 120 Hz substeps, with swept static-world and
mover contact and separate swept pawn contacts. The conservative collision proxy
is 18 units in radius and 14 high. This does not reproduce every extremity's
collision. Throw speed is 720 forward / 125 upward; each victim can be contacted
only once per throw. A normal hit applies 18 blunt damage and a mass-scaled shove;
a facing, raised shield prevents damage and reduces the shove substantially.
Crossed weapons offer a weaker guard. Original damage, armour, death and scoring
callbacks still run. Team friendly-fire rules suppress both damage and momentum
when friendly fire is off. Ground contact restores the original corpse actor
basis, collision and cleanup behaviour; a four-second flight cap prevents leaks.

Eligible multiplayer bots observe visible replicated bodies and send ordinary
Use/Throw intents. They prefer nearby bodies when a visible opponent is at useful
throwing range, allow the preparation, aim before throwing and avoid friendly
lanes. They do not get pickup/damage authority or know hidden corpse locations.
Compatible non-boss campaign Vikings have the same opportunistic action, with
an eight-second reuse cooldown and a longer preparation. They retain original
campaign AI outside this action. No creature AI or campaign objective scripts
were replaced.

## Replication and saves

Existing owner/joint attachment fields encode the carry. Replicas resolve the
body through ownership links, since snapshots do not execute attachment scripts
or install JointChild arrays. Existing flag 4 identifies centred, relaxed corpses
(the flag retains its guard meaning for pawns). Active corpse positions are sent
in that centred basis. This makes a client joining during a throw reconstruct
the same pose without first observing pickup. On landing, interpolation never
mixes the centred and original ground-pose coordinate bases. No new message type
or field was added. Local attack/guard anticipation is suppressed while
carrying; movement acknowledgements include reduced speed/acceleration.

Native match rules revision is **22** (previously 21). Wire protocol/ALPN remains
22. Matching client/host builds are required; old platforms are not silently
assumed to understand the new gameplay rules.

New flight/carry bookkeeping is transient and owned by the actors. Saving safely
releases held bodies and normalizes active flights before serialization. A save
does not retain the overhead carry or a pending throw. Original assets and
existing saves are unchanged. Gore presentation never settles an actively
controlled body; the mechanic works independently of Original/Enhanced gore.

## Validation

`Tools/RuneValidation/corpse_throw.py` runs isolated standalone-input, network and
Team Deathmatch fixtures against original map geometry and death scripts. Cases
cover Use and Attack dispatch, preparation/release, exclusive pickup, speed,
blocked extra moves, inventory ownership, shield defence, repeated use,
save normalization, death/delete/disconnect cleanup, giant/decorative/severed
rejection, bot pickup/throw decisions, and friendly-fire suppression. A campaign
DarkViking also performs its automatic pickup/throw in the standalone-input case.
The fixture is bounded coverage, not a complete campaign playthrough.

Each native fixture encodes/decodes real snapshots and checks source/replica
mesh transforms through 140 flight/settle frames. `--probe-corpse` provides a
separate rendered fixture with an original Goblin death, carry and throw;
`--save-directory` and `--probe-enemy=GoblinWarrior` are required. Run it at native
resolution with 4x AA (`--render-scale=1 --render-msaa=2`).

Evidence, exact commands, build logs and local previews are outside Git under
`outputs/corpse-throw-20261103`. Early visual captures with a fixed-height body
anchor are rejected and retained as diagnostic evidence. Later checks use the
actual hand anchor. Final results are recorded there after verification. No
claim is made for Android performance, internet latency, every character/map,
or human playtesting from these Mac fixtures.

### Recorded Mac result

Final native fixtures pass **2,482 assertions** (830 standalone-input/campaign
bot, 825 network, 827 TDM), including walking while carrying and reduced movement
acknowledgements. Source/replica transforms match through 140 flight/settle
frames per case. A separate RuneSinglePlayer game-rule run completes 800 rendered
frames at native **2816×1762 / 4x AA**, including the Goblin corpse carry/throw and
449 finite-pose checks. The overhead preview was visually inspected after the
hand-anchor correction.

All 15 network-library tests and the existing native combat/sever/death regression
(430 snapshots / 4,093 ticks) pass. A four-player TDM regression also completes
with 75 ms one-way simulated latency, 15 ms jitter, 3% loss and 2% duplication.
That broader match is not proof that a bot happened to throw a corpse in that
particular run; deterministic bot/authority fixtures cover the action. The final
attached-effect position housekeeping was followed by the native corpse and
rendered tests; binary hashes for each stage are retained in verification.json.
Six original INI files and the existing save/checkpoint backup match their
pre-work hashes. No normal saves were overwritten, no public host remains,
and no Windows/Android install, commit or push was performed.

### Visible demo follow-up

The owner did not see a pickup in the first short live demonstration; its log
alone must not be treated as proof of what they saw. The optional `--corpse-demo`
fixture now uses the real Ctrl/Use and left-mouse/Fire engine input bindings,
clears loose equipment from its isolated pickup area, holds the corpse overhead
for 20 seconds at 60 Hz, and captures frames during carry, wind-up and release
with `--corpse-demo-capture=<prefix>`. It still requires `--probe-corpse`, bounded
frames and isolated saves. Ordinary gameplay and the original short fixture
remain unchanged.

The carry frame from `outputs/corpse-throw-20261103/watch-demo-long` was inspected:
Ragnar and the overhead corpse are visibly rendered, with the hands supporting
the body. This is an engine capture, not confirmation that the Mac window was
foregrounded. The reason the earlier view did not show the pickup remains
unconfirmed. Native 2816×1762/4x AA; no tablet/Windows changes or publication.

### Relaxed-pose and proportion correction

The live Goblin audit found unit local scales at every joint and world-transform
axis lengths 1/1/1. The flat appearance came from reusing the original floor death
pose overhead, rather than a nonuniform transform. A cached reference pose now
provides natural proportions and articulated hanging limbs. The reference pose
is chosen from the original skeleton's idle sequences; no assets were modified.
Support height comes from the torso surface, avoiding floating caused by using
hanging hands/feet as the grip plane.

Posed corpses now use their actual skinned bounds and centred transform for BSP
visibility. Native tests ensure visible source and replica vertices are inside
those bounds, rather than inheriting the old authored-pose/joint bounds. The
uncontrolled-corpse and other-actor bounds paths are unchanged.

All **36 roster skeletons / 1,332 bones** pass reference-position, scale and
world-length invariants. Standalone-input, network and TDM fixtures also pass
full skinned-vertex parity, mid-flight late-join and landing-transition checks,
alongside the existing pickup/throw, shield, bot, movement and cleanup coverage.
These checks establish proportion/replication invariants, not visual approval of
every roster character. The Goblin pose was visually reviewed at native resolution
with 4x AA on the Mac. Final evidence is in `outputs/corpse-shape-20261103`.
