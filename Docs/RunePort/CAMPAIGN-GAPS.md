# Open Rune — Campaign compatibility audit

October 24, 2026. Static findings from the user's recovered shipping scripts and the local campaign actor catalog. These establish required paths and implementation gaps; they do not establish full-level runtime reachability or original runtime parity. This pass initially inspected 46 map inventories and 326 distinct placed classes; BeetleFly was absent from that initial catalog. Follow-up implementation and runtime outcomes must be recorded separately.

## Prioritized findings

1. **Polyobj movement, rendering and collision are a progression gap.** Original `Engine.Polyobj` extends `Brush`, not `Mover`, and `RunePolyobj` extends it. Its script `InterpolateTo` sets `PHYS_MovingBrush`, `bInterpolating`, 16-key position/rotation data and `PhysRate`; state code then calls `FinishInterpolation`. At audit time, native `TickMovingBrush` only integrated `UMover`, `VisibleActor` only rendered mover brushes, and brush trace/overlap/broad-phase paths likewise only recognized `UMover`. A loaded Polyobj can therefore retain script interpolation state without native motion. Polyobj/RunePolyobj are placed across the opening, underwater, Hel, Thor, dwarf and Loki maps. This requires brush geometry, motion and collision support together, not a missing-function stub. The parallel Polyobj work owns this correction.

2. **Script synchronization required natives 660/661.** `ScriptPawn.Scripting` and `RunePlayer.Scripting` call `WaitForRelease` when their ScriptAction/ScriptDispatcher has `bWaitToBeTriggered`. Original ScriptAction/ScriptDispatcher Trigger calls the waiting pawn's `Release`. Goblin mining states also use WaitForRelease/ReleaseTagged. Placed ScriptActions are widespread; per-instance wait flags are needed to identify the first actual gate. Implemented from original wrapper/poller recovery in this pass; desktop and ARM headless fixtures pass (see runtime section below). Original WaitForRelease is a 9,999-second Sleep using latent index 384. Release sets that actor's countdown to -1 only if it uses that shared sleep poller, so it also intentionally wakes ordinary Sleep. It does not release unrelated latent actions or queue a future signal.

3. **Surfacing required native 535.** `RunePlayer.PlayerSwimming` calls `FindWaterLine(Location+(0,0,40), Location+(0,0,-40))` when its head enters a non-water zone, then traces/positions the player at the result. This is a concrete underwater-to-surface path not covered by prior stationary underwater or exit-Touch fixtures. Implemented from original helper recovery in this pass; desktop and ARM headless fixtures pass (see runtime section below). The original bisects against the body's current zone water flag until the endpoints are less than one unit apart. It does not test endpoint crossing first, so its no-crossing behavior is part of the compatibility fixture.

4. **Cinematic skip required native 268.** `RunePlayer.PlayerCinematic.Use` calls `StopAllSound` before `ReleaseFromCinematic` when the camera is interruptible. A completed passive cinematic does not cover this input path. Implemented as a Rune-only binding. Recovered ALAudio StopAllSound clears effect channels but retains music and its viewport; the new StopAllEffects API preserves that distinction. Backend-spy fixtures pass on desktop and ARM. This does not establish audible device behavior during a natural cinematic skip.

5. **Breakable polyobjects need natives 668/669.** `RunePolyobj.Explode` calls `GetCollisionRadius` when `DebrisSpawnRadius == 0`; base Polyobj also calls it when spawning configured debris. Original native recovery returns half the diagonal of the selected primitive's render bounding box. Inherited `Engine.Polyobj.Explode` calls `GetTexture` when no DebrisTexture is configured: the initial RuneI-only scan missed that inherited caller. The original selects the most frequent visible surface texture, resolving animated material references. Both are implemented and pass desktop/ARM fixtures. Radius uses the transformed cached brush AABB (the original precise=0 render-bounds path). Texture ties preserve the first visible surface; null materials count too. The concrete campaign brush path is covered; original missing-brush mesh/skeleton/default-primitive fallbacks remain unsupported and raise an explicit error instead of inventing a radius.

6. **Particle/beam native behavior is missing.** The recovered `Engine.ParticleSystem` is intrinsic and owns particle arrays, emitter/beam/swipe modes, lifecycle and `SystemInit`, `SystemTick`, `ParticleTick` callbacks. At audit time the port had no corresponding native particle-system dispatch or Rune particle rendering path. `RuneI.BeamSystem`, Rope, lightning, weapon trails and many effects inherit this behavior. Rope is placed in nine catalog maps; missing beam rendering can hide a climbable rope even if script interaction works. DarkDwarf lightning/charge effects and MechRocket/explosion effects require explicit coverage. This is broader than a missing VM binding; visible effects and any gameplay depending on lifecycle callbacks must be traced separately. Do not assume all projectile damage is absent: several projectiles apply damage from ordinary script events.

7. **Fleeing required native 664, beyond Goblin path-following.** Inherited `Pawn.FindPathAwayFrom` calls `NearestNavPoint`; `ScriptPawn.Fleeing.PickDestination` calls that helper, including after a living pawn loses its right arm. Fear/acquisition logic can also enter Fleeing. `Goblin.StayOnPaths` is an additional caller. Recovered native664 scans the navigation list using squared 3D distance, with an initial bound of exactly 9,999,999. It performs no reachability or eligibility test; strict comparisons retain the first entry on a tie. Implemented with a fixture that executes the original flee helper and checks its outgoing route/excluded-previous-point behavior; execution pending parent build. `CloserNavPointTo` (665) remains a declaration with no callers found in the bounded recovered Engine/RuneI scan.

8. **World-positioned HUD text required native 492.** RuneHUD calls `Canvas.TransformPoint` for messages whose world-position Z is nonzero and for multiplayer typing indicators; RunePlayer also uses it for debug selection. The regenerated catalog contains 106 SpecialEventRune actors in RuneMessage state. Exactly one selects a world position: `Hel3b_CUT.SpecialEventRune29`, tag `Hel3BattleBonus`, MsgLocation=(0,0,0), actor Z=-7.89941. Original RuneMessage.Trigger substitutes actor Location for zero MsgLocation, establishing the required call path. The remaining 105 placements are screen-relative. Implemented from native492: transform through the scene frame, project only when view depth >1, return (-1000,-1000) behind/at the near threshold, leave outputs unchanged without a frame, truncate coordinates toward zero without screen clipping. The port adapts physical frame projection to logical canvas pixels for its existing UI scaling. Probe execution pending parent build; a natural triggered world-message visual comparison is not yet covered.

## Enemy coverage priorities

The catalog places DwarfMech in DwarfMap2, DwarfMap3A_CUT and DwarfMap6DarkDwarf. Its script spawns MechRocket, mechblade, Explosion and blacksmoke; recovered joint rotation/dynamics alone do not validate these attack and effect paths. DwarfBlack in DwarfMap6DarkDwarf spawns darkdwarfbolt, DarkDwarfChargeup, DarkDwarfBlast and DarkDwarfExplosion. SnowBeast variants occur in Mountain1_CUT, Mountain2, ThorMap4A and Loki1, with separate jaw/paw actors and breath effects. StoneGuard occurs in Loki1A, Loki3a and loki-maze. Conrack occurs in SailingShip and Thor1; Loki in Loki2, Loki3a and VillageRuin. These are concrete next encounter targets, not newly proven missing-native failures. Existing generic projectile/AI paths may already handle some behavior.

## Bounded negatives and evidence

The native declaration scan compared current registration names with recovered Engine/RuneI declarations and then searched RuneI call sites. It is not a complete bytecode call graph: dynamic class spawning, inherited Engine callers, map instance properties and version-specific scripts can add requirements. Native accelerator declarations 651/652/654/655 had no directly placed class hits in the initial catalog. Rope inherits BeamSystem, not RopePoint; it is not evidence that RopePointTick is required. `PHYS_Sliding` is declared by Rune but absent from the port's dispatch; no recovered Engine/RuneI script call site selected it in this scan, so it remains unprioritized pending map/default-property evidence.

Local script provenance is retained in `../rune-classic/analysis/recovered-script-manifest.json` (package/export/offset/text hash); inventories are in `analysis/campaign-catalog/catalog.json`. Relevant recovered text SHA-256 values:

| Recovered class | SHA-256 |
| --- | --- |
| Engine.Polyobj | `9133e1426b6fd7b0cb7d4b3beea1ec9835b212faa82e36d3c108cda2ef5e24c9` |
| Engine.ParticleSystem | `879d29157eb56a7a39ef3ad706e913e6b57129d73a738a296a661425842c0623` |
| Engine.ScriptAction | `66056bdbdade3de7ea00def2548ec329db9a7979a9abe88dc6a416366c3b04a7` |
| RuneI.ScriptPawn | `eea3278b371359eb38fcf8c1ec3cf09ac3973f26b80fc49a1a99cb9ac2e1cd21` |
| RuneI.RunePlayer | `505eb7b31a48c7cc42fd83207a62ab4d7af1e0ca811f3deccda5b2a43ed7b8c6` |
| RuneI.Goblin | `f5f2c720b3cd8f9822dad44ff01104f9b8b377318b87e3754c04295b2c9ff982` |
| RuneI.DwarfMech | `f65a4ceb66619b9f8fd6d20d3e370ae023ae4c5dae73ccab5890fbcd382bc0a8` |

## Focused original native evidence

REA 6.3.0/Ghidra recovery of the same Engine.dll identified in DYNAMIC-JOINTS.md. Full Evidence JSON and pseudocode are retained as `evidence/Engine.<name>.json/.cpp`. These are static results, not original-runtime tests.

| Function | Address | Evidence ID |
| --- | --- | --- |
| execWaitForRelease | 0x10304525 | ev_a02ca316d104e64904cac41cb5ae19de456144a357c01841d53d78ec4bc4a529 |
| execRelease | 0x10302f2c | ev_5607cc18a1d559819b23346521218156ba476423b1f88b013fdfe0584bdd4fa2 |
| execSleep | 0x10303a21 | ev_a392860104b9edf78840c4efbcf6ad3730ac54f03911bc694ca5f2dd6ce1ec53 |
| execPollSleep | 0x10305957 | ev_9956afbe89e77836c35556ab6f06c992d9d078985fe320e4ff2c0deb33fdcce6 |
| execFindWaterLine | 0x103055f1 | ev_779e42eaf95f16b2ddf536086c40dbf201cd02e36d4ad22215b7961baa5305bc |
| findWaterLine | 0x103056af | ev_238da24145688e596c6757beec767ebf65c10e90cf16554ddef5f3f523550159 |
| execGetCollisionRadius | 0x10301da7 | ev_e1265a284e49a752d8183707d3913c908c004afc5c7318b99a63f5c4303f87fa |
| execGetTexture | 0x1030122b | ev_64d84b0bf4e76e48b4ef504e48c61a450678e02ee46da6622e1e633149532641 |
| ModelGetRenderBoundingBox | 0x103042af | ev_d98498e1b68f8cdeae509864534c5914d0bfcd45748188ae7f9e7434e1cb9a0b |
| ModelGetCollisionBoundingBox | 0x10304da4 | ev_6aabb79aaec47fd2fb538b2f4240165bda8afce4b9952d15d351a7d68fe1d004 |
| execStopAllSound | 0x10304e58 | ev_f2bc193302f96028ce5a806c09cf79f3b74036c2a5b54d6d55d392007655dd89 |
| execNearestNavPoint | 0x10304c5f | ev_0153f7039f4b39b38c789193b7d89fc6fb329054a4c7c11c551e494d5c7f59fa |
| NearestNavPointBody | 0x103b1930 | ev_f12ccbe66edf85ef69c31aa77a8c06c623e1ebb2003eb443498243f01f2cd5e1 |
| execTransformPoint | 0x10303e59 | ev_ed0e23316d8c9c424bddfb094012843cef223e1bcb3b6651c4cdf2a48567faff |
| TransformPointBody | 0x103647c0 | ev_5fd15f84f8c0e2fbeac817a0e847d8497aa5cc57f0e4a6c0724322a9fbaa985f |
| CanvasRoundScreen | 0x1040ac80 | ev_04450668fdc7646a51c63974545fe6d438eeffb0ae607cf9949f21e01ed9ad18 |
| ALAudio.StopAllSound | 0x10004470 | ev_0401960718d355efff358b9e2e7e4ec65ea231bf62e98a70f991f35349ef1e32 |

ALAudio.dll SHA-256: `cc3a9e5e83cccad5dc63484aa472526efbedc7017e1dc5ce571bf4ebfe766926`. The navigation cutoff constant and its original instruction are retained separately in `evidence/Engine.NearestNavPointConstant.json`.

## Runtime validation of this pass

`analysis/campaign-probe-desktop.log` and `analysis/campaign-probe-android.log` contain matching successful markers for the following tests (parent-executed builds; inspected local logs):

- Wait/release: real VM suspension/resumption, actor targeting, original ScriptAction.Trigger, ordinary Sleep release, four unrelated latent-state guards, timeout, save index 384 and legacy reader index 257.
- Waterline: wet Z=-0.625, dry Z=0, same-water Z=-10.625, horizontal crossing X=-0.625; three crossing cases, two no-crossing cases and zero length. These are synthetic BSP-zone fixtures, not full swimming traversal.
- Polyobj natives: transformed radius=23.8537; visible texture majority, first-surface tie, animated texture, null-majority and empty brush-node cases.
- StopAllSound: two active channels stopped and a queued channel cleared, with music/viewport preserved, repeat/no-device guards. A backend spy verifies routing without opening an audio device.

Navigation and canvas probes are newly added and pending the next parent build. Full campaign playthrough, natural enemy flee behavior, triggered bonus-text placement and audible cinematic skip remain unverified by these focused fixtures.

## Rope release follow-up: Core native 662

The Android rope fixture reached climbing, then original `RunePlayer.PlayerRopeClimbing.Jump -> LeapOff` failed on unregistered `Object.VSize2D`. Recovered `Core.Object` declares this static function at native index 662 (`Object.uc:237`). The existing port implemented horizontal vector magnitude for Unreal 227 but registered it only there at index 271. A Rune-only index-662 binding now reuses that implementation; other game mappings stay unchanged.

`RuneCoreVectorProbe.h` resolves the recovered Core.Object function, checks index 662 and calls it through `Frame::Call`. It asserts XY length=5 for (3,4,0) and (-3,-4,9999), zero for a vertical-only vector, fractional magnitude=0.5, and the original rope release comparison at exactly/above 50 horizontal units. Build/device results are pending parent execution. The numerical fixture does not establish successful rope release animation, detachment or restored player control.

Source provenance: recovered Core.Object text SHA-256 `5d56f411030931b5d2f2d68d03202d666aa63fa3972fe1d3de3958390b0e3d35`; RuneI.RunePlayer hash is listed above. Original Core.dll SHA-256 `3bc01427e8bf8745e31ec54bb0e72cc4d790c494fb8b06e6e1151300374834fe`. Original `FVector::Size2D` reads X/Y at offsets 0/4 and returns `sqrt(X*X + Y*Y)`, confirming the reused helper. Full static REA results are retained in `evidence/Core.execVSize2D.json/.cpp` (0x101033d7, `ev_b3fabe07cee25f76f28e41175f4d3c8dcb31f26ae9b0cb2bbb0a00db57dd2e3d`) and `evidence/Core.FVectorSize2D.json/.cpp` (0x10101bd6, `ev_e2ce5317b00258d58bfd382420fa3826b1d2e4dcd69ae087f04a5a584e960b90`). `Tools/RuneRecovery/core_vector_native.py` reproduces this focused recovery. The owned REA session closed cleanly.
