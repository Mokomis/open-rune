# Moving-lift visibility — November 2, 2026

The user confirmed doors were improved by the [stationary visibility cache](LOSTCRYPT-PERFORMANCE.md), but lifts still slowed down sharply. Lift motion changes ray endpoints every frame, correctly invalidating exact cached results and exposing the expensive original BSP visibility traversal again.

## Fix and correctness

`LightingVisibilityBvh` accelerates only the fixed-world half of mover-lighting visibility. It builds a triangle hierarchy lazily from reachable level BSP geometry, excludes the same non-solid/non-visibility-blocking polygons, and clears on map load. Moving doors/platforms remain outside this hierarchy and continue through the existing live non-level collision query. Gameplay ray/sweep collision is not replaced. No quality setting, light count, texture, animation or movement rule is reduced.

The accelerated query retains each polygon's original BSP ancestor branch predicates. Some original polygons extend outside those ancestor halfspaces; a plain triangle hierarchy would add blockers the old traversal did not visit. It also uses the original reciprocal-multiply normalization and float `0.01f` start bias, preserving rounding at exact polygon vertices. Bounds are padded outward and only reject candidates; original plane guards, barycentrics and strict end limits decide hits. These details were required to pass the actual geometry oracle.

`--render-fast-world-visibility=0` selects the previous fixed-world query while keeping the stationary cache enabled. Android's `fastWorldVisibility` extra forwards it only for isolated tests. There is no new ordinary settings-page option.

## Moving workload

The bounded `--probe-lift=auto` fixture selects an original vertical mover and invokes its existing `InterpolateTo` for a four-second movement. It follows the current top face from above, staying inside narrow shafts. LostCrypt uses `Mover10` with 496 units of vertical travel; HelLift uses `RunePolyobj3` with 400. During each 360-frame fixed-120-Hz run the lift moves for 299 frames, with the measured 240-frame span inside the movement. It does not place Ragnar on the lift or test user triggering/rider controls. It only runs with bounded frames and an isolated save directory; ordinary cameras and gameplay are unaffected.

## Actual OPPO comparison

System Qualcomm driver, native **2520×1680, 4× MSAA**, one shadow light, saved WIP/material/detail settings, matched seeds and simulation. These are short engine-throughput profiles, not sustained displayed FPS or a 120 FPS guarantee.

| Moving fixture | Previous / optimized engine FPS | Mean frame time | p95 frame time | CPU scene time |
|---|---:|---:|---:|---:|
| LostCrypt | 30.52 / 75.23 | 32.76 / 13.29 ms | 39.45 / 19.10 ms | 31.42 / 8.61 ms |
| HelLift | 100.86 / 116.45 | 9.91 / 8.59 ms | 19.57 / 11.17 ms | 5.91 / 3.78 ms |

Both final captures are **byte-identical**, and all recorded lift positions/motion counts match. GPU scene time remains close: LostCrypt 10.84→10.39 ms, HelLift 5.38→5.30 ms. The optimization targets CPU queries rather than reducing the rendered image.

The final visible Mac fixtures also have identical captures and motion at native 2816×1762/4×. LostCrypt improves 19.78→65.56 engine FPS; HelLift 81.65→94.15. Earlier exploratory camera/timing runs are retained separately and excluded from these results.

## Regression coverage

- UBSan: 50,000 rays against an independent analytic cube-plane oracle, plus degenerate/short rays and explicit BSP ancestry acceptance/rejection.
- Mac: 6,000 accelerated-versus-original world queries each on LostCrypt, HelLift and Hildir, including exact mesh vertices and nearby offsets; all 18,000 match.
- Actual ARM64 executable: the same 6,000-query comparison on LostCrypt and HelLift, plus the existing translated/rotated/scaled brush, world-surface flags, moving-occluder and legacy-Mover fixtures; all pass.
- Actual Android moving-lift renderer comparisons on LostCrypt and HelLift: exact image/motion comparisons, native/4× checks and isolated save/preference preservation.

## Installation and preservation

The signed update is installed with the existing key and optional Turnip payload. Tests use the system driver. The automation returns to normal LostCrypt without test-save overrides. At test completion the original saves and preferences matched exactly; normal play afterward generated new checkpoint autosaves. The current live checkpoint was left intact, and the pre-test checkpoint was additionally copied into previously unused **Manual save 9** (`Save8.rsa`) with a backup label and verified matching hash. Manual save 1 and all graphics settings are preserved; saved scaling remains Hardware 50%, AA 4× and shadow maximum three. No saved game was loaded by the test or replaced with an older copy.

Windows was not rebuilt or changed. No server, sound-pack work, commit or push. Evidence outside Git: `outputs/lift-performance-20261102/` above `work/SurrealEngine`, including exact commands, oracle/build logs, Mac and Android captures/profiles, normal-data backups, checkpoint-preservation record and installed APK verification. This covers the tested lifts and engine path, not a campaign-wide playthrough.
