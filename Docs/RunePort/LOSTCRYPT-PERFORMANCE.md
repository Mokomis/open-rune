# LostCrypt mover-lighting visibility — November 2, 2026

The user reported low FPS in DM-LostCrypt even at one shadow-casting light. A read-only live OPPO sample at Hardware Scaling 50%/4× AA measured approximately 25 displayed FPS and only 32–37% GPU busy time. CPU sampling pointed strongly to mover lightmap collection repeatedly casting visibility rays through level geometry before its existing lightmap-cache check. The simpleperf frame-pointer callchains were incomplete, so inclusive percentages are diagnostic clues rather than exact frame-time attribution. Controlled tests below confirm the repeated work was material.

## Change

Only the fixed level-BSP half of each mover-lighting visibility ray is cached. `TraceWorldPart` separates the original complete ray query into level-only and non-level parts with the same margins, visibility flags and ignored receiver. The ordinary query still defaults to All. For lighting, a cached fixed-world blocker can terminate the query; otherwise the moving-brush part is tested anew every call. Closed/open doors, moving platforms and collision changes on moving actors therefore remain live.

`WorldVisibilityCache` belongs to a lightmap, retains exact ordered start/end coordinates, and invalidates entries when a face or light moves or the queried ray sequence changes. Old tail entries are dropped, and all entries are destroyed on map loading along with the original lightmap cache. Light brightness, flicker, wave effects and material updates are not cached by this helper. Level BSP geometry is immutable during ordinary play; moving geometry remains actor/brush geometry and is deliberately outside the cached result. Original non-pixel lighting retains its existing path.

`--render-mover-visibility-cache=0` retains the original complete visibility queries for diagnostic comparisons. Android exposes the `moverVisibilityCache` intent extra only with isolated `saveTest` runs. No graphics settings were reduced, and no map assets, gameplay, collision outcomes or network rules changed. The improvement applies to this engine path across maps rather than using LostCrypt-specific exceptions.

## Validation

- UBSan helper tests verify unchanged fixed rays are queried once, moving occluders are still checked every time, and endpoint, map/cache lifetime and ray-list changes invalidate the memo.
- The real Polyobj fixture compares complete-query results against cached level/non-level results, including fixed endpoints with a translated occluder. LostCrypt and HelLift each pass 15 oracle comparisons with one fixed-world trace, plus existing rotated/scaled surface, collision, movement and legacy-Mover checks. Mac and the actual Android ARM64 executable pass.
- Matched Mac LostCrypt, HelLift and gong-triggered Hildir runs use native 2816×1762, 4× MSAA, one shadow light, fixed 120 Hz simulation and identical seeds/settings. All three final captures are byte-identical. This includes current-position brush lighting and an opened hatch; it is not visual proof of every intermediate animation frame or all campaign scripts.
- Actual OPPO comparison uses native 2520×1680, 4× MSAA, one shadow light, WIP 4× texture artwork and the saved material/detail settings, system Qualcomm driver, fixed seed and 120 Hz simulation. Final captures are byte-identical, sampled player/camera state matches, and normal saves/preferences are unchanged by the test.

| Matched OPPO sample, 240 measured frames | Reference | Cached |
|---|---:|---:|
| Mean frame time | 24.94 ms | 12.06 ms |
| Engine throughput | 40.10 FPS | 82.90 FPS |
| p95 frame time | 25.51 ms | 13.56 ms |
| CPU scene preparation | 23.61 ms | 6.91 ms |
| GPU scene | 10.33 ms | 10.16 ms |

The remaining GPU time stays nearly unchanged, as expected from retaining the image. These short sequential fixed-step samples establish the benefit in the matched scene; they are not sustained combat/display measurements or a locked-120 claim. Mac LostCrypt throughput improves about 25.05→48.89 FPS; HelLift 16.16→31.72; the Hildir gong view is already mostly GPU-limited and changes 59.12→60.60.

## Delivery and current state

The final signed ARM64 update is installed and its APK bytes match the local package. The current normal session is DM-LostCrypt with normal save access, no `saveTest` override. All original save/backup hashes match. The user's later eight-light selection and its legacy feature bit were preserved; all other preferences match the pre-test backup. Earlier one-light measurements and the current eight-light session are not a controlled pair.

A subsequent read-only display sample at the saved **Hardware Scaling 50%, 4× AA, eight-light maximum** reports about **67.66 displayed FPS** over 13.17 seconds, with no intervals over 50 ms. Scene/input/clock conditions differ from the earlier 25 FPS report, so this is current-state evidence rather than an isolated speedup ratio. Keep native/4× comparison numbers separate from this scaled result.

Windows was not rebuilt or changed in this task. Mac and Android source builds pass. Optional Turnip payload and signing key are retained; no sound pack, server, commit or push was added.

Evidence outside Git: `outputs/lostcrypt-fix-20261102/` above `work/SurrealEngine`. Includes exact scripts/arguments, reference/optimized captures and profiles, native oracle logs, final display timestamps/clock context, APK hashes, original data backup and preservation checks. The earlier live profile is in `outputs/lostcrypt-performance-20261102/`.
