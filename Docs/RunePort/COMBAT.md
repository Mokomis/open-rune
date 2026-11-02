# Open Rune — First enemy combat encounter

> Historical v0.5.0 milestone. Current camera, wider combat, dynamic joints and campaign save/transition results are in [CAMPAIGN.md](CAMPAIGN.md) and [DYNAMIC-JOINTS.md](DYNAMIC-JOINTS.md).

October 24, 2026. Native ARM64 Android prototype on OPPO Pad Mini OPD2515.

## Play it

Launch Open Rune and tap **New fight** at the top left. It reloads DM-Hildir, gives Ragnar a hand axe through normal game startup and a Viking shield through the original inventory routines, then spawns one GoblinWarrior. Use Attack and hold Block. The shield has finite durability. Tap New fight again to retry, including after death. The current arena is discarded; this does not save a campaign. The normal app launch still allows exploring the empty arena before starting a fight.

The original AI, weapon scripts, damage reactions, inventory and death states run from the user's original packages. The fixture does not freeze the enemy, change its health/collision or script its attacks. It places the enemy 140 units in front of Ragnar; this is a combat test arena, not campaign placement.

## Fixes and evidence

1. **AI stuck at contact.** Walking reachability required reaching the center of a blocking goal. Goblins chased Ragnar into melee range but remained Hunting because reaching that center is impossible. Goal contact now counts as reached, retaining the vertical check, obstacle checks and restoration of the pawn's real position. Live checks on desktop and Android accept pawn contact and reject an elevated goal and an intervening obstacle.
2. **Goblin attack leap missing native 663.** `Actor.CalcArcVelocity` now computes the fixed-pitch ballistic launch using zone gravity magnitude. The behavior was recovered from the original Engine.dll and independently checked by integrating three different flight paths to their destinations; degenerate/impossible cases return zero. The goblin Fighting script now runs without this missing-native exception.
3. **Death interrupted by platform statistics.** `RunePlayer.ReceiveDeathMessage` calls `ModifyIntStat(38,1)`. The missing callback stopped death before the Dying transition. Six stat/achievement callbacks now maintain real offline counters and achievement flags for this Engine session. They are explicitly **not persistent and do not sync to Steam**. Integer accumulation saturates and invalid enum values return zero/false. VM tests cover round-trips and invalid queries.
4. **Shields excluded from weapon sweeps.** The sweep iterator was discarding all carried items, even though the original weapon script handles shield interception and owner suppression. Removing that blanket exclusion allows shield joint-box hits and durability damage. The lowered-shield control still takes body damage.
5. **Repeatable play on device.** New fight queues a request for the engine thread. Between frames it drops encounter references and held inputs, reloads the arena and logs in a fresh player. Two in-process resets, including after death, were exercised. The Activity keeps its own visible game window awake; no global timeout changes.

Native recovery used a temporary REA 6.3.0 stdio server and existing Ghidra 12.1.4/Java 21. It did not change tool integration settings. Source binary SHA-256: `41458bac7e717a57effbe45d9351664ab79d72d7b821c3103018d057b7c585d0`.

- `execCalcArcVelocity`, address `0x103015b9`: `ev_7441b88e98c212188fc85e991f1c30676a807d128143b8c4fcc8c722bc26dad0`.
- `execApplyJointForce`, address `0x10301d93`: `ev_93c8b370a764b8a9d2f14a8ecb7eff027235575ee03b0251bfaef0056530195a`. Recovered for follow-up, **not implemented or claimed tested**. It feeds dynamic joint force state, so an ignored-force placeholder would be incorrect.

Pseudocode/JSON are in `evidence/Engine.exec*.cpp/json`. These are static recovered observations, not original execution traces or original compilable C++ source. `Tools/RuneRecovery/combat_native.py` reproduces the two recoveries.

## Final validation

All six Android cases used the installed final APK, seed 1 and DM-Hildir. Full logs and parsed assertions are under `evidence/combat-final-*`.

| Case | Observed outcome |
| --- | --- |
| Attack, 1200 frames | Goblin health 70→40→10→-20, pain/death animation and Dying/deletion; Ragnar survives at 60 health |
| Raised shield, 1800 frames | Ragnar stays at 100 while shield durability falls 70→61→52→43→34→25→16→7; shield breaks, subsequent attacks hurt and eventually kill Ragnar |
| Lowered shield, 1200 frames | Ragnar takes repeated damage and enters Dying at 0 health; enemy returns home |
| Reachability, 360 frames | Pawn contact reachable; elevated goal and intervening obstacle unreachable; pawn position preserved |
| Stool, no attack, 300 frames | Target survives |
| Stool, attack, 300 frames | Target destroyed |

No unknown-native or script exceptions occurred in these final bounded tests. Repeated attack timing is frame based and depends on real elapsed simulation time: this is one successful fight pattern, not a promise that tapping at any cadence always wins. The helper `Tools/RuneValidation/android_combat.py` enforces outcomes and retains failures. Run one device test at a time.

Touch/restart evidence: `combat-new-fight.log` and `combat-touch-block.log`. The second reset restored Ragnar to 100 health and a fresh 70-health shield after the first fight's death. Holding the actual Block button produced Defending and durability damage while health remained 100. Releasing it returned the proxy to Idle. Screenshots were visually inspected; close camera placement can obscure the opponent.

Desktop and native Android headless probes both pass: ballistic integration, offline stats VM calls, model/joint geometry, animation proxy, layered pose, attachment transforms, native bindings and SailingShip's 1,904 actors. See `combat-headless-final.log` and `combat-headless-android-final.log`. Desktop and both Android targets build successfully. `combat-before-fix-*` files intentionally preserve earlier native failures and are not final test results.

This turn did not repeat broad frame-pacing/thermal benchmarks or graphics-layer validation. Renderer/pacing code was not changed. The overlay's short FPS readings are not independent presentation measurements. Existing results and limitations remain in UPSCALING.md.

## Installation and preservation

Package `org.runeclassic.port`, UID 10300, updated with the existing signing key. Installed APK bytes match the release APK; no validation layer is packaged. APK SHA-256: `eddabd35e52eda6ac9ca997f941028f0abce9910a7e294ed8109e01ba586ae9d`.

All `persist.opdrr.*` properties match the pre-test snapshot, including Rune's existing 120 Hz exception `persist.opdrr.42800e61=3`. Hashes of the four checked original DroidDeck INIs match. No original assets/saves or unrelated device policies were intentionally written. This is a four-file hash check, not a newly generated whole-installation checksum. See `combat-install-verification.json` and matching before/after files.

## Remaining work

- Wider enemies, limb severing, dismemberment, thrown/dropped weapons and exact collision parity remain unvalidated. DarkViking attacks were diagnostic on desktop; it is not part of this release's final Android acceptance suite.
- ApplyJointForce/GetJointRot and dynamic skeleton behavior are still incomplete. Missing natives must continue to report their name/stack; do not add silent stubs.
- Close-up camera presentation can crowd Ragnar over the view in tight locations. Rune's script requests client-side alpha fading; the renderer does not yet implement that property. Follow up on camera/visibility and compare the original before claiming parity.
- Fractional animation-proxy interpolation/tween continuity remains incomplete. Combat FPS does not prove animation fidelity.
- Stats/achievements reset with the Engine session. Persistence and Steam integration are not implemented.
- Campaign save/load, campaign transitions, multi-touch/controller coverage and broader combat performance remain separate milestones. Two arena reloads do not establish campaign transition support or long-session memory behavior.
