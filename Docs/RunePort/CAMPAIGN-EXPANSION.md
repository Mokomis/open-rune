# Open Rune — Campaign expansion — work in progress

October 24, 2026. This pass starts from `fea5179a`. The changes below are shared engine mechanics; they are not proof of a completed campaign playthrough. The user requested native-resolution displayed-FPS checks only.

## Scope and evidence

- Rune Polyobj inherits Brush, not Mover. Rendering, lighting, collision bounds, ray/box traces, overlap and keyed motion now recognize it separately. It supports the original 16-key property layout. Pure rotation updates collision bounds. Headless fixtures borrow original SailingShip and HelLift geometry and verify actual InterpolateTo/InterpolateEnd scripts, key15, smooth glide, and the existing eight-key Mover control. Tilted-platform rider movement and full original transform parity remain unverified.
- Native WaitForRelease/Release share original latent poller384 with Sleep; Release wakes only that actor and does not queue a future signal. Save reads retain legacy prototype index257. Original ScriptAction.Trigger is exercised by real VM bytecode fixtures.
- FindWaterLine bisects water-zone membership with the body's current zone as reference. Tests cover both crossing directions, horizontal boundaries, no crossing and zero length. These are native semantics tests; actual swimming/surfacing still needs device coverage.
- Polyobj destruction queries668/669 now return transformed bounds radius and the most common visible surface texture (first on ties). StopAllSound268 clears effects while preserving music and viewport, checked with a backend spy; audible output is not established by this test.
- Rune camera interpolation now uses its two original cubic curves, instant-point cuts and wrapped spline rotations. It advances Target before invoking reached-point events, so pauses/exits are no longer one point late. Original CineCamera pause, Timer and endpoint destruction callbacks are tested, including temporary-node cleanup, owner FOV/flash, old-alpha rate/time dilation, exact endpoint threshold and original integer rotation rounding. Non-Rune interpolation is unchanged.
- Polyobj BSP surface indices refer to the level model, even when outside the local brush surface array. Ray/box tracing now resolves those flags against the correct model, fixing a second HelLift crash exposed during dynamic lighting. A regression fixture verifies solid/non-solid ray and lighting traces with a deliberately out-of-range local index.
- Falling physics stops handling a rejected move when there is no collision actor, fixing the null dereference observed at HelLift. It also rechecks destruction/physics changes after movement callbacks.
- Rune flying physics preserves vertical velocity required by original rope-climbing PlayerMove. RuneRopeProbe uses actual placed ropes and original Touch/PlayerMove/Jump scripts; it injects input fields, not Android touch input. The first Android vine fixture climbed but exposed missing Core.Object.VSize2D662 at jump-off; a Rune-only binding and exact recovered XY-magnitude fixture are now included in the installed build.

## Camera recovery

Original Engine.dll SHA-256: `41458bac7e717a57effbe45d9351664ab79d72d7b821c3103018d057b7c585d0`. `physPathing` export at0x10301785: REA `ev_18dc15f6686367099b0e195ff51c4cba576beeddc67d494bcdcff4e6d88c2894`. Angle helper0x103cfbd0: `ev_6c54f636a5e0089da3c59cf3e73069078cff97f913e8319a57dac82f79ca1302`. Pseudocode/evidence retained alongside this report; static recovery is not an observation of the original running game.

The helper selects equivalent16-bit angles nearest to the prior angle, retaining exact half-turn ties. Curves use Catmull-Rom when bSplineThruPoints is set and cubic B-spline otherwise, with linear fallback when adjacent points are at most16units apart or neighbors are missing. Instant cuts are selected by the next point. A bounded malformed instantaneous/skip-cycle guard prevents a hung frame.

## Baseline device coverage

The old installed build completed RagnarVillage narration naturally into RagnarVillage2 within233seconds, without injected input or exit calls. Opening native presentation sample:119.825FPS, p95 8.335ms, zero intervals over50ms.

Ten subsequent entry checks ran on that build: SailingShip, SinkingShip, SinkingShip2, DeepUnder1Odin, DeepUnder3, Hel1, Hel1a, Hel1a2, Hel1b, Hel3a. Hel1a2/Hel1b player died while idle; these are loading/render/script observations, not traversals. HelLift exposed the falling crash above. SailingShip measured114.14 displayedFPS; underwater/early Hel samples were generally118–120. New-build measurements must supersede these before performance claims.

## Current validation

The collision checkpoint installed with the existing package/signing identity passed
HelLift entry for 63.22 seconds. Its independent native-resolution presentation
sample measured 119.242 FPS over 36.371 seconds (4,338 frames), p95 8.331 ms,
maximum interval 16.662 ms and no intervals over 50 ms. SurfaceFlinger hardware
mode remained 120 Hz. The inspected screenshot shows the level, Ragnar and HUD;
this is a stationary entry check, not lift traversal.

The newer desktop particle/camera integration passes the full headless suite on
HelLift, including camera endpoint cleanup, Polyobj geometry/motion, world-surface
collision and RuneParticleProbe. Initial compilation exposed API/header/type
mismatches in the new particle code; those were corrected before the passing run.
The first Android particle build also passes these headless checks. HelLift
entry measured 118.664 FPS. Hel3a now visibly renders both fires at entry but
measured only 67.637 FPS, with CPU tick averaging 10.169 ms; SailingShip measured
94.256 FPS, CPU tick 6.370 ms. The display remained at hardware 120 Hz. These are
performance regressions under investigation, not accepted 120 FPS results.
One separate Hel3a sample was interrupted by New fight and is excluded.

The next build reduces per-particle reflection lookups, hoists emitter constants,
and adds opt-in tick/bounds/draw timing. Its desktop native suite passes, including
new nonidentity SystemCoords and VSize2D tests. The optimized Android build passes the original DeepUnder3 vine attachment, climbing, stopping and jump-off fixture at 60/120 simulation steps, plus isolated save-flow and fresh-process resume fixtures.

The collision/camera checkpoint also completed the original opening naturally
from RagnarVillage into RagnarVillage2 in 231.65 seconds, restoring PlayerWalking
with no ViewTarget. Its opening sample measured 119.909 FPS, p95 8.332 ms,
maximum 16.663 ms. No input or exit invocation was injected.

NearestNavPoint664 and Canvas.TransformPoint492 now have recovered implementations
and passing fixtures. The nearest-nav fixture exercises the original flee helper;
projection checks cover depth/rounding/offscreen behavior. See CAMPAIGN-GAPS.md.

## Latest particle performance checkpoint

Interning fixed property names and restoring the original hidden-system guard
reduced Hel3a particle update time to 0.799 ms (previous repeat: 5.615 ms), with
CPU tick at 3.508 ms. The native 4x-MSAA presentation sample reached 92.250 FPS;
SailingShip measured 105.830 FPS. A preceding optimized build measured 119.168 FPS
in HelLift. Hel3a's inspected image shows both entry braziers burning. None of
these stationary samples establishes performance throughout those levels.

A native-resolution diagnostic with antialiasing disabled measured 113.802 FPS.
It is a sequential diagnostic, not a controlled thermal comparison or acceptance
of reduced image quality. Default 4x MSAA is retained. The rendering audit and
next validation requirements are in VULKAN-OPTIMIZATION-PLAN.md. No renderer
optimization from that plan is implemented in this checkpoint.

## Remaining work

Broader Android regression/deployment of the newest particle/camera integration, natural cinematic progression with corrected events, real rope traversal, native FPS in later maps, full route traversal, particle/beam visuals and lifecycle, and remaining evidenced native bindings. See CAMPAIGN-GAPS.md and CAMPAIGN-ROUTE.md. The new rope/fire renderer addresses gameplay-relevant invisible effects; broader device visual and traversal verification is still needed; do not claim full campaign compatibility from successful map entries.

### Sequential repeat caveat

After the no-MSAA diagnostic, restoring native 4x MSAA measured 54.375 displayed
FPS in another stationary Hel3a sample. CPU tick increased to 5.923 ms and the
combined GPU scene/resolve/postprocessing interval to 13.958 ms. The earlier
92.250 FPS sample is therefore not a stable sustained result. These sequential
runs do not isolate device state, and the source of this variation is unresolved.
Do not attribute the full difference to antialiasing or advertise a fixed gain.
The latest ARM headless suite passes, including SystemCoords, Core.VSize2D and
hidden-particle assertions.

Final installed-build regression also passes DeepUnder3 vine climbing/jump-off,
original exit/save-flow and fresh-process resume. All four normal save hashes
remain unchanged across this final isolated regression. Save10 and its backup
already differed from the older baseline before that regression; their cause is
not established. Current files were retained and backed up locally, not restored
or overwritten. See campaign-final-save-check.json.
