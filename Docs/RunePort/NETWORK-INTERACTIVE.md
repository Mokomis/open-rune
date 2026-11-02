# Interactive private multiplayer checkpoint

Current multiplayer checkpoint: [four private game modes](NETWORK-MODES.md). Protocol/ALPN 14, native rules 14; older checkpoint versions below are historical. One representative map per additional mode was validated.

Latest mode checkpoint: [Team Deathmatch](NETWORK-TEAM-DEATHMATCH.md), protocol/ALPN 12 and native rules 12. Earlier versions below are historical.

Latest Deathmatch checkpoint: [four players, scoring, match limits and map rotation](NETWORK-DEATHMATCH.md). Protocol/ALPN 11 and native rules 11 supersede earlier checkpoints below.

October 26, 2026. The opt-in build now has a dedicated authoritative server and
interactive rendered clients. This supersedes the earlier statements that the
adapter is only a headless probe. It remains an experimental two-player
DM-Hildir match, using Open Rune's protocol, not original Rune server compatibility.

Latest follow-up: [movement prediction and the selected defense window](NETWORK-RESPONSIVENESS.md).
The earlier validation entries below retain their original build boundaries.

## Contract and ownership

Protocol and DTLS ALPN are **10** (`open-rune-net/10`). Snapshot payload version 5
adds an attachment joint to each entity (77 bytes); a full 512-entity snapshot is
39,486 bytes / 62 fragments with the optional owned movement checkpoint. Carried weapons/shields follow their server-owned
joints; thrown and loose inventory is captured separately. The server owns the
60 Hz simulation, damage, equipment, pickups, death and respawn. Snapshots are
captured at 30 Hz. Client input is sent at 50 Hz and presentation runs every
rendered frame using the existing fractional Rune body/torso animation path.
Grounded local movement now predicts and reconciles; melee contacts use the selected 33 ms defense window. See the follow-up for exact bounds and unsupported movement modes.

Respawn is action kind 9. It requires the same owned pawn and life, an authored
dead state, and at least three server seconds since death was observed. Original
RestartPlayer/StartPlayer events select a spawn and restore equipment; the life
generation increases and old commands cannot carry into the new life. Control
record type 8 carries an authenticated leave or match-ended reason. One player
leaving ends this small private match for both players.

Each host run generates separate random credentials for its two invitations.
Files are created with mode 0600 inside a mode-0700 directory. Share each file
only with its player; the file is a bearer credential. Credentials are never
printed in logs. DTLS binds authenticated peers to their actual UDP source and
requires agreement on the map/script/rules contract before gameplay starts.
Incomplete authentication attempts expire. This is not a production public
hosting service: two per-player UDP ports, explicit IPv4 binding, no discovery,
NAT traversal, reconnect/resume, moderation, or internet service management.

## Running a match

Configure the root build with `-DOPENRUNE_ENABLE_NETWORK=ON` (default remains
OFF). All command-line fixtures must supply isolated save directories.

```
SurrealEngine --direct --save-directory=/private/host-saves \
  --rune-server=/private/new-invitations --rune-net-address=192.168.1.10 /path/to/Rune
SurrealEngine --direct --save-directory=/private/player-saves \
  --rune-join=/private/new-invitations/player1.rune-invite /path/to/Rune
```

Use the host's actual LAN IPv4 address for separate devices; the default is
loopback. The invitation folder must be new. Both slots must finish joining
before simulation starts. `--rune-net-seconds=N` bounds development runs.
`--rune-net-test-arena` and `--rune-net-test-input` are explicit test fixtures,
never normal match defaults. The socket adapter currently supports macOS,
Linux and Android; Windows socket integration remains unfinished.

Android's title menu has **Multiplayer → Open invitation** using the system
file picker. The selected file is copied into app-private storage. Joining asks
before replacing unsaved campaign progress. Existing saves are retained;
replicas cannot call the campaign save writer. Pause offers Resume, Settings
and Leave match; after death it offers Respawn. The match continues while a
menu is open, with local movement/attack released. Existing touch and physical
controller mappings feed the network intent path, including Enter for Attack,
Ctrl for Use, Right Ctrl for Block, Q for Throw, F for Kick and Space for Jump.
The themed HUD displays confirmed health/shield; unreplicated power/air meters
are not invented. Android retains surface geometry, frame pacing and graphics
controls. The same package and signing key are used, with INTERNET permission
and the pinned Mbed TLS notices bundled for networking builds.

## Validation boundary

### Human blocking practice

`Tools/RuneValidation/android_combat_playtest.py` hosts an isolated DM-Hildir
sparring session for the tablet (player 1), with a desktop client driving only
player 2. Both start facing each other with shields. The opponent waits 30 seconds,
then cycles weapon attack, kick, and shield bash every four seconds using ordinary
authenticated actions and authored animation wind-ups. It follows within a short
clear arena range; it is not general navigation AI. Damage and shield breakage
remain real. Use the Android Respawn button after death; an accepted respawn
repositions the practice pair together. There is no scheduled forced death.

The launcher defaults to 50 ms added delay each way and +/-5 ms jitter on the
tablet connection, without artificial packet loss. LAN latency is additional.
The proxy binds only the explicitly supplied local interface and accepts only
the specified tablet IPv4 address and first UDP endpoint. Desktop smoke checks
can use `--desktop-check --host 127.0.0.1 --tablet 127.0.0.1`.
Normal loopback proxy defaults stay unchanged. Credentials remain private and
temporary; leaving ends the match, and the launcher cleans up its host/opponent.
Sessions expire after 30 minutes by default. Native resolution and 4x AA are
session overrides; saved preferences and campaign saves are preserved.

The first desktop practice check confirmed accepted weapon/kick/bash actions
and natural contacts. This establishes the setup, not human-perceived fairness.
The practice flags are optional desktop fixtures and do not require replacing
the existing protocol-10 Android APK.

The desktop interactive fixture renders two independent clients at native
window resolution and 4x AA. It checks movement, a natural kick, jump, throwing,
scripted server death, client-requested respawn, new life and orderly leaving.
After the Android menu integration changes, both clients passed: 1,322 / 1,312
frames, 407 / 405 installed worlds, one kick result each; player two observed
death then life 2 with restored health. These are callback-driven controls,
not a manual playtest or a 120 FPS performance claim.

The separate impaired engine duel also passed at 50 ms one-way delay and 3%
loss: both clients observed kick/bash health 100 → 93 → 90 and interpolated
between installed worlds. The standalone 10-group suite covers the protocol,
authentication, clocks, input admission, snapshots and close records.

### Android device results

The OPPO OPD2515 ran against the Mac host over LAN, including a join through the
real system file picker. Actual touch movement/look, Attack, Jump, Kick, Throw,
Use, Respawn and Leave were exercised. The server accepted these intents; Use
admission alone is not proof of a successful pickup. The first run installed
2,717 worlds over 10,934 rendered frames, observed health 100 → 93, death and
respawn to life 2 / health 100, and moved 1,566 units. A later file-picker match
installed 1,991 worlds over 7,682 frames and moved 2,623 units. Leaving ended the
other client; the other client leaving also returned Android to its title menu.
Final UI checks confirmed host-controlled combat settings and preserved 144 FPS
pacing selection across menu/respawn transitions. Physical controller input was
not manually tested in this pass.

Visual inspection caught missing body groups on replicas: skipping PreBeginPlay
also skipped the skeleton's authored polygon/joint flags. The adapter now copies
those visual defaults without running gameplay scripts. A new engine assertion
and the retained Android screenshot verify the correction. Dynamic severing and
skin changes are still outside the snapshot schema.

A delayed-second-player test caught the initial join timeout incorrectly ending
an authenticated lobby. That timeout now applies only before match agreement.
A later intermittent old-world stall exposed missing cross-lane removal recovery:
retired actor references request a new full world, missing dependencies retry,
and a ten-second world-update outage ends the client cleanly. The engine probe
covers a retired actor overtaking a snapshot, then successful fresh-world
installation. The final 25-second delayed-join fixture passed: 4,472 / 1,705
rendered frames, 456 / 450 installed worlds, kick, jump, throw, death and respawn.
The earlier failed delayed run is retained separately; it is not counted as a pass.

Native **2520×1680 / 4x MSAA** was confirmed by the renderer. SurfaceFlinger
measured **68.18 displayed FPS** over 10.68 seconds in the corrected-model view,
with no interval over 50 ms (maximum 20.84 ms). An earlier different-view sample
measured 87.25 FPS. These sequential samples do not isolate a performance change
or establish 120 FPS. The hardware panel stayed at 144 Hz. Existing enhanced
rendering settings were retained; no scaling or effect tradeoffs were made.
Campaign-save hashes and saved preferences matched before/after testing.
See [retained evidence](evidence/network-interactive/results.json).
Unfinished: prediction for other movement modes, attack rewind, combat audiovisual
feedback replication, inventory selection, scoring/rounds, movers/world events,
map travel, larger player counts, long sessions and broad network/lifecycle
coverage. The fixed arena is not campaign co-op or 32-player readiness.


### Follow-up performance audit (October 26)

The previous 68 FPS sample must not be compared with an unrelated single-player
view or a different scaling setting. The saved everyday setting was Hardware
Scaling at 50% dimensions, while the earlier network validation forced native.
With all existing effects and 4x AA retained, the follow-up fixed-camera native
sample measured about 81 FPS in both modes once Ragnar was visible in each.
The first single-player sample with Ragnar outside the view reached 96 FPS.
These are comparable workloads, not identical actor positions/counts or poses.
The native client spent about 0.08 ms/frame processing received network state,
0.20 ms on presentation/input/audio work, and 12.02 ms rendering. Native 120 FPS
with these effects remains unmet; the primary limit in this view is shared
rendering work, rather than protocol processing. Hardware refresh remained 144 Hz.

At the user's saved Hardware Scaling 50% setting, multiplayer measured 143.58
presented FPS (1260x840 scene, 2520x1680 display, 4x AA). This is a separate scaling
result, not a native-resolution speedup. No quality or saved preference change
was used to disguise a native result.

`--profile-frames` now reports `OPEN_RUNE_CLIENT_PROFILE` after 60 active-match
warm-up frames, including receive/install, presentation, render, limiter and
95th-percentile frame times. `--probe-view` also works in the network client and
requires isolated saves. `Tools/RuneValidation/android_network_performance.py`
provides a bounded private LAN test, removes its temporary invitation, captures
actual presentation timestamps and clock samples, and preserves normal saves.
It restarts Open Rune, so use only when discarding the current unsaved view is
safe. Native is the default; `--saved-resolution` explicitly keeps saved scaling.

Material-family shader specialization and zero-contribution lighting shortcuts
were tested and removed after failing to show a material gain. Grouping the
three shadow-atlas sections in one render pass passed the native GPU fixture,
including all-pixel agreement with the original path within one 8-bit value,
but gave no worthwhile gain in this view. It remains disabled by default:
`--render-group-shadows=1` / Android `--ei groupShadows 1` enables the diagnostic.
No main renderer quality or pacing defaults changed. The interactive test's
bounded duration now allows cold pipeline compilation before its respawn sequence.
See [performance evidence](evidence/network-interactive/performance/results.json).


## Shared renderer optimization (October 26)

The subsequent [native 4x lighting pass](RENDER-LIGHTING-PERFORMANCE.md) improves
this client's matched fixed-camera sample from 79.99 to 88.59 displayed FPS with
all effects on. Network receive/install remains 0.076 ms/frame in the optimized
sample. The improvement is in shared rendering, not a protocol or interpolation
change. `android_network_performance.py --reference-renderer` reproduces the
previous shader path on the same installed build.
