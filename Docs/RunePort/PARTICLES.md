# Open Rune — particles and climbable beams

## Evidence and scope

Rune's `DT_ParticleSystem` is draw type 8. Original `ParticleSystem.uc` declares
64 reflected `Particle` structs, four particle textures, emitter parameters,
32 beam connection points and independent callback replacement flags. Before
this implementation the renderer had no draw-type-8 path. Placed Rope,
ClimbableVine/Chain, FireSpewer and SteamSpewer actors could still execute their
ordinary scripts while invisible. Spewer's repeating damage timer therefore
remained a gameplay hazard. ZombieBreath and MushroomPuff stop their damage
timers and set one-shot particle flags without setting Actor.LifeSpan; missing
particle completion also left their actor cleanup incomplete.

`Engine.TickParticleSystem.cpp/json` retains REA evidence
`ev_1c753363b212bf749c9608ed4dbf2056979b72025d5c60d9547189fb0e109f2b`.
Its dispatch table selects emitter, swipe, generic or beam functions. Each of
`bEventSystemInit`, `bEventSystemTick`, `bEventParticleTick` replaces the
corresponding native stage. Initialization precedes the paused-state check.

REA/Ghidra did not recognize the six internal emitter/beam dispatch entries as
functions, either at their jump thunks or their destinations. The focused
fallback `Tools/RuneRecovery/RuneParticleFunctions.java` imports the unchanged
binary into a temporary Ghidra project, explicitly disassembles/defines only
those entries and exports pseudocode. `Engine.ParticleDispatch-recovery.json`
records the artifact SHA-256, addresses and output hashes. These files have
explicit Ghidra provenance, **not REA Evidence IDs**. Imported vector helper
calling conventions are partially unresolved; the pseudocode is not original
compilable source.

The reconstructed emitter supports delayed initialization, random-delay restart,
component-range spawning, directional velocity, convergence, gravity and zone
velocity, scale/alpha evolution, bounded/water-only particles, respawn, one-shot
completion and SystemLifeSpan destruction. Scale and alpha use the original
pre-decrement fraction against LifeSpanMax. The shipped initializer unconditionally
chooses texture zero after its random-texture branch; this behavior is preserved.
Spewer scripts can grow ParticleCount without resetting live slots.

### One-time SystemCoords spawn transform

The retained `Engine.ParticleEmitterSpawn.cpp` calls `FVector::TransformPointBy`
when HasValidCoords is set, then recomputes the spawn displacement used by
bConvergeX/Y/Z. A focused read-only instruction check resolves the implicit
receivers that the initial pseudocode did not express:

- Engine.dll `0x103a25e3..0x103a263c` tests HasValidCoords at actor+0x464,
  supplies actor+0x434 as SystemCoords, and sets ECX to the particle location
  before the imported TransformPointBy call at `0x103a25fa`. The subsequent
  subtraction uses transformed location minus `(actor.Location + OriginOffset)`.
- Core.dll export `0x101016e0` jumps to `0x1010c5b0`. Instructions through
  `0x1010c629` subtract Coords.Origin from the input point, then dot that result
  with XAxis, YAxis and ZAxis. This matches the existing `Coords * vec3`
  operator; it is not multiplication by an inverted affine matrix.
- Core.dll SHA-256 is
  `3bc01427e8bf8745e31ec54bb0e72cc4d790c494fb8b06e6e1151300374834fe`.
  Engine.dll identity remains the one recorded in ParticleDispatch-recovery.json.
  These are supplemental direct instruction observations, not new REA Evidence IDs.

Reproduce the focused instruction checks against the matching local files with:

```sh
objdump -d --start-address=0x103a2300 --stop-address=0x103a2648 System/Engine.dll
objdump -d --start-address=0x101016e0 --stop-address=0x101016e5 System/Core.dll
objdump -d --start-address=0x1010c5b0 --stop-address=0x1010c62c System/Core.dll
```

Immediate InitEmitter applies SystemCoords to every particle in the initial
batch, then clears HasValidCoords. With SpawnDelay>0, the original initializer
clears HasValidCoords before marking particles invalid; the eventual delayed
spawn therefore uses no transform. The port now follows both paths. Its new
fixture uses nonzero origin, rotated/nonuniform axes, all three convergence
flags, a second immediate particle, a subsequent respawn and a delayed spawn.
The expected position `(364,-273,136.5)` and velocity `(-526,1425,666)` are
calculated independently in the test. The latest desktop integration passes this new fixture. The first ARM particle
probe predates this addition; final ARM verification is recorded separately.

Beams use the original connection/offset properties and three textured faces per
segment. Rope's actual SystemTick supplies sway; endpoint generation supports
TargetLocation, Target and skeletal target joints. With neither an explicit
target nor bUseTargetLocation, existing script-owned ConnectionPoint values are
preserved. Generated quads are written to ParticleArray.Points. Rendering supports
ordinary, vertical, flat and explicit-quad particles, using existing textures and
blend modes. Bounds include potential spawn regions and live particles/beams.

The original ActorTick recovery calls TickParticleSystem for ParticleSystem
subclasses after skeleton updates. The port ticks once from UActor::Tick, and
rendering does not issue a second callback. The old script's bSystemTicks comment
describes render-driven scheduling; exact original visibility/stasis scheduling
is not established. This implementation keeps lifecycle progress independent of
visibility and must be profiled in heavy FireSpewer maps.

## Limits and validation

Particle payloads remain original properties, so script callbacks and saves share
one representation. IsLoaded remains transient as in the original declaration;
native initialization after load can restart visual particles. No global actor
pointer cache is retained. Unknown modes log once per class/feature.

Native swipe and generic generation/update remain explicitly unsupported.
Relative-emitter transforms are diagnosed rather than claimed as exact parity.
The one-time SystemCoords spawn path is implemented as described above. Exact
beam transverse orientation/texture orientation and native
render ordering, PercentOffset, SpawnOverTime and TextureChangeTime behavior have
not been established. These limitations do not establish complete effects parity.

`RunRuneParticleProbe` is a synthetic fixture using original classes with a real
loaded map context. It checks deterministic emitter motion, alpha/scale/lifetime,
callback replacement including original Explosion.SystemInit and Rope.SystemTick,
pause, delayed spawn, Spewer count growth, actual one-shot/SystemLifeSpan actor
destruction and beam geometry/bounds. It does not validate Android rendering.
The desktop HelLift headless integration passes all of these assertions
(`campaign-particles-probe-desktop.log`). Android now visibly renders both Hel3a entry fires. Broader before/after rope/fire
comparisons remain outstanding; this is not a visual parity claim.

## Initial Android integration and performance correction

The first particle build passes its ARM headless assertions. In Hel3a, the two
entry braziers now visibly burn. That build was too slow: 67.637 displayed FPS
in the stationary entry sample (CPU tick 10.169 ms), and SailingShip measured
94.256 FPS (tick 6.370 ms). Hardware refresh remained 120 Hz. These findings
triggered optimization rather than a performance acceptance.

The next build resolves particle member offsets once per short-lived array view
and hoists common emitter parameters out of the per-particle loop. Cached offsets
never survive their current loaded object/view. The original reflected storage,
script callbacks and serialization remain in use. Optional RUNE_PARTICLE_PROFILE
reports tick, bounds and draw time over the same sample as RUNE_PROFILE.
Desktop tests pass after this change. Android results and further fixes follow below.

With Core.VSize2D662 bound, the original ClimbableVine0 in DeepUnder3 passes
falling/swimming attachment, grounded rejection, forward climbing, stopping and
jump detachment. Climb distances were 85.284 units at60Hz and86.135 at120Hz. This
fixture injects original PlayerMove fields, not touch input. HelLift offered no
clear320-unit interval for that fixture and is not claimed as a rope traversal pass.

The hidden-system guard is now restored from original ActorTick (REA
`ev_3e35b15b15a3bbb7a2bc76c0d83fcc7e35df8700d1c04cae81269d1daf82ee41`).
Hidden particles do not initialize or advance their native data, while ordinary
actor script/state/timers continue. A headless assertion checks unchanged time
and lifetime. Fixed property names are also interned once; simpleperf showed
name-string hash lookup accounting for13.36% of sampled CPU cycles before this
change. The latest native 4x-MSAA Hel3a sample reached 92.250 displayed FPS,
with particle tick 0.799 ms and total CPU tick 3.508 ms. SailingShip reached
105.830 FPS. Native resolution with antialiasing disabled reached 113.802 FPS
in a diagnostic sample; 4x MSAA remains the default. These results leave
rendering optimization outstanding.

Full original zone stasis remains unimplemented. Its recovered skip predicate is
`bStasis && (bForceStasis || Physics==PHYS_None || Physics==PHYS_Rotating) &&
NetMode==NM_Standalone && levelTime-zone.LastRenderTime>5.0`. The port lacks the
renderer-maintained zone timestamps. Actor traversal visits are not equivalent
to visible zones, and particle frustum culling must not replace this whole-actor
scheduling rule. Exact original timestamp write sites remain unresolved.

### Sequential repeat caveat

After the no-MSAA diagnostic, restoring native 4x MSAA measured 54.375 displayed
FPS in another stationary Hel3a sample. CPU tick increased to 5.923 ms and the
combined GPU scene/resolve/postprocessing interval to 13.958 ms. The earlier
92.250 FPS sample is therefore not a stable sustained result. These sequential
runs do not isolate device state, and the source of this variation is unresolved.
Do not attribute the full difference to antialiasing or advertise a fixed gain.
The latest ARM headless suite passes, including SystemCoords, Core.VSize2D and
hidden-particle assertions.
