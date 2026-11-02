# Open Rune — Village hall passage and skeletal blocking

October 24, 2026, following `6f606d83`.

The village hall can now be entered with ordinary Android movement. Its weapon
pickup sequence opens the inner door, Ulf's training scene releases control,
normal attack taps defeat him, and his alarm sequence naturally loads SailingShip.
This closes the previously unvalidated village progression gap; it does not
establish the rest of the campaign.

## Reproduced blocker and correction

At `(2277.86, 63.69, -85)`, walking east stopped against `Skinrack0` even though the
visible passage was open. A map trace identified the rack, not the bush or world.
Its 107-radius cylinder overlapped the usable passage beside the intentionally
placed roof blockers. The original actor has `bJointsBlock=true`.

The retained original `USkelModel::LineCheck` evidence selects flagged collision
joints for such actors and falls back to the cylinder only if no collision joints
exist. Our movement trace always used the cylinder. `RuneJointCollision.h` now
transforms those six-plane joint hulls using the same animated skeleton/world
transform as rendering and sweeps the moving actor's width and height against
them. Empty masks, disabled joint blocking and nonskeletal actors retain the
existing cylinder path. Resting face contact permits sliding and moving away.

Original evidence, already retained in this repository:

- `evidence/Engine.SkelLineCheck.cpp`, evidence ID
  `ev_7112d595cf788a8fb6228353c60d93961ddcbde2ae764e4aa3cded11f01fcfe3`.
- `evidence/Engine.LineCheckJointBox.cpp`, evidence ID
  `ev_38d418b7153019860567bf8c09d9218a4066d18bf07bd12c82eb2e32057d0c08`.

This is an implementation based on recovered behavior, not imported mod code.
Full numerical parity, joint-touch callbacks, moving-joint broad-phase bounds,
and overlap/placement-query parity remain unverified. The existing spatial hash
and placement-overlap rules are unchanged. The earlier two-wall candidate remains
deferred and is not in this build.

## Verification

- Desktop and ARM headless suites pass, including a new
  `--probe-map=RagnarVillage2.run --probe-village-collision` fixture: passage clear,
  rack frame solid, empty-mask/disabled fallback, separate width/height extents,
  and tangent sliding.
- Actual Android movement from the formerly blocked isolated save advanced from
  x2277.87 to x2618.28, then entered the weapon room at `(2692.56,353.39,-85)`.
- Ordinary Use actions acquired axe, sword and mace. The inner door opened through
  original events; entering started the original training cinematic. No map,
  actor placement, trigger, health or difficulty override was used for this route.
- The fight was saved/reloaded in isolated quick slot9 during diagnosis. Ulf dealt
  damage and respected his original low-health training safeguard. Aimed separate
  Attack taps reduced his health in 30-point steps to zero. Holding Attack here
  produces one swing, so sustained holds were an ineffective test input.
- Original Ulf defeat/alarm events transitioned to SailingShip without directly
  invoking the exit. The ship cinematic continued with player health8 preserved.
- Normal save hashes and graphics preferences match the pretest baseline. Same
  package/signing key, native2520x1680, 4xMSAA, no device-policy changes.

`--probe-camera --trace-rune-combat` now additionally logs player proxy/weapon
state, attack input and nearby pawn health/position for diagnosis. Both flags
must be selected; normal play has no added logging.

## Performance and evidence

Actual presented FPS at native resolution/4xMSAA: weapon-room sample **92.65 FPS**,
8.34ms median and16.67ms p95. The later sample spanning defeat/alarm/transition
measured **90.05 FPS**, with two intervals over50ms and a275.11ms maximum.
A subsequent SailingShip cinematic sample measured **87.95 FPS**, with16.67ms
p95 and no interval over50ms. The panel was confirmed at120Hz in all samples. These are not sustained120FPS
results or a controlled before/after performance comparison.

Logs, timestamps, preserved-save checks and installed APK digest are in
`evidence/village-hall/`. Original assets and isolated save files remain local.
