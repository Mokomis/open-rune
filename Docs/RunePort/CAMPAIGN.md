# Open Rune — campaign and combat validation

October 24, 2026. This ledger records the current implementation and bounded runtime checks on the OPPO Android build. It does not establish complete campaign compatibility or original-engine parity. Dynamic-joint recovery and desktop/ARM numerical coverage are documented separately in [DYNAMIC-JOINTS.md](DYNAMIC-JOINTS.md).

The subsequent [touch-pickup correction](TOUCH-PICKUP.md) fixes an Android Ctrl/LControl mismatch missed by these direct engine-key weapon fixtures. Use-button pickup is only established by that follow-up real-screen-tap test.

## Current changes

### Close-camera visibility and capture

The renderer now honors Rune's original close-camera alpha for the player and carried equipment. Rune enables the fade by default; `--rune-camera-fade=0` provides a comparison. This implements the script's existing visibility response rather than moving the camera. Alpha uses a dedicated vertex-opacity path and translucent rendering when required.

Capture readback now matches `TextureColor`'s RGBA byte order. Readback and its caller use matching internal render dimensions; native and upscaled capture paths are both checked. The internal capture is not the final reconstructed GSR display image. These are capture fixes, not a change to the intended on-screen color rendering.

### Combat and Android controls

The Android controls expose Use and Throw. Rune's `Landed` callback now receives its required second argument, the hit actor, allowing the original thrown/dropped weapon scripts to settle correctly. The original inventory and weapon scripts handle pickup and attachment. GetJointRot and dynamic-joint forces are implemented from recovered native evidence; their numerical validation is covered in the linked ledger.

### Campaign entry and save restoration

The Android Campaign / saves menu exposes New campaign at Medium difficulty, quick save/load, and checkpoint load. New campaign starts `RagnarVillage.run`; quick save uses slot 9 and checkpoint load uses slot 10. Replacement actions ask for confirmation in the UI. Engine actions run between frames and clear held inputs and encounter references before changing sessions.

Rune save restoration reconnects the saved player to the viewport without rerunning `Possess`/`ClientRestart`, which would reset saved movement, animation and script state. Owner references rebuild the reverse child list without replaying ownership events. Sleeping actors use the serialized `LatentFloat` countdown. Save metadata records the real map URL, including when saving again after loading a `SaveN` package, and normal Rune map metadata is flushed immediately for Android process termination.

Validation uses an absolute, process-local `--save-directory` override. It isolates automatic checkpoints as well as explicit test slots; isolated saves store map metadata beside the save. It does not change the user's configured save directory.

## Confirmed runtime checks

Selected logs and measurement data are retained under [evidence/campaign/](evidence/campaign/). Full local captures remain in `../rune-classic/analysis/`. No original game packages or saves are redistributed.

| Area | Observed result | Evidence |
| --- | --- | --- |
| Close-camera comparison | Native DM-Hildir runs completed with fade enabled and disabled. The script reported camera distance about `58.5729` and alpha `0.509330`; side-by-side captures show the body becoming transparent. The disabled renderer still receives the same script alpha, as expected. | `camera-on-final.log`, `camera-off-final.log` |
| Upscaled capture regression | Enhanced presentation with `sgsr75` at scale `0.75` completed the repaired capture path. The camera runs reported Vulkan validation `active=1 synchronization=1` and no validation errors. | `camera-upscaled-fixed-validation.log` and the two camera logs above |
| Additional enemy | DarkViking approached/fought with its VikingAxe. Player health went `100 → 70 → 10 → -20` and entered Dying; enemy health reached `70`, then it returned home and waited. This confirms mutual combat and player death, not a DarkViking kill. | `darkviking-approach.log` |
| Head severing | GoblinWarrior joint 20/part 5 health `40 → -40`; spawned its concrete `GoblinBHead` class in Drop state. Asserted hidden severed geometry, visible cap and removed body-part collision. | `campaign-final/limb-head.log` |
| Left arm severing | Joint 9/part 1 health `35 → -45`; spawned `goblinlarm` in Drop state. The same geometry/cap/collision assertions passed. | `campaign-final/limb-left.log` |
| Right arm severing | Joint 15/part 3 health `35 → -45`; spawned `goblinrarm` in Drop state. The same geometry/cap/collision assertions passed. | `campaign-final/limb-right.log` |
| Throw and recover | Ordinary throw input detached the Handaxe; it traveled up to `563.344666` units, settled into Pickup, and Use recovered an equipped, attached inventory weapon. | `campaign-final/throw.log` |
| Drop and recover | The original DropWeapon script detached the weapon; it moved `82.031349` units, settled into Pickup, and Use recovered an equipped, attached weapon. | `campaign-final/drop.log` |
| Save and map transitions | Isolated slots 90/91 round-tripped SinkingShip2, DeepUnder1Odin and DeepUnder3. The fixture invoked the original exit Touch paths between maps and finished with `RUNE_SAVE_FLOW_OK`. | `campaign-final/save-flow.log` |
| Fresh-process resume | A subsequent process loaded slot 90 into DeepUnder3 and continued for 240 frames, ending with `RUNE_SAVE_RESUME_OK`. | `campaign-final/save-resume.log` |
| Android session actions | Quick save slot 9 succeeded in SinkingShip2; quick load executed there; New campaign loaded RagnarVillage with SinglePlayerTown. | `campaign-ui-actions.log` |
| Active combat endurance fixture | The bounded 2,400-frame run completed. Accumulated player/enemy damage reached `260/330`, confirming damage continued during the workload. The fixture replenishes health, body-part health and shield durability to sustain combat; it is not a survival or balance result. | `campaign-final/endurance.log` |

### Save assertions and travel policy

The save probe checks player health/max health, strength, rune power, inventory classes and stow slots; player position, physics, velocity, acceleration, state and animation; and animation proxies, ownership and joint-attachment links. It includes a synthetic falling state with velocity `(64,-32,120)`, then restores the stationary baseline after round-trip checks. Active sleeping actor countdowns survived round trips in SinkingShip2 and DeepUnder3. DeepUnder1Odin had no eligible sleeping actor, so that particular check was explicitly skipped there.

The initial SinkingShip2 fixture has player health `73`, a stowed Handaxe while swimming, and VikingShield durability `41`. Save/load preserves these values and the stowed weapon. The logged held weapon `None` is expected in that state. Original inter-map travel resets shield durability to the class default `70`, because that field is not a travel property; the probe accepts this original policy rather than claiming durability persists across travel. Health `73`, inventory and applicable travel state survive the two exit transitions.

## Limits and remaining coverage

The limb fixtures directly invoke the original `JointDamaged` script with sever damage. They validate downstream original script execution, limb classes, geometry, gore caps and collision removal. They do not demonstrate that natural melee sweeps consistently hit each intended limb or that animated collision masks exactly match the original engine. The fixtures resolve the concrete enemy's `SeveredLimbClass` and body-part mappings; base Goblin assumptions are incorrect for GoblinWarrior.

Weapon recovery places the player near the landed item before issuing Use. It establishes throw/drop/settle/pickup behavior, not autonomous navigation to the item, thrown-weapon enemy damage, or every terrain interaction.

The campaign transition fixture invokes original teleporter Touch behavior; it does not traverse the full levels or prove all triggers, puzzles, cinematics, AI or subsequent maps. The UI checkpoint-load option was exercised on the final build against the preserved DM-Hildir checkpoint and returned to PlayerWalking. New fight was then exercised successfully with a fresh player, shield and GoblinWarrior; see `campaign-ui-final.log`. The repeated combat workload is bounded and synthetic, not a long thermal soak. Dynamic spring/pendulum state is transient on load, so exact ongoing motion is not preserved.

## Performance

Native resolution only is the current test priority; no additional native/GSR heat comparison is required. These measurements use the OPPO's 2520×1680 surface, Original visuals, 4x MSAA, Swappy targeting 120 FPS and mailbox presentation. SurfaceFlinger unique actual-present timestamps are measured independently of the engine counter; the hardware reports min/max/current 120 Hz in all four samples. Each sample lasts about 36 seconds.

| Native scene | Displayed FPS | Median ms | p99 ms | Longest interval ms | Intervals >50 ms |
| --- | ---: | ---: | ---: | ---: | ---: |

| Active combat fixture | 118.98 | 8.334 | 8.335 | 16.67 | 0 |
| Village opening cinematic | 119.68 | 8.334 | 8.335 | 16.67 | 0 |
| SinkingShip2 swimming | 119.35 | 8.335 | 8.335 | 16.67 | 0 |
| Save/load and two level exits | 113.24 | 8.335 | 16.669 | 266.72 | 4 |

Steady scenes are near the target but do occasionally take two refresh intervals. The transition sample deliberately includes synchronous save/load work and map changes: its 113.24 FPS average and four pauses up to 266.72 ms must not be described as uninterrupted 120 FPS. It combines several save round trips with two exits, so it does not isolate the cost of an individual exit. The village sample covers the opening cinematic, not free exploration. Combat replenishes health/durability to keep damage active. These bounded samples do not establish every scene in the full game.

Raw JSON, timestamps and engine state logs are included as `fps-*-native.*` in the evidence folder. A previous GSR combat sample exists locally, but further dual-mode comparisons were stopped at the user's request.

### Save request concurrency

The final native transition fixture also confirms that an accepted quick save survives a same-frame automatic checkpoint: both slot 9 and slot 10 are newly written. A second pending session action is rejected rather than overwriting the first. `RUNE_SAVE_REQUEST_RACE_OK` records both assertions. Fixture files and map metadata remain inside their isolated save directory.

## Final build and preservation

Final installed APK SHA-256: `6484889a4b5e3ecdbf9b39ef7e2a8ada882ca8f873719c814fa730e436ead506`. It matches the release APK byte-for-byte and omits the validation layer. Package, signing key and Rune-only 120 Hz refresh rule are retained. All `persist.opdrr.*` properties match the pre-test snapshot; the four original DroidDeck INI hashes also match.

All nine extended cases passed on this APK in `campaign-final/`. Original attack, blocking/shield break and reachability regressions also passed (`campaign-core-*.json/log`). The attack case ended with Ragnar at 60 health and the goblin at -20. The checkpoint UI load and New fight actions passed on this APK.

An early save fixture wrote map labels into the prototype's normal INI before isolation was extended to metadata. The original save package bytes were never overwritten. The affected labels were corrected using the preserved saves' package GUID and serialized LevelInfo title: normal slots 0 and 10 are DM-Hildir. Test-only labels 90/91 were removed after confirming no corresponding normal save files existed. All other INI content was preserved. Four normal save/current-backup hashes still match the pre-test archive, including after UI checkpoint loading. See [map-identification evidence](evidence/campaign-preserved-save-maps.md), `campaign-metadata-repair.json` and `campaign-save-preservation.json`.
