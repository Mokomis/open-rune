# OPPO 120 Hz timing follow-up

October 28, 2026. The user changed the game to **120 FPS / 120 Hz / pacing on**
and explicitly skipped further thermal investigation. Native 2520×1680 and
4× MSAA remain the performance-test settings; saved Hardware Scaling 50%
remains unchanged. No thermal tuning or heat-soak comparison was performed.

## Finding and retained adjustment

The app correctly requested 120 Hz and supplied Swappy a 120 FPS target, but the
existing OPPO package-specific rule still forced 144 Hz. Swappy initialized with
`target_fps=120 refresh_ns=6944443`. The hardware compositor confirmed 144 Hz.
Displayed frames arrived at approximately 13.89 ms intervals (72 FPS), consistent
with a two-refresh-interval pacing ceiling. This was a timing bottleneck on top
of the renderer cost; it does not explain the earlier thermally limited 42 FPS
observation, which used different settings.

Aligned only `org.runeclassic.port` with the user's chosen 120 Hz:
- `persist.opdrr.42800e61`: **4 → 3**.
- Re-evaluation: `service call oplusscreenmode 12 s16 org.runeclassic.port i32 3`
  using the already installed refresh-manager hook.
- Swappy now reports `target_fps=120 refresh_ns=8333333`; the hardware compositor
  confirms `FPS min:120 max:120 cur:120`.
- All other `persist.opdrr.*` properties match their pre-test snapshot.

The policy was briefly switched back to 4 for a reference repeat, then left at
3. No root module, global display setting, clocks or thermal limits changed.
The ordinary APK still requests refresh through Android APIs; it does not write
root refresh rules. Other devices require their own measurements.

## Repeated native 4× comparison

Same build, scene, camera and effects (world features 40871 including medium AO),
120 FPS target and pacing enabled. The only retained test-variable change is the
OPPO per-app panel rule. Actual SurfaceFlinger timestamps, approximately 13
seconds per sample:

| Panel policy | Displayed FPS | p95 interval | Longest interval |
| --- | ---: | ---: | ---: |
| Forced 144 Hz | 72.05 | 13.89 ms | 13.89 ms |
| Matched 120 Hz | 102.30 | 16.66 ms | 16.66 ms |
| Forced 144 Hz repeat | 72.05 | 13.89 ms | 13.89 ms |
| Matched 120 Hz repeat | 101.01 | 16.66 ms | 25.00 ms |

Approximately 40–42% more displayed frames in this view. This is not locked
120 FPS, a whole-campaign guarantee, or a sustained thermal result. The matched
run has some missed 120 Hz deadlines, so its p95 interval is longer despite
higher average FPS. No sample contains a gap over 50 ms.

Read-only GPU clock samples were predominantly 1225 MHz. Scene GPU work remains
about 7.8–7.9 ms. The reference spends approximately 3.8–4.0 ms in presentation;
the first matched run spends approximately 0.20 ms. The native rendering library
is byte-identical to the previous retained lighting-buffer build.

## Rejected shader experiment

Before identifying the timing ceiling, an experiment put per-draw lighting in
128-entry uniform-buffer pages. Hall ABBA scene GPU times were
7.898 / 7.817 / 7.838 / 7.919 ms: about 1% saving, with no displayed/engine FPS
benefit under the pacing ceiling. The additional page/batch complexity was not
retained. All eight modified experiment files were restored byte-for-byte from
the pre-pass snapshot, preserving prior uncommitted work. The existing packed
HDR and frame-wide uniform-lighting optimizations remain.

## Validation and handoff

The Android multiplayer client joined the laptop host and completed a brief
scripted combat check with 100 ms added RTT and ±5 ms jitter. Seven contact
outcomes were recorded. Actual displayed FPS averaged **99.93** over 22.26
seconds, p95 interval 16.66 ms, longest 25.00 ms, no gaps over 50 ms. Native
2520×1680 / 4× AA / world features 40871 / 120 target and panel were verified.
No script or device-loss errors were logged. This is a brief joining/combat
sample, not an endurance or tablet-host player-capacity claim.

The installed APK matches its built hash. Save0, Save10 and their backups retain
the original hashes. MainActivity preferences match the snapshot taken after
the user's 120 settings change byte-for-byte. All other refresh-manager rules
are unchanged. Test hosts and clients are stopped and the ordinary menu is
restored with saved Hardware Scaling 50% / 4× AA and 120 FPS / 120 Hz pacing.
The final APK SHA-256 is
`6417a0a0e5f8fbea9873a895ef610f4319635a2e2ec1d4f1071665da5515d52c`.
The rebuilt native ARM64 library matches the preceding retained build exactly;
its prior production pixel-probe evidence applies. The full laptop multiplayer
suite was not repeated.

Local scripts, policy snapshots, captures, profiles and presentation timestamps:
`../../outputs/mesh-lighting-pass-2026-10-28` relative to the repository.
No commits or pushes were made. To deliberately return to the old 144 Hz setup,
change both the game target/request and this package's rule together; do not
silently restore policy 4 while the user wants 120 Hz.
