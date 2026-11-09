# Offline stair presentation — November 3

The tablet's isolated DM-Hildir practice game uses the offline RunePlayer path.
The existing multiplayer stair compensation does not run there. In the offline
path, collision climbs 16-unit stair treads immediately while the original
scripted camera smooths its anchor. Ragnar therefore jerks against a comparatively
steady background, matching the earlier human report.

`RuneStairPresentation` now follows that existing camera anchor after a small
grounded step. It translates only the rendered player and held equipment/body,
their rendering bounds and the bloodlust head halo. Authoritative actor/joint
transforms, collision, input, damage, attachment positions, scripts and the
camera calculation remain unchanged. There is no protocol or save-format change.

The bounded transient offset resets for map/player changes, jumps, death,
cinematics, camera overrides, first-person views, teleports, large ledges or long
frame stalls. Pausing holds the presentation. Ceiling-induced camera movement on
flat ground cannot start stair compensation. Multiplayer replicas retain their
existing shared mesh/camera smoothing and do not receive a second offset.

## Mac results

Matched offline ascent/descent through DM-Hildir, native 2816×1762, 4× MSAA,
120 Hz deterministic simulation with a 120 FPS cap:

| Metric | Original | Fixed |
| --- | ---: | ---: |
| Ascent height range | 208 | 208 |
| Descent height range | 208 | 208 |
| Maximum physical step | 16 | 16 |
| Ascent body/camera vertical jolt | 15.2838 | 0.0848 |
| Descent body/camera vertical jolt | 15.7112 | 0.0010 |

Every logged physical position, camera position and anchor position is identical
between the two runs. The maximum visual compensation is about 29.3 world units;
it can slightly overlap stair treads, as presentation smoothing commonly does.
Reviewed captures show aligned equipment and the expected player silhouette.
The test measures presentation discontinuities, not human comfort or sustained
120 FPS performance.

The existing authenticated multiplayer stair route also passes: 208-unit ascent
and descent, no backward steps, and maximum unobstructed body/camera jolt below
0.001 units. The standalone UBSan test covers 3,554 sampled stair updates at
30/60/120/144/240 Hz plus pause, jump, teleport, camera and frame-stall resets.

Evidence: `outputs/stairs-local-20261103`.

## Installed Android check

The signed update is installed on OPD2515. Actual native 2520×1680/4× MSAA
ascent/descent comparisons reproduce the same numbers above, including identical
physical and camera routes. Captures were reviewed. All normal saves, system
files and preferences (113 files) match their pre-install hashes. The app returns
to its normal title menu with the user's Hardware 50% setting retained; native
rendering was a test override. The user confirmed the stair issue fixed after installation.

`android_local_stairs.py --device DEVICE --output NEW_DIRECTORY` automates this
bounded isolated check. Its `localStairProbe` intent requires a separate save
directory and 1,100–1,500 bounded frames; ordinary launches do not inject input.

## Reproduce

```
python3 Tools/RuneValidation/local_stairs.py --binary /absolute/SurrealEngine \
  --game /absolute/test-game --output /absolute/new-output --captures
```

The harness requires a fresh output directory and creates its own saves/config.
It compares the production default against `--rune-local-stair-smoothing=0` in
the same executable, then asserts unchanged simulation/camera and reduced jolts.
For the pure state test, compile `Tools/RuneValidation/local_stairs_test.cpp`
with C++20, UBSan and `-I SurrealEngine`.
