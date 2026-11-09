# Shadow caster visibility — November 3

Moving Rune meshes now use their current posed geometry for visibility bounds.
Shadow views collect moving mesh/brush casters independently of the static BSP
surface bounds. Static surface traversal, light selection, light budgets, shadow
resolution and receiver shaders retain their previous behavior.

## Reproduced problems

The old Rune mesh bounds used the first serialized animation frame with the
legacy UE mesh transform. Rune renders its own PositionOffset convention and
current animation/procedural joints. On the sampled Dark Viking, actual rendered
vertices extended outside the reported box in all 960 Hildir views.

Separately, the BSP walker could reject a whole subtree using its static scenery
RenderBound before examining its ActorList. Moving actors can extend beyond that
bound. In one of 960 Hildir samples, DarkViking0 was directly visible to Light31
but absent from the shadow list (node 1863, bound 1015). The same new executable
reproduces both failures with the reference flags below.

## Implementation

`USkelModel::PoseBounds` caches local joint-influence boxes per geometry variant,
then transforms and unions those boxes with the actor's current joint matrices.
The union conservatively contains the weighted skin vertices, without skinning
the entire mesh for each bounds query. Carried bodies retain their existing
exact posed bounds. These are rendering/BSP-list bounds, not collision bounds.

`VisibleFrame::ProcessShadow` traverses static surfaces normally, then tests each
mesh/brush actor against the light frustum using that actor's own bounds. It
retains the ordinary actor visibility/attachment rules. Shadow frustum culling,
already available on Windows, now defaults on for Mac and Android too, avoiding
software span rasterization for these non-occluding shadow views.

## Checks and limits

Mac CPU fixture, final source:

| Map | Sampled light views | Required player/enemy caster appearances | Missing | Bounds failures |
| --- | ---: | ---: | ---: | ---: |
| DM-Hildir | 960 | 909 | 0 | 0 |
| DM-LostCrypt | 1,152 | 1,442 | 0 | 0 |
| RagnarVillage | 96 | 66 | 0 | 0 |

All 36 roster models, their geometry variants and sampled animation phases also
pass 32,400 actual skinned-vertex containment checks. These deliberately sampled
positions test visibility, not playable navigation through every position.

The actual Mac Vulkan three-light GPU fixture passes 28 cases at native
2816×1762 with 4× MSAA. Native single/network/team corpse fixtures pass again;
rendered walking/turning/throwing checks cover the moved shared body-motion hook.
Brief native 4× matched samples initially measured 65.96→66.82 FPS static and
74.88→73.22 FPS with an enemy; this is approximately neutral, not a guaranteed
performance gain. CPU shadow traversal is cheaper, but previously omitted
geometry can add GPU work. Timing under concurrent workloads is excluded.
A clean reverse-order combat repeat measured 70.20→79.09 FPS and 1.92→1.36 ms
CPU scene time. The short runs vary; they show no consistent regression, not a
sustained frame-rate guarantee.

This fixes the demonstrated actor-bound/subtree failures. It does not certify
all maps, eliminate shadow changes caused by the finite light budget, or resolve
the separately reported terrain pop-in.

Evidence is outside Git: `outputs/shadow-culling-20261103`, including exact
arguments, reference/fixed logs, source hashes, GPU results and captures.

## Reproduce on Mac

Run the built engine against an isolated game fixture:

```
SurrealEngine --rune-network-probe --rune-shadow-culling-probe=DM-Hildir.run \
  --shadow-culling-strict --shadow-roster-bounds \
  --save-directory=/absolute/isolated/saves /absolute/game
```

Reference comparison: add `--rune-pose-bounds=0 --shadow-caster-scan=0
--shadow-frustum-cull=0` and omit strict mode, since failures are expected.
Rendered GPU checks use `--probe-shadow-budget --render-scale=1 --render-msaa=2`
with bounded `--maxframes` and isolated saves/configuration.

The changes are shared by all clients. Protocol/ALPN and native rules stay at 22.
Windows deployment is separate; do not infer it from a Mac/Android build.

## Android deployment

The matching ARM64 update is installed on OPD2515 with the existing signing key.
All 68 eight-slot GPU shadow cases pass at actual native 2520×1680 with 4× MSAA;
the offline stair comparison also passes. Normal saves/system files/preferences
are unchanged. Evidence and the previous APK/data backup are in
`outputs/shadow-culling-20261103/android`. The tested installed APK SHA-256 is
`c87896b513ecb5dd82f154e8032bcb818a8c1372852d7b28f0f60c270d2e047b`.
This also adds the bounded Android stair diagnostic intent; the native library
is the already-tested build. Windows is not updated by this deployment.

## Shadow-camera depth range correction (November 7)

The shared perspective helper omitted `z_near` in its depth translation term.
Main cameras use near=1 and were unaffected, but shadow cameras use near=2.
Their intended 900-unit depth range clipped at approximately 450 units.
This excluded Ragnar and other character casters even when aiming at them.
The perspective coefficient now includes the near-plane multiplier; no camera
FOV, light count, atlas resolution or shadow-distance setting was increased.

An independent regression checks analytic near/far endpoints and out-of-range
points across near=.5/1/2/4, far=64/900/1800, both handedness conventions and both
clip-depth conventions (192 checks). It fails before the fix and passes after.
Thorstadt's native culling fixture passes 672 views with 825 expected casters
and none missing, on Mac and Windows. Same-view Mac rendering changes from
zero actors/pawn outside shadow range to six actors/pawn inside, with actual
2816×1762/4× AA captures reviewed. A bounded Android check at2520×1680/4× AA
also reports six character/attachment casters and pawn-visible=1, completes
without launch errors, and its capture was inspected. The signed update was
installed and the current match rejoined. No claim of a sustained FPS gain.
