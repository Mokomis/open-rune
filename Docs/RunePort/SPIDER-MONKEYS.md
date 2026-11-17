# Spider Monkeys — wall climbing

A separate Law of Battle for eligible campaign players/enemies and multiplayer
players/bots. Off by default in the campaign. Stored selections are preserved;
existing host profiles do not silently enable a new bit.

## Controls and surfaces

Jump toward a wall, then press **Use** (Ctrl, left-stick click or the Use touch
button) to grab it. Move forward/back to climb up/down, and strafe to traverse
sideways. Jump pushes away. Use again or Crouch drops. A clear top transitions
through the original pull-up/step-up motions.

Use remains the original interaction while grounded. Grabbing requires a nearby,
facing, visible, solid, approximately vertical BSP wall at hand height. Movers,
invisible barriers, portals, sky, ceilings, water/pain zones and unsafe geometry
are excluded. Original ledge grabs and ropes remain available normally.

## Presentation

Wall traversal uses the original **ClimbUpRope** complete-body cycle: the hands
alternate their reach and the feet alternate their steps. Its phase follows
actual swept distance (one cycle per96 model-scaled units), reverses while
climbing down, and holds when movement stops. Original limb rotations, scales
and local positions are preserved, with normal frame interpolation.

The two-handed **intropullupA** / **pullupTest** motions are reserved for the
collision-validated ledge finish. They are not repeated while climbing a wall.
Serialized pelvis travel is rebased at the Base joint so native collision sweeps
supply world movement. There is no procedural wall limb posing or invented knee
rotation, and existing ground IK remains unchanged.

## Movement, AI and lifecycle

All movement uses the original full-cylinder TryMove sweep. Ledge paths require
clear vertical/across/down segments and a safe static landing, and each segment
is still swept during execution. No flying/noclip or collision-disable path.
Wall queries reject gaps; bot plans additionally require continuous contact to a
reachable top. Top searches while climbing are bounded to ten per second, with
an immediate final query on losing hand contact.

Weapons are stowed/hidden during climbing and restored on release. Attacks and
other special moves cannot run concurrently. Committed damage, death, disabling
the campaign law, saving or disconnecting release the climb. Saving cancels
transient hanging state rather than restoring a permanently suspended fighter.
Bosses, scripted states, incompatible models and missing-leg fighters are excluded.

Campaign humanoids can climb toward a higher enemy after a valid exit plan.
Multiplayer bots plan toward higher blocked goals and use the same admitted Jump,
Use and movement inputs as humans. No bot teleport or server-only movement action.
Current traversal checks are bounded examples, not a promise that every bot can
solve every route/secret in every map.

## Multiplayer and settings

The thirteenth law is bit4096; mask8191. Fresh campaign default4079 still excludes
both Spider Monkeys and giant leap; giant leap remains unconditionally disabled
for every campaign participant. Host rules stay separate and authenticated.

Climbing uses native-combat flag16 in the existing optional snapshot extension;
entity flag64 remains reserved for the wire's extension-presence marker. Snapshot
round-trip tests cover the distinction. Walking/falling prediction is suspended
while attached. Original animation phases and the climbing flag are authoritative.
No packet layout expansion or new client action. GameplayRulesRevision36 and
version0.1.0-dev.16/build16 require matching host/client builds.

## Evidence

Task artifacts: C:/OpenRune/outputs/spider-monkeys-20261109 and controller
receipts; rejected visual prototypes and failed intermediate logs are preserved.
Tests include actual BSP wall movement, sideways travel, jumping/dropping, law/save
release, collision-clear top traversal, actual Jump/Use admission, serialized
snapshot installation, prediction suspension, bot rule/input decisions, and a real
campaign DarkViking completing a planned climb. All36 roster classes have the alternating traversal and
original ledge clips. Trajectory checks confirm that both hand-height and
foot-height ordering reverse during each cycle.720 sampled original poses
preserve limb rotations, scales and local positions. Mac rendering uses native2816x1762 with four samples.
This is not an all-map human playtest or a sustained cross-platform FPS claim.

Alternating-cycle follow-up evidence: C:/OpenRune/outputs/spider-alternating-20261109
