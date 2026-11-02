# Open Rune — skeletal interpolation

October 25, 2026. Implemented after the reported choppy character/monster motion.
This fixes confirmed sampling gaps; it does not change animation playback rates.

## Change

`RunePose.h` and `USkelModel::SampleFrame` interpolate local joint positions/scales
and shortest-arc quaternion rotations before building the hierarchy and skinning
once. Previously the renderer blended two already-skinned base frames, while
both samples used the same integer upper-body AnimProxy frame.

`SampleAnimation` samples actor and proxy with their own fractional phase and
loop flag. `CaptureTween` retains the previous local pose for negative-phase
transitions. An interrupted transition starts from its current blended pose.
`ActorPose` combines body groups, then applies dynamics and look offsets once.
Rendering, attached weapons and live joint queries use this shared pose sampler.
Non-looping sequences clamp to the last frame; looping sequences blend last to
first. Original authored frames remain the endpoints.

`TickAnimation`, rates, elapsed time and notify dispatch are unchanged. Combat
`FrameSweep` retains its exact authored-frame sampling path; smoothing does not
replace sweeps with interpolated frames. Transient tween snapshots are not saved:
loading in the middle of a transition safely uses the destination's first pose
until playback resumes. This can produce one visual discontinuity on load, not
an ongoing animation slowdown. Existing save format remains compatible.

Enabled by default. Diagnostic comparison only: Android Intent
`--ei animationSmooth 0` selects the legacy sampler for that launch; `1` restores
smoothing. Desktop/standalone equivalent is `--rune-animation-interpolation=0/1`.
Force-stop the prototype before relaunching with a different comparison value.
No persistent option, graphics preset or refresh rule is changed by this flag.

## Validation

`Tools/RuneValidation/rune_pose_math.cpp` checks 10,000 Rune rotation round trips,
shortest-arc wrap, position/scale blending, exact endpoints and 1,000 random
rotation blends. Maximum round-trip matrix error was 0.000285009.

Desktop and ARM64 `--rune-probe --probe-map=SailingShip.run` pass. The new
`RuneAnimationProbe.h` checks Ragnar, GoblinWarrior and SnowBeast authored
endpoints, loop wrap, transitions/interruption, render/attachment agreement and
safe missing-snapshot fallback. Proxy fractional movement, lower-body isolation
and independent proxy transition also pass.

At 30/60/120 simulated ticks per second, walkforward7 and the actual hand-axe
X1_AttackA have identical elapsed durations and FrameNotify sequences with
smoothing on/off. All 11/15 frame events and one AnimEnd are delivered. At120,
the walking fixture produces88 changing poses versus11 legacy integer poses;
the attack produces37 versus15. Named sound notifies are disabled in this
synthetic timing fixture; actual combat separately exercises game scripts.
This is evidence of fractional pose sampling, not a measurement against the
original Windows executable or proof of every campaign animation.

Device checks and short native4x presentation samples are retained in
[evidence/animation](evidence/animation). The repeatable presentation harness is
`Tools/RuneValidation/android_animation.py`; it uses isolated saves, verifies
native2520x1680/4x, measures SurfaceFlinger timestamps during active combat and
restores preferences. Higher presentation rates do not speed up animation time.

### Installed native4x results

OPPO OPD2515, native2520x1680, 4x MSAA, existing Medium detail/depth, enhanced
bloom off for the comparison. Actual SurfaceFlinger presentation during a
synthetic sustained goblin encounter (health/limbs replenished):

| Target | Sampler | Displayed FPS | Median / p95 interval |
| --- | --- | --- | --- |
| 30 | Smooth | 30.00 | 33.333 / 33.335 ms |
| 60 | Smooth | 60.04 | 16.666 / 16.667 ms |
| 120 | Smooth | 119.05 | 8.334 / 8.334 ms |
| 120 | Legacy | 118.72 | 8.334 / 8.334 ms |

These are short sequential samples, not a controlled sustained performance
benchmark or proof of120FPS in every scene. No material regression appeared in
this encounter. The legacy comparison is close enough that no speedup is claimed.
Hardware FPS remained120. Ordinary attack and shield-block tests pass: goblin
killed with player surviving; shield protected health while intact and damage
resumed after breakage. Both use real game combat callbacks.

Real Android Throw/Use weapon recovery and food consumption pass. Isolated
save-flow (1500 frames) and process-restart save-resume (600 frames) pass. The
installed APK hash matches the packaged build. All10 normal/campaign save files
have unchanged hashes; preferences are byte-identical and the Rune-only120Hz
rule remains3. Final title menu uses campaign-village-ledge, smoothing enabled,
native4x. No normal save slot was overwritten.
