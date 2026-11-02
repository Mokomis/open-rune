# Private multiplayer protocol 17 changes

This implementation uses protocol/ALPN 17, native rules 17, invitation
format 4 and raw snapshot format 6. It is incompatible with the untouched Android
installation's earlier experimental protocol. No original Rune protocol support.

## Gameplay and lifecycle

Match contracts include team count, Arena team size and HeadBall limb scoring.
Score rows carry a stable invitation slot and the current pawn reference separately;
a reconnect can get a new entity identity without changing the player's slot or
score. Four team colors are represented, including maps whose teams are Blue/Gold.

Reliable actor records add bounded skeletal appearance: mesh index, 16 group
flags, 50 joint flags, and two halves of eight package/export texture references.
Spawn copies the content-hashed class defaults, so initial appearance records
are omitted when they exactly match those defaults. Changes and restoration to
defaults still send reliable records. Clients validate the package/export/type
and apply presentation data without VM callbacks. Hidden stowed inventory is captured; clients reconstruct its owned
inventory links and stow slots. Weapon selection intents 13–16 request the
original authoritative SwitchWeapon handler. Combat outcome move 3 reports native
weapon contacts, including blocked contacts; clients show feedback only for matching
entity/life generations.

Each player has a private DTLS key and invitation slot. Leaving retires only that
slot; stale traffic cannot allocate a new session unless it is a DTLS ClientHello.
Reconnection authenticates afresh and receives current actors, appearance and map.
Map travel uses a new replication epoch on the authenticated connection. An empty
lobby persists; a failed map-loading participant cannot terminate the host.

## Snapshot packing and recovery

The raw snapshot bytes may be packed before 640-byte fragmentation. The optional
envelope is ASCII `ORZ1`, then a big-endian uint32 exact uncompressed byte count,
then tokens. Token low seven bits plus one give a length of 1–128. A high bit of
one emits that many zero bytes; a high bit of zero copies that many following
literal bytes. No recursion or dictionary. The encoder uses zero runs of at least
three bytes and sends raw bytes unless packing saves more than 32 bytes and still
meets the 31-byte minimum fragment payload size.

Input and expanded size are bounded by 40,960 bytes. Expansion checks remaining
output capacity before each token, literal input availability and exact final
length. Raw format-6 decoding and all world/reference validation still follow.
The feature changes bytes and bandwidth, not authoritative precision or physics.

Small/medium deltas can replace an offered transfer at most every 33 ms, only
after its transport queue drains. The flight window is bounded to 16 snapshots
against retained installed baselines. Fragment retry is clamped to 100–2,000 ms
and follows twice the measured RTT. Large/bootstrap transfers use selective retry.

A reliable despawn may overtake an older completed snapshot on the unordered
lane. A client invalidates that exact transfer with existing feedback fields:
`receiving=the rejected snapshot ID`, `receivedMask=0`. `requestFull` is true
only when a full bootstrap is needed; a lifecycle-obsolete candidate retains the
last installed baseline when one exists.
The receiver ignores further pieces of that ID. The sender immediately replaces
that transfer with a fresh update against that baseline (or a full bootstrap); repeated feedback cannot cancel the newer
ID. Plain full-world requests without an ID do not repeatedly cancel a healthy
bootstrap. This also avoids full-world refresh storms during simultaneous deaths, and
avoids retrying an obsolete full world until the ten-second
transfer timeout. Removing an actor alone does not force a full refresh if the
next delta already contains its removal.

These semantics are covered by loss/reordering, delayed ACK, malformed payload,
selective retry and exact-transfer rejection tests in `Network/tests/ReplicationTests.cpp`.
