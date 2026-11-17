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
carrier's authored throw animation where available. Carried and airborne limbs now use bounded pendulum motion around that relaxed
pose: arms, lower legs and head react to translation and turning, with small
thigh motion and a fixed torso support. Landing restores the authored ground
pose. The authoritative body still uses rigid flight; individual limbs do not
collide with scenery or apply damage. See the November 3 motion follow-up below.

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

## November 3 follow-up: movement-reactive limbs

Actor-owned cosmetic pendulums update once after simulation/presentation and
before drawing. Multiple mesh, shadow and reflection queries consume the same
cached pose and cannot integrate it again. Steps are at most 1/120 second;
higher refresh rates take smaller steps. Pauses freeze motion; large time gaps
and relocations reset it safely. Joint cones, damping and bounded relative speed
prevent unbounded flailing. Reference translations/scales and torso support stay
unchanged. Posed visibility bounds follow the animated vertices.

Multiplayer replicas simulate their own cosmetic motion from presented body and
carrier transforms. Limb angles are not sent over the network; rigid trajectory,
attachments, hit tests, saves, damage and protocol/rules 22 are unchanged. Different
clients can show slightly different secondary motion without affecting combat.
Grounded bodies discard the carry-motion state and resume their authored pose.

Mac checks cover all 36 roster skeletons/variants, moving/turning support,
30/60/120/144/240 Hz, finite transforms, unchanged bone lengths/scales, fixed torso,
pause and repeated draw queries, relocation reset, posed culling, remote cosmetic
initialization and unchanged body physics. Existing single/network/team fixtures
also pass. Native 2816×1762/4× MSAA walking/turning/throw rendering completes;
frames were reviewed. Focused CPU sampling averages roughly 0.2–0.3 ms per active
body on this Mac during the build/test run, not an Android FPS measurement.

Evidence: `outputs/corpse-motion-20261103`. This follow-up is validated on Mac and the signed Android update is installed.
DM-Hildir is handed to the user as an isolated practice arena with two Dark
Vikings and two Goblin Warriors, plus weapons. Normal campaign saves and INI
files match pre-install hashes; existing graphics choices were retained.
Android human carry/throw feedback is pending. Windows retains the prior pose.

Practice setup correction: an autosave captured the temporary `PlayersOnly`
setup freeze, so retry restored frozen AI and the non-player animation proxy
required for Use. `DebugContinue` clears that flag idempotently. Live enemies
resume and ordinary Ctrl/Use acquires a fresh hammer; the isolated practice
checkpoint was rewritten after recovery. Normal saves/INI hashes match.
Do not freeze world ticks while preparing a session that can autosave.

## November 8: overhead grip visibility (dev.14)

The carrier previously moved the wrists overhead while retaining their standing
or running world rotation. The hands could fold into the forearms or carried
torso, leaving an apparent handless support. The overhead pose now explicitly
orients both palms upward and the fingers inward, with relaxed finger-joint
rotations. Body support is lifted two units onto the palm surface instead of
intersecting it. Bone positions/scales, body proportions, leg animation and the
shared shield-rush solver are preserved. The shared carry presentation applies
to local pawns, eligible bots and multiplayer replicas.

Focused native checks verify palm/finger orientation on authority and replica,
retained wrist proportions, pickup, walking, throwing, damage, cleanup and
replicated skin transforms. Mac single/network/team cases pass at approximately
two million geometry/state checks per case. A bounded orbit fixture captures
front/back/both sides using the actual wrist positions as its camera target;
--corpse-hand-audit is isolated under --probe-corpse and --save-directory.
Android exposes this only for bounded isolated diagnostics via corpseHandProbe.

Mac native2816x1762/4xAA captures of a DarkViking carry were visually reviewed.
Earlier orbit captures with first-person hiding, obstructed cameras or poor
framing were rejected as visual evidence. They remain in the external evidence
folder for traceability. Android native2520x1680/4xAA captures, installation and
normal data preservation are in the same evidence folder. No new FPS gain or
all-character visual certification is claimed by this pose correction.

Evidence: controller ~/OpenRuneArtifacts/CarryHands-20261108 and Windows
C:/OpenRune/outputs/carry-hands-20261108. Protocol25/rules34 remain unchanged.
