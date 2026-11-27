# Ceiling recovery and committed-hit stagger — November 9, 2026

Current source is guarded-merged into `C:/OpenRune/work/OpenRune-main`. The work was developed in the isolated `OpenRune-ceiling-stagger-20261109` worktree; all ten touched main-checkout files still matched the initial snapshot before merge, and the controller saved backups. No commit or push. Normal game packages and saves remain intact. The live Android seven-bot host was only read, not controlled or stopped during development.

## Ceiling / ledge recovery

The old authority rejected Jump outside Walking/PlayerWalking, including RunePlayer's EdgeHanging state. Bots also lacked a recovery decision for their own climbing pose. Ordinary automatic climbs can recover without input, so that admission gap alone is not proof of the cause of every reported ceiling hang.

The new brain gives automatic climbing one second to progress. It observes its own replicated pull-up/hanging animation, clears incompatible queued specials when the climb stops progressing, requests ordinary Jump, and eventually uses backward/Duck input. It does not read hidden server state or teleport.

The authority now admits Jump in EdgeHanging without arming an airborne giant-leap combo. It calls the original Jump handler. If both the body and torso climb animation have stopped (body frame nonnegative, zero playback rates) and the pawn remains EdgeHanging, it releases the pawn at its current position into ordinary swept falling. Active animation/tweening and normal player-led climbing remain script-owned.

Verified regression: injected a stopped pull-up into the real authority/snapshot/replica/brain path, checked delayed recovery intent, accepted Jump, unchanged position at release, gravity-driven descent, world-collision landing, and rejection of a giant leap armed from the ledge. Passed Windows and Mac. This reproduces the frozen-animation failure class; it does not prove that every live ceiling sighting had that exact cause.

Diagnostic map scan: 192 supported giant-leap launches on DM-Hildir; three normal grabs completed automatically. Zero unsupported stationary final positions using a cylinder-width support sweep. The initial point-only floor diagnostic falsely reported 19 positions supported at their cylinder edges; those were not gameplay failures and received no patch. A separate 37-variant launch pass had no persistent hangs but did not produce ledge grabs, so it is not claimed as 37-model climb coverage. A bounded seven-bot simulation also had no persistent frozen state; its low combat activity makes it inadequate as a sustained fight qualification.

## Stronger hit reaction

In Cinematic and Chaos, a committed nonlethal health loss of at least 18 can produce an explicit directional whole-body pain stagger. It retains the original front/back pain clip when available, plays it over 0.32 seconds plus a 0.06-second blend, and returns control in roughly 0.4 seconds. Bash and this stagger share a two-second immunity interval. It does not add damage, change attack wind-ups, or change the existing two-server-tick/33 ms block window.

Light hits, shield-only contacts, dead targets, bosses/heavy resistant actors, scripted scenes, genuinely airborne targets, carried bodies/props, ongoing special moves and full ragdoll ownership are excluded. Original physics mode retains its prior behavior. Original damage may briefly mark a standing pawn Falling; a short floor sweep recognizes that case without freezing real airborne motion. Recovery explicitly restores the live combat state so the original Pain state cannot leave the pawn in None.

Both normal players and humanoid enemies/bots use the committed-damage hook. Existing snapshot body sequence/rate fields carry the stagger; no new wire field or save layout. The already implemented cosmetic muscles, IK and full knockdown remain in place. This is not a new full-body active-balance or physical locomotion system.

## Evidence

- Windows heavy-hit runtime/37 character-skin variants: `stagger-7/game.log` under the Windows artifact root.
- Windows real authority/replica stagger: `stagger-network/result.json`.
- Windows frozen-climb regression: `ledge-recovery/result.json`; last rerun also saved locally as `windows-ledge-final.log`.
- Existing native combat suite: `native-combat-final/result.json`, 495 snapshots / 8,159 ticks, damage/block/dual/rush/death/corpse checks passed; measured contact wait 34 ms with the fixture's integer clock. The policy remains two 60 Hz ticks.
- Mac: `mac/results.json` (ledge, replicated stagger, all 37 stagger/recovery variants passed).
- Mac visible world run: `visual-stagger-qualified/result.json` and frame-90/110/130 images. Actual world drawable 2048x1488, 100% rendering, verified 4x AA; startup target was 2816x1762 before the window changed size. Do not describe this capture as a full-display native-resolution performance benchmark. DarkViking visibly recoils with bent legs/torso and no stretched bones.
- First Mac `--probe-vulkan` attempt failed in the existing R32_UINT hit-buffer probe/resolve path on MoltenVK. The world-only fixture completed after excluding that unrelated validation probe. Its existing blend warnings remain recorded, not silently labeled as a new physics failure or fixed here.
- Android ARM native build and signed APK are ready; signing certificate and bundled license hashes verified. The user approved installation; see the device completion below.

Artifacts: `/Users/rwillmore/OpenRuneArtifacts/CeilingStagger-20261109` and `C:/OpenRune/outputs/ceiling-stagger-20261109`. Prior qualified CharacterPhysics artifacts are preserved.


## Device completion

Installed in place on OPD2515 after explicit restart approval. APK SHA-256: `3f34a7ce6b2a8f29ce1b69992726cd051bba0229b3e2a05c6f92ae6fcb05fb9a`; installed bytes match, original signing certificate retained. All 15 pre-install protected file hashes were unchanged immediately after installation. After tests and restoring the host, only the expected last-run log and multiplayer host-session preference changed; campaign saves, game INIs and gameplay settings remained unchanged.

The ARM command-line engine passed frozen-climb recovery, replicated stagger/control restoration and all 37 stagger/recovery variants. Android restricts execution of a helper in shell temporary storage; it ran normally from the debug app's own code cache. Both task helper copies were removed after testing; no root/security policy was changed.

The actual installed APK completed a 1,200-frame real player/DarkViking duel at 2520x1680, scale 1.0, 4x MSAA, Chaos, with four committed stagger events (both enemy and player). Captures show the enemy recoiling; the encounter log shows it returning from Uninterrupted to Fighting. The short 800-frame profile averaged 97.69 FPS, including diagnostic polling/captures; this is not a seven-bot sustained combat benchmark. Evidence: `android/rendered-combat/result.json`, `stagger-*.png`, `android/native-results.json`.

Restored the actual local-host UI session on DM-Hildir, eight participants (user plus seven bots), Chaos confirmed in both host/client logs and all Laws of Battle (4095) confirmed in the invitation. Kept the previous match limits (20 frags / 10 minutes) and ordinary render preferences (Hardware 50%, 4x MSAA). All eight participants joined with varied bot models. Controls handed back; no claim that the exact original live ceiling sighting has been conclusively reproduced.
