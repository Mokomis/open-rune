# Moving-brush encroachment (November 3)

Rune-only changes to Mover and Polyobj encroachment, from reviewed original
Engine.dll evidence (SHA256 `41458bac…c585d0`). Qwen reports were treated as
untrusted; facts come from the full retained pseudocode and independent
disassembly. Reviews are kept outside Git in
`outputs/qwen-collision-contracts-20261103/` and
`outputs/qwen-movement-contracts-20261102/`.

## What changed

- **Pair rule** (`UActor::IsBlockedBy`, original 0x10329b70). When a brush is
  involved, the other actor's `bCollideWorld` and the brush's block flag decide
  (`bBlockPlayers` for player pawns). Used for moving-brush encroachment and for
  ordinary actors whose trace hits a brush. Actor–actor pairs keep the previous rule.
- **Encroachment** (`RuneCheckEncroachment`, original CheckEncroachment 0x1038eca0).
  Checked at the test pose before the brush commits. Direct riders are carried
  first, as in MoveActor. A non-brush blocker is pushed (`moveSmooth`) by the
  translation plus rigid rotation offset. If it is still encroached, the push is
  undone with the brush at its test location. Only then does `EncroachingOn`
  decide: Stop/Return reject; Crush/Ignore continue. Blocked candidates then get
  `EncroachedBy`.
- **Rotation**: `TickMovingBrush` checks the combined translation and rotation,
  so rotation-only steps also push or are rejected.
- **moveSmooth** (`RuneMoveSmooth`, original 0x103b7b70 and TwoWallAdjust
  0x1030cef0). Slide, HitWall and corner adjustment. Script `MoveSmooth` now
  returns the first move's progress, as in the original.
- **Relocation** (`SetLocation`, original FarMoveActor 0x1038dcc0). A successful
  relocation clears direct riders' bases without moving them (joint attachments
  are not physics bases and are unaffected), sets `bJustTeleported` and moves
  `OldLocation` to the new spot. The flag is word 0x208 bit 0x2000; counting the
  recovered `Actor.uc` bool/byte declarations from the known 0x1f8 collision word
  places `bJustTeleported` exactly there. Without it, a pawn walking into a
  Teleporter got its velocity recomputed as teleport distance / tick (325,070
  units/s on DM-IceCavern).
- **Script relocation encroachment** (`RuneFarMove`, script `SetLocation` only):
  after the spot search, CheckEncroachment runs at the new spot, so the original
  `Pawn.EncroachingOn` refuses non-player pawns onto pawns and any pawn into a
  brush, and a player relocating onto a pawn telefrags it through
  `EncroachedBy`. Native port callers (network placement, fixtures) keep the
  plain spot search.

`--trace-encroachment` logs each rejection (`RUNE_ENCROACH_REJECT`).

## Validation

`--rune-probe --probe-map=MAP --probe-mover-encroach --save-directory=…` covers
both brush classes:

- free and pinned pushes in all four event modes
- the pair rule both ways
- rotation push and stop
- carried riders
- relocation with and without riders
- `MoveSmooth` return
- actors moving into brushes

Results:

| Run | HelLift | LostCrypt |
| --- | --- | --- |
| Unchanged source | 9 of 20 fail | 13 of 24 fail |
| Final, Mac | 22/22 pass | 26/26 pass |
| Final, Windows (MSVC) | 22/22 pass | 26/26 pass |

HelLift has no usable rotation arrangement, so its rotation cases are skipped;
LostCrypt covers them.

Existing Polyobj, lighting and completion probes still pass. These rendered or
deterministic fixtures match the unchanged build:

- Mac lifts on both maps
- mob kick and bash
- bloodlust leap, charge and block
- dual wield

Rendered fixtures need `--close-menu` with a fresh config.

Real pawn riders: `--probe-lift=NAME --probe-lift-rider` stands the player on
the lift's top face, requires it to land based on the lift, then to ride the
whole leg. LostCrypt `Mover10` carries the player 496/496 units; HelLift's
elevator floor `Mover4` carries it 272 units within the window (still moving).
Neither logs an encroachment rejection. HelLift's auto choice, `RunePolyobj3`
(tag `elevr`), is a separate brush that normally moves with `Mover4`, so it is
not a rider test.

`--rune-tactics-probe=relocation` (DM-IceCavern) checks a real Teleporter touch
mid-walk (284 units/s afterwards, previously 325,070), the teleport flag and
`OldLocation`, a non-player pawn refused onto the player, the player
telefragging that pawn, and a pawn refused into a mover. Unchanged code failed 4
of 5; the fix passes 5/5 on Mac and Windows MSVC. The rider check needs fixed
timing (`--probe-tick-hz=120 --maxfps=120`); uncapped, LostCrypt's lift can
finish and start back down before the end check.

A networked DM-HelEvator match (server, one rendered client, three bots, two
timed rounds; its lift cycles every 12 seconds) completes with every process
exiting cleanly and no rejections in the server's `--trace-encroachment` log.
A DM-IceCavern match with teleporters (three timed rounds) is also clean.

With `-DOPENRUNE_ENABLE_NETWORK=ON` (off by default; without it the network
probe crashes at window creation), these pass on the fixed build:

- the headless native combat probe (430 snapshots, 4,093 ticks, as in the
  November 1 evidence)
- `network_native_combat.py` dual, bash and blood (leap/charge)

These fail identically before and after the change, so they are not regressions:

- blocked mob kick and bash time out
- the default tactics fixture fails to spawn
- AI bash is timing-flaky on both builds

## Limits

- The original undo of a rotation-only push runs with the brush at its old
  rotation, so the blocker may not return exactly to where it started.
- `GetPlayerPawn` also requires a Player object; the port still uses a
  PlayerPawn cast.
- Brush-vs-brush encroachment is unchanged. All 63 HelLift and 13 LostCrypt
  authored brushes have `bCollideWorld=0`, so the original rule never blocks them.
- Relocation still uses the port's grid spot search rather than the original
  FindSpot, and skips the original's netmode condition for bCollideWhenPlacing.
  Native port callers do not run relocation encroachment.
- Script `SetRotation` on brushes does not encroach. The original routes it
  through MoveActor; this is unverified for Rune.
- No live multiplayer door or lift session, Android run or campaign playthrough
  was performed.
