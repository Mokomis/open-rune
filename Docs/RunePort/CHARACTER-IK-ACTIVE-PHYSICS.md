# Native character IK and active physics

Open Rune implements its own character IK and muscle-driven reactions in C++ on
Jolt. It does not embed RootMotion Final IK or PuppetMaster, does not require
Unity, and does not include their commercial source or assets. Jolt remains the
collision and rigid-body backend. These are native equivalents for Rune's
character interactions, not a claim of feature parity with those entire products.

## Behavior

- Grounded bipeds fit their feet and ankles to solid walkable surfaces with a
  two-bone solver, a bounded pelvis correction and time-based smoothing. Feet
  retain their actual bone lengths. Airborne, swimming, scripted/boss and
  unsupported rigs retain the existing animation. A severed leg is not planted.
- Existing two-hand shield-rush and overhead corpse grips share the same robust
  two-bone solver. Carrying keeps its existing hand height and wrist orientation.
- Cinematic and Chaos physics add short, on-demand upper-body muscle reactions
  to committed living hits. A hit selects a head/neck or arm branch; the remaining
  rig follows animation kinematically. Limited motors recover toward the current
  animated/IK target. Reactions fade within 0.7 seconds; at most twelve run at once.
- A full knockdown retires the cosmetic rig and uses the existing authoritative
  articulated body. On safe recovery, an eased root and powered joints return
  toward a standing pose with feet fitted to the validated floor. Original mode
  and the previous passive recovery remain available.

## Gameplay and multiplayer boundaries

Cosmetic muscles never own actor position, velocity, collision, damage, weapon
sweeps or action timing. Their separate collision layer cannot move gameplay
rigid bodies. Own-body contacts are rejected. Damage is observed after the
outermost successful script call, only when health actually decreased; blocked,
missed and lethal hits do not create a living reaction.

The client receives existing reliable committed-hit outcomes, fenced by actor
identity, life and age, and simulates only cosmetic muscles. The interactive
client explicitly advances this visual simulation because it intentionally
bypasses the campaign actor/VM tick. Its render scope enables presentation-only
IK; server grounded flags prevent planting remote airborne characters. Full
knockdowns/corpses retain the existing server-authored physical pose protocol.
No packet layout, attack wind-up, damage rule, Laws of Battle or 33 ms block window
has changed.

Targets are sampled inside a nested target-pose scope so a previous physical
result cannot feed back into its own motors. Repeated render/shadow/attachment
queries reuse the foot solve. Destruction, teleport, model changes, level reset
and saving discard transient reactions. Saves do not serialize Jolt handles.

## Controls and fallback

Use the existing Gameplay physics profile: Cinematic or Chaos enables active
muscles; Original retains ordinary hit animation. Ground IK is automatic for
supported grounded characters. There are no new attack gestures or AI actions.
Developer comparisons can disable `--rune-ik=0` and/or `--rune-puppet=0`.
`--rune-puppet-trace` logs reaction creation and actual presentation when an
isolated save directory is supplied. These flags do not change normal preferences.

## Validation

Evidence is kept outside Git under:

- Windows: `C:/OpenRune/outputs/character-physics-20261109`
- Mac: `/Users/rwillmore/OpenRuneArtifacts/CharacterPhysics-20261109`

The native math test covers 40,000 random reachable/unreachable and degenerate
cases. The Jolt muscle test runs at 60 and 120 Hz, including impulses, release,
recovery, translating/rotating roots, targets beyond passive bind limits, cleanup
and cosmetic/gameplay collision isolation. Runtime probes exercise all 37 original
character/skin variants, rendering-only rotation, expiry, movement preservation,
actual damage callbacks, remote cosmetic stepping, teleport, save cleanup,
knockdown promotion and powered recovery. A twelve-character stress case is bounded.

Mac, Windows and ARM Android builds pass those runtime probes. The existing
physics, save/load, native network combat and corpse-interaction checks also pass.
Rendered playground evidence covers physical fall/recovery, death handoff, pickup
and throw; Android targets are checked at 2520x1680, 100% scale and 4x MSAA.
Interactive network validation uses private invitations and no public advertising.
Probe entry points (always supply a separate `--save-directory` and game root):

- `--rune-probe --rune-physics-probe --rune-ik-probe`
- `--rune-probe --rune-physics-probe --rune-puppet-probe`
- Add `--rune-physics-campaign` for the campaign fixture.
- Build/run `openrune_ik_tests` and `openrune_muscle_tests` for standalone tests.

See artifact reports for exact builds, captures and profiles; short fixture rates
are not guarantees for every map or a substitute for human playtesting.

## Not included

This does not replace character locomotion with physics or add a general-purpose
whole-body balance controller, arbitrary-creature auto-rigging, climbing or a Unity
plugin bridge. Existing campaign collision/navigation and combat rules remain
responsible for gameplay. Ocean foam is a separate deferred idea, not part of this
change.
