# Sword counter to an exposed kicking leg

November 9, 2026. Part of the existing **Kick** Law of Battle, for eligible
campaign players/enemies and multiplayer players/bots. No extra button or
random roll: a committed sword swipe that actually contacts the raised kicking
leg can sever it. Existing kick timing remains 375 ms to contact.

## Gameplay

- The kick must still be active, in animation phase .12-.90, with a projecting
  foot or raised/projecting knee. The actual native swept joint mask must hit
  that leg's knee/ankle/toe subtree. Torso hits do not become leg hits.
- The original weapon damage must be committed, effective incoming damage at
  least eight, actual health lost, and the target must survive. Frontal shield,
  crossed-weapon block, team and other existing damage decisions still apply.
- The first severed leg leaves a separate, non-pickup physical part. The survivor
  is not forcibly ragdolled: walking uses a render-only hopping stance, speed
  and acceleration are reduced to 60%, and further kick attempts are disabled.
  The original swept collision cylinder and stair traversal stay intact.
- A bounded local stump/drop effect and shared-budget floor stains show bleeding.
  There is no added damage over time. Blood retains the reviewed dark palette.
- Bosses, scripted/cinematic actors, unsupported skeletons and already injured
  fighters are protected. Respawn resets the injury. Saving/loading preserves it;
  an eventual corpse retains its missing leg.

## Integration

RuneLeg.cpp handles contact capture, injury, pose, detached geometry and local
presentation. The ordinary VM DoWeaponSwipe/JointDamaged path remains the source
of damage. Multiplayer captures the anatomical exposure at the original sweep,
then replays it only if that hit commits after the existing two-tick/33 ms block
window. It does not reevaluate a later idle pose or undo committed damage.

Unused JointFlags slots 48/49 carry a tagged injury/part marker, guarded by the
supported joint count; original property serialization and reliable appearance
replication preserve it. BodyPartHealth[6/8] uses a native sentinel. There is no
new packet or save field. Weighted triangle masks support roster models whose
legs share material groups, and both render pose paths apply the hop after foot
IK. Movement acknowledgments carry the same speed reduction for prediction.
GameplayRulesRevision **35**, version **0.1.0-dev.15 / build 15** requires matching
host/client gameplay revisions. The underlying wire protocol is unchanged.

## Validation and limits

`--rune-probe --rune-kick-counter-probe` uses isolated real game packages, VM,
animations, swept weapon collision, network authority and save fixtures:

- All 36 roster class geometries expose valid leg masks (the 37th skin variant
  shares geometry). Finite hopping poses and separate leg transforms checked.
- A 33-case sword/kick timing-and-distance matrix succeeds in three cases, two
  within kick range; all three occur before kick contact. These are selected
  test cases, **not a random probability or a player success-rate estimate**.
- Torso-only contact, folded leg, disabled Kick law and weak scratch are rejected.
- Deferred committed hit retains original exposure; remote appearance restores
  the injury. Ordinary admitted input moves the injured fighter; it cannot kick.
- Real campaign NPC/player roles, injury save/load, loose parts and original
  SpawnCarcass/InitFor missing-leg transfer pass.

Mac replay `--kick-counter-demo` reviewed the separated leg, missing right leg,
hopping stance and blood at actual **2816x1762, four samples**. This presentation
fixture has an explicitly logged deterministic original-script contact fallback;
it is not the proof of the naturally timed counter. The separate 33-case sweep
probe supplies that evidence. This is bounded regression/visual coverage, not
all-character/all-map human playtesting or a new sustained performance claim.

Evidence: `C:/OpenRune/outputs/kick-counter-20261109` and controller
logs are retained; use final probe and deployment receipts for actual results.
Original game packages, ordinary saves/settings and previous builds are preserved.
No upstream contribution, commit or push is part of this change.
