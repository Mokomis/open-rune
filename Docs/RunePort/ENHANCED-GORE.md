# Enhanced gore presentation

November 3, 2026. First presentation upgrade; Mac native-resolution / 4× MSAA
validation. Original gameplay physics and the multiplayer protocol are retained.

November 3 color follow-up: enhanced spray, trails, pools and flesh fragments now
share a deeper crimson (RGB 0.34 / 0.045 / 0.028); red intensity is about 29%
lower. Wound palettes are reduced similarly, while preserving bone, alpha,
emission counts and geometry. This changes presentation only.

## Controls and behavior

**Video → Effects & weather → Gore presentation: Original / Enhanced** is a
local visual setting, independent of Fire & combat effects. Enhanced is the
default for this new option; Original restores original gib/corpse rendering.
The global original-visual comparison also disables the enhancement temporarily.
Windows/Android menu definitions, shared preferences and JNI are wired; this
milestone builds/tests the Mac, without deploying a tablet or Windows update.

Mac comparison: `--gore=0|1`, or `gore 0` / `gore 1` in the existing console
(session-only). Low/very-low-gore game settings suppress the additions. Known
Sark/green-blood body types are excluded from the red treatment; corpse blood
also requires recent nearby authored red-blood evidence.

Enhanced adds:

- Small opaque 3D flesh fragments with source-velocity-biased launch, varied
  shape and spin. Their independent 120 Hz cosmetic solver loses energy on
  impacts, resolves only the remaining contact time, skids, settles, sleeps and
  fades after six to seven simulated seconds.
- Extra render-only pitch/roll on original red-gib meshes, around their original
  centers. Original actors continue their original movement, collision and
  lifetimes; even original gibs can push pawns, so their physics is not replaced.
- Wet material response on identified wound/gib surfaces and restrained crimson
  palette treatment, preserving UVs, masked edges, pale bone and source detail.
  Wet surfaces bypass generic procedural roughness maps. The wet highlights use
  Remastered lighting; the palette and geometry changes also work without it.
- Brief airborne blood streaks, short floor trails, impact marks and small pools.
  One cached, mipmapped irregular sprite avoids perfectly round pools. Marks
  align with the contacted surface; corner/step checks reject overhanging spots.
  Pool growth remains within the checked final footprint.
- After completed death animations, bounded visual corpse alignment eases toward
  the original rotation and corrects small floor gaps. Ground correction is
  limited to six units down / three up and rejects larger discrepancies. This
  applies to dead pawns as well as replacement carcasses. It is not a ragdoll or
  a solution for bodies spanning complicated stair geometry.

Pickupable heads/limbs, held weapons, HeadBall objectives, damage, inventory,
server snapshots and original script callbacks are unchanged. No presentation
transform is used by joint queries, weapon sweeps or collision. No game packages
are rewritten. Gameplay randomness is neither consumed nor reseeded.

## Budgets and limits

- 24 additional fragments; at most eight emitted in a frame. Original actor
  counts and network messages do not increase.
- 96 collision-query budget per update; round-robin fragment processing avoids
  permanently starving the same fragments when overloaded. No physics updates
  after a fragment sleeps; old fragments retire rather than accumulating.
- 128 map-local blood marks in a fixed ring, nearest 48 drawn; existing blood
  history remains separate. Marks persist until map change or ring replacement.
- Fragments, streaks and marks share the view's existing effect budget and have
  distance limits. They add neither dynamic lights nor shadow-render passes.
- Moving-surface contacts retire the added fragments instead of leaving them
  floating after a lift moves. New blood marks are restricted to static world
  geometry. Water suppresses new marks/emissions and retires submerged fragments.
- Presentation state resets on map changes and is not stored in saves. It can
  differ cosmetically between network clients; authoritative objects cannot.

## Validation

Evidence, screenshots and isolated test data are under
`outputs/enhanced-gore-20261103` above this checkout. The fixture's configuration
is a copy and all saves use absolute process-local `--save-directory` paths.

- Sanitized `Tools/RuneValidation/gore_physics.cpp`: matched 30/60/120 Hz results,
  energy loss, bounded settling, sleep, remaining collision time, moving-surface
  retirement, paused time and surface-tangent checks.
- Java menu model: default, persistence, comparison exit and independence from
  Fire & combat effects. Existing material/shadow/settings checks also pass.
- Rendered Original / Enhanced / Enhanced / Original fixtures at actual
  **2816×1762, 4× MSAA**, with Remastered physical/environment lighting and one
  shadow light held fixed. Each run directly compares source actor location,
  velocity, rotation, physics, base and carried state across rendering. All
  **3,540 checks per 720-frame run** pass.
- The fixture drops original gibs/limbs, severs a live goblin arm, invokes the
  original death path, and covers wet wound textures, 24 cosmetic fragments,
  37 recorded cosmetic impacts and over 100 accepted marks. A separate controlled
  two-unit visual gap exercises the bounded corpse-alignment path without
  relying on frustum visibility. This artificial placement is validation only.
- Existing 24-case original gib fixture completes with `physics_changes=0`.
- Existing native multiplayer combat/severing/corpse/gib-scale regression passes:
  **430 snapshots / 4,093 simulation ticks**. This is native adapter coverage,
  not a new live internet match or a rendered HeadBall playtest.

Separate full-run timings drifted materially (Original alone went roughly
77→68 FPS). Those runs are retained and are not used to claim a feature cost.
A subsequent frozen-scene off/on/on/off redraw test retains the exact same
24-fragment scene, camera, pose and world time: four 135-frame blocks, first 15
excluded, 120 samples each. Renderer time averages **13.28 ms Original versus
13.63 ms Enhanced**, approximately **0.35 ms / 2.6%** additional renderer time.
Normal updates average **21 microseconds** in the rendered fixture. Frozen
redraws exclude simulation, so these are separate render/update measurements,
not independent display-FPS measurements or Android performance guarantees.

Matching captures were visually reviewed. Original meshes and death animations
remain recognizable; added fragments/marks and wound treatment are visible.
This does not establish every model, map, water/lift interaction or campaign
death. One exploratory run after a failed incremental build used the previous
executable and is explicitly marked discarded in its output directory.

Reproduction switches: `--probe-gore` (requires a GoblinWarrior fixture and
isolated saves), `--capture-gore-comparison`, and `--probe-gore-render`.
