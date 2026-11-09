# Combat animation playback

November 7, 2026. Installed on Android; Windows main executable and installed Mac app updated. User requested an end to testing and shutdown of the live match; no match restarted.

## Change

The legacy animation fraction used elapsed time since packet arrival divided by the previous arrival gap. Irregular arrivals caused pauses and catch-up jumps even though fractional skeletal sampling was enabled.

Confirmed non-looping, positive-rate living-pawn animations now use a persistent render playback clock. The authored rate is preserved with bounded75–125% phase correction, monotonic progress within the same clip and at most50ms visual lead beyond the latest confirmed phase. Playback cannot exceed the authored final frame. New clips, same-clip restarts, models and lives reset; long gaps reanchor. The intermediate untimed placement during snapshot installation does not clear playback clocks.

Negative-phase windups, loops, stopped clips, corpses and special physical/bloodlust states retain their previous paths. No animation notifies, attacks, damage or local gameplay scripts are executed by the clock. Wind-ups, fixed server simulation, input admission and the existing two-tick late-block window are unchanged. Local attack/block anticipation remains before confirmation, and reliable damage still cancels anticipation immediately. A same-sequence negative tween now captures its displayed source instead of reusing stale tween data.

## Evidence

Controller: `~/OpenRuneArtifacts/CombatInterpolation-20261107`; archived receipts/source under `C:/OpenRune/outputs/combat-interpolation-20261107`.

- Real native character replay,60/120/144/165/240Hz, jitter/drop, finite bone transforms, no continuous-strike rewind, bounded per-frame progress, restart/tween/life/gap guards passes. At120Hz:22 sampled legacy holds versus0; RMS rate error1.30315 versus0.134686.
- Native first-render-frame attack and shield/crossed-block response at8ms passes without authoritative input/damage effects. Native combat430snapshots4093ticks and dual/skin/loot/save/drop checks pass.
- Original twelve melee-window cases pass, including late block, retained weapon window and no committed-damage rollback.
- Two encrypted Mac clients with50ms each-way latency,jitter8ms,1% loss pass expected dual attack/guard states7. Matched reference/current runs use actual2816x1762 drawable,4xAA,scale1,identical camera/features. Native-client remote sampled attack holds29→0, RMS phase-speed error1.11836→0.187819. These are animation-cadence measurements, not FPS gains.
- Captured135 full-motion frames of the corrected native scene; both hands/recovery reviewed. An earlier reference window2816x1484 is not used for matched native-resolution claims.

The Mac graphics fixture still contained an isolated articulated-shadow prototype. Before deployment that prototype was preserved outside Git and the affected Mac render/engine files were restored from Windows primary. The playback source stayed identical. The final Mac app was rebuilt/signed without that unrelated prototype; no new gameplay test was run after the user's stop instruction.

## Deployment

Android APK SHA28b594de70ac7acbca1f51b17c7eb0cdad0faf0a6333a2b65d1cd9f058d41cd8, original certificate and notices verified, install-in-place succeeds; nine saved-game/settings files unchanged. Normal launcher opened, no private match joined. Installed Mac app binary/resources updated and locally signed, not launched. Windows main executable rebuilt; private match and ten bots stopped, match/recovery scheduled tasks Ready. No commits/pushes.

Diagnostic opt-out: `--rune-combat-playback=0`. `--rune-combat-playback-probe` with the native network fixture runs the phase regression. `--rune-net-animation-trace=PATH` requires isolated saves and writes presentation/authoritative phases; `--rune-net-fullscreen` is an isolated native-drawable fixture flag. Neither adds a settings page.

Courtyard FPS diagnosis and visibly stronger character shadows remain open. This change does not claim to solve either rendering issue.
