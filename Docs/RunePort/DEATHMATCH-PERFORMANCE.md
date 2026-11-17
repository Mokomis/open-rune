# Deathmatch performance sweep — November 8, 2026

Completed bounded coverage of all 25 installed deathmatch maps requested for this pass. DM-Hildir and DM-Skorir were excluded as requested. This is not an every-room/full-match certification.

Follow-up: the ChAoS black-image finding is **resolved**. The capture caught an authored startup fade whose partially transparent canvas tile was incorrectly drawn opaque. The original world becomes visible after the fade; the corrected shared canvas path now fades smoothly. Ten post-startup views have been rendered and reviewed. See [Canvas alpha follow-up](CANVAS-ALPHA.md). Original sweep results below remain historical timing evidence.

## Coverage and measurement

- Mac: five spatially spread, grounded positions per map, with opposite headings at each position: 250 profiled views. Player position follows the sampled camera region so shadow selection remains relevant.
- Each valid profile contains 160 frames after warmup. Actual scene textures and readbacks are 2816×1762 with four MSAA samples. The presentation window sometimes resized to 2048×1488, so explicit isolated-test render extents were used. These results measure full native scene workload, not fullscreen display cadence or sustained displayed FPS.
- Windows: ten autonomous bots per map, private localhost hosts, 40-second runs, all battle laws, Chaos physics. All 25 hosts and ten-bot joins completed. Some maps had no contacts in this short interval; those are load checks rather than proof of combat everywhere.
- Optimized look-target iteration was checked against the original candidate order throughout a second 25-map ten-bot pass. All passed; original script scoring and tie behavior remain in use.
- Mac captures of all 25 worst sampled views were collected and reviewed, with supplemental normal-camera views for close-wall/black captures. ChAoS was initially held; the startup-fade follow-up above resolves that hold. Three paired reference/optimized captures are pixel-identical, with matching native scene size and four samples.
- No tablet installation or Android performance claim in this pass. Public Windows match remains stopped. Original packages, normal saves and settings were not modified.

## Retained change

The stock RunePlayer look-focus query used VisibleActors to trace nearby scenery, lights and effects, then discarded actors that cannot be look targets. The native iterator now rejects those irrelevant actors before visibility traces, only for the exact stock DetermineLookFocus call, receiver, Actor base class, 1000-unit radius and location. Other queries and overridden routines keep the general iterator. Known RuneI, RuneI101 and RuneI107 packages are scoped explicitly. Original scoring still executes.

Fallback: `--rune-look-focus-filter=0`. Correctness comparison: `--rune-look-focus-verify` (diagnostic only; it runs original visibility work too, so do not benchmark that mode). No gameplay rules, AI perception distances or graphics settings change.

LostCrypt initial ABBA active-bot runs: reference means 10.91 / 13.35 ms; filtered means 6.31 / 5.51 ms. Autonomous fights diverge, so these are repeated matched seeded starts, not identical input replays. Follow-up uses the cleaned production build without per-actor timers.

| Windows map | Baseline mean / p95 tick ms | Optimized mean / p95 tick ms | Mean reduction |
|---|---:|---:|---:|
| DM-LostCrypt | 13.07 / 16.99 | 6.15 / 7.71 | 52.9% |
| DM-Team_ships | 8.46 / 12.76 | 5.60 / 8.73 | 33.8% |
| DM-Ingles | 4.59 / 7.80 | 3.97 / 5.43 | 13.5% |

Steady comparisons use diagnostic intervals 5–14 (approximately 10–28 seconds), excluding host startup and bot departure. Maps/bot fights vary; figures are workload observations, not guaranteed end-user FPS gains.

## Rendering findings and rejected work

The main scene GPU pass dominates most sampled slow views. Shadow rendering is a much smaller cost in these samples. LostCrypt also has substantial CPU scene preparation in its worst view. The retained look-focus change addresses CPU simulation, especially hosts with many players/bots; it does not eliminate the GPU pixel workload.

Three rendering candidates were rejected: skipping unused relief work, early masked-alpha rejection, and disabling opaque ONE/ZERO replacement blending. Captures were unchanged, but timings showed no reliable useful gain (some were slower). None is enabled in production. No quality reduction, per-map special case, shadow distance reduction, or resolution/MSAA change was retained.

## Bounded follow-up and known limitations

- Ingles was additionally sampled at its slow view: 1909 of 2312 main-thread samples waited on the GPU fence. BSP span clipping was the largest individual application leaf (77 samples), but this does not establish a comparable GPU optimization or justify changing visibility behavior. No speculative culling change was retained.
- **DM-ChAoS initial visual hold (subsequently resolved):** the world is black in inspected fixed-camera and normal chase-camera views, with both enhanced world features and stock feature settings. It loads and passes ten-bot/reference-order checks; it is not a visual success. Cause remains unestablished. Its timing is retained as diagnostic workload only, not a representative playable-view result.
- Bothvar and TEAM_MadKing include close-wall/rock sample views. Supplemental normal chase-camera captures were reviewed. Sampling is intentionally bounded and does not certify all corridors, lifts, secrets, firefights, or match lengths.
- Large GPU costs remain in some native 4× AA views, particularly IceCavern and Ingles. No claim that every map now meets 120 FPS is made.

## Per-map sampled rendering

Worst sampled view, not a sustained match average. GPU scene/shadow columns refer to separately timed passes. CPU scene is preparation/draw submission, not total simulation.

| Map | Worst frame ms | GPU scene ms | GPU shadows ms | CPU scene ms | Baseline host p95 ms |
|---|---:|---:|---:|---:|---:|
| DM-AcidChamber | 16.12 | 11.28 | 0.71 | 2.07 | 6.14 |
| DM-Alfheim | 21.16 | 16.99 | 0.35 | 2.18 | 7.14 |
| DM-Bothvar | 16.69 | 12.02 | 0.78 | 1.72 | 6.50 |
| DM-ChAoS | 21.43 | 16.63 | 0.17 | 2.24 | 6.13 |
| DM-Coliseum | 13.73 | 10.28 | 0.32 | 1.45 | 8.14 |
| DM-DwarfForge | 18.57 | 13.29 | 0.60 | 2.34 | 6.96 |
| DM-Gates | 21.26 | 16.69 | 0.79 | 1.92 | 6.86 |
| DM-Havlamahl | 22.02 | 14.54 | 0.68 | 4.74 | 9.31 |
| DM-Hel | 14.68 | 10.68 | 0.82 | 1.42 | 4.81 |
| DM-Hel2000 | 17.87 | 11.66 | 0.66 | 3.14 | 7.89 |
| DM-HelEvator | 15.75 | 10.23 | 0.47 | 2.77 | 6.71 |
| DM-Hudson | 21.21 | 17.31 | 0.20 | 1.90 | 8.56 |
| DM-IceCavern | 24.45 | 22.33 | 0.15 | 0.83 | 10.89 |
| DM-Ingles | 30.39 | 17.44 | 0.66 | 6.98 | 7.80 |
| DM-LavaPit | 13.87 | 10.27 | 0.53 | 1.54 | 4.47 |
| DM-LostCrypt | 24.36 | 11.67 | 0.75 | 9.01 | 16.99 |
| DM-Ruins | 13.09 | 9.99 | 0.00 | 1.24 | 7.13 |
| DM-Sanctum | 14.36 | 9.85 | 0.86 | 1.86 | 8.44 |
| DM-TEAM_MadKing | 16.13 | 11.27 | 0.28 | 1.73 | 10.77 |
| DM-Team_chasm | 16.84 | 11.49 | 0.82 | 2.52 | 9.12 |
| DM-Team_ships | 18.77 | 13.97 | 0.47 | 2.62 | 12.76 |
| DM-Thorstadt | 16.97 | 12.85 | 0.53 | 1.84 | 6.57 |
| DM-Zorn | 21.81 | 17.17 | 0.66 | 2.44 | 8.26 |
| DM-geothermal | 19.80 | 16.37 | 0.00 | 1.40 | 9.33 |
| DM-seaside | 16.98 | 13.58 | 0.00 | 1.42 | 7.92 |

## Reproduction and evidence


Windows counterparts: `C:/OpenRune/outputs/deathmatch-performance-20261108`. The retained candidate is in the Windows primary checkout, and its Mac mirror is built and functionally tested. No Git commit or push was made. Public match remains stopped.

Invalid earlier resized-window profiles, missing verification coverage and harness/invitation setup failures are preserved separately and excluded from success counts.
