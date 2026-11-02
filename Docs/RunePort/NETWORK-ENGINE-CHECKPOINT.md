# Open Rune networking engine checkpoint

Latest October 26 checkpoint: [interactive private multiplayer](NETWORK-INTERACTIVE.md)
adds rendered clients, Android join/leave/respawn menus and protocol 9. Earlier
non-playable/protocol-version statements below describe historical milestones.
See that checkpoint for exact runtime validation and remaining limits.

October 26, 2026. Multiplayer development is **paused at the user's request**.
This checkpoint preserves unfinished work for later continuation. No feature
implementation or new gameplay test was performed for the GitHub upload.
Later on October 26, the user resumed the Open Rune protocol/server direction
to support the fork's custom combat moves. This page remains the historical
checkpoint; [the multiplayer plan](MULTIPLAYER-PLAN.md) records the active scope.

## Included boundary

The standalone networking library now includes fixed 60 Hz simulation helpers,
bounded entity identity, checked quantization and presentation interpolation.
The root build option `OPENRUNE_ENABLE_NETWORK` defaults to **OFF**. Enabling it
links the core and experimental `RuneNetwork` adapter, plus the desktop
`--rune-network-probe` fixture. The adapter provides owned-pawn input admission,
script action dispatch, world capture and explicitly bound presentation replicas.
Normal builds retain their standalone path.

The fixture loads DM-Hildir using RuneMultiPlayer, logs in two distinct server
pawns through the original script login path, and exercises two authenticated
client sessions in-process. It checks movement, one jump action, snapshot
installation, invalid ownership/life/animation cases and disconnect cleanup.
It uses isolated save storage and requires separately owned original game data;
that data is not included in this repository.

## Existing results preserved

These runs completed before development was paused; the GitHub checkpoint did
not rerun them:

- The opt-in desktop engine build linked successfully (RelWithDebInfo).
- The desktop fixture completed 480 simulation ticks, 360 world installations
  and one dispatched action. These are fixture counters, not match benchmarks.
- The standalone Debug ASan/UBSan suite passed 10/10 groups in 19.37 seconds,
  including the new simulation group.

The exact captured outputs are in
[evidence/network-engine-boundary](evidence/network-engine-boundary/).
Earlier release, fuzzing, mutation and Android cross-build results are recorded
in the earlier networking milestone documents; they do not validate every later
engine change.

## Not yet delivered

This is not playable multiplayer. The fixture does not run two independent game
processes, an internet server, or an Android multiplayer session. No new APK was
installed for the engine adapter. Joining/session UI, public socket integration,
asset/archetype negotiation, complete authoritative combat outcomes, prediction
and reconciliation, latency fairness and sustained multi-player performance
remain unfinished. The proposed 32-player capacity has not been demonstrated.
The user has since requested resumption; these gaps still apply.
