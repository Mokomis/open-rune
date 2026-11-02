# Android multiplayer menu and hosting

Public menu follow-up: [Find games and HyperDHT hosting](HYPERDHT-DISCOVERY.md). Host setup now offers public visibility and a match name, and Find games assigns player slots automatically. The private invitation flows below remain supported.


Newer [connection recovery follow-up](ANDROID-CONNECTION-RECOVERY.md) validates
Wi-Fi interruption, background/resume and host-loss behavior and improves the
recovery menu and rejoin handshake. The earlier untested recovery boundary below
is historical; see that checkpoint for exact scope.

Performance follow-up and newer installed build: [native hosting optimization](NATIVE-HOST-PERFORMANCE.md).
The measurements and APK hash below identify the original UI checkpoint.

October 28, 2026. The Android UI is wired to the protocol 17 client and a new
app-private host service. The ARM64 library and signed APK build successfully.
The signed protocol 17 APK is installed on the OPPO. Real touchscreen hosting,
invitation export/import, LAN joining, leaving and rejoining are validated below.
The earlier exhaustive laptop multiplayer playtests were not repeated.

## Player flow

From the title screen, select **Multiplayer**:

- **Host game** opens mode, player-slot, team/arena-size, score/round limit,
  time limit and map-rotation choices. HeadBall exposes optional limb scoring.
  Four player slots are the default; supported counts range from two to sixteen.
- **Start & join** confirms replacement of unsaved campaign progress, starts the
  host and joins its reserved player-one slot. Saved campaign files are retained.
  The invitation screen opens automatically.
- **Invite players** selects slots two through the configured roster size.
  Share the invitation with Android's share sheet, or save it using the document
  picker. Give a different slot to each player. The invitation is a private
  access credential; its contents are not shown in the menu or logs.
- **Join with invitation** imports a host's file using the document picker.
  Formats 1–4 are recognized, including normalized Windows line endings;
  matching engine protocol/gameplay content is still required by the native
  handshake. Invalid imports are rejected before leaving the campaign or
  replacing the remembered invitation. **Rejoin last match** reuses the last
  confirmed import while its host is still available.
- During a match, Menu offers Resume/Respawn, Settings, the scrolling score
  display, and Leave. A local host additionally has Invite players; its Leave
  confirmation explicitly ends the match for everybody. A joining client leaves
  only its own slot. A Stop match action is also provided in the foreground notification; its visibility
  depends on Android notification policy and is not part of the verified touch flow.

Maps are read from the installed game's `Maps/*.run` files. The menu filters
known team/Arena restrictions and preserves rotation selection order; the server
still validates every selected map before becoming ready. Changing a variant
removes incompatible selections and chooses a compatible default if necessary.
Campaign maps are not offered. Original assets and campaign saves are not edited.

Use the same Wi-Fi/LAN, or a reachable shared VPN/direct network. The host screen
offers Android-managed Wi-Fi/Ethernet IPv4 addresses first, followed by VPN
addresses. Unmanaged virtual interfaces are excluded; a host address disappearing
produces a disconnected warning. Invitations include the selected
address and the individual slot's UDP port. There is no public directory,
automatic router forwarding, relay or original Rune-server compatibility.
Sixteen is the existing protocol's functional ceiling, not a tested Android
hosting performance target; begin with four.

## Host lifecycle and isolation

`RuneHostService` runs in `:multiplayer_host`, a separate Android process because
the engine has global state. A user-started foreground service keeps the server
alive while an invitation is being shared or the client is in a menu. A partial
wake lock covers the server lifetime. The package ID remains
`org.runeclassic.port`; the update uses the existing signing key.

Every host creates `files/multiplayer/host/<UUID>/` with its own configuration,
saves, host log and invitations. The game directory is read-only source data.
A bounded status heartbeat distinguishes preparation, ready, stopping, failure
and a dead host. The service does not automatically restart an old match.
Stop requests reach the server's atomic cancellation flag and normal peer-close
path; a ten-second fallback ends a stuck loading process. The process is retired
after each match to prevent reuse of stale engine globals. Exiting the client
requests host shutdown. Original sessions remain on disk for diagnosis.

The invitation provider is non-exported, read-only and grants access only to an
explicitly shared player-two-through-sixteen URI. It cannot expose player one,
configuration, campaign saves or arbitrary files. Service entry points are also
non-exported. The foreground-service subtype describes the user-started game
server; it is not a general background scheduler.

## Validation

- ARM64 `runeclassic` builds with networking enabled; all four host JNI entry
  points are exported alongside the existing join/status/leave functions.
- The APK's Java classes, DEX and resources compile, and APK signature/license
  checks pass using the existing local key.
- `MultiplayerMenuModelTest` compares menu filtering against the existing map
  evidence: **157 supported choices pass; 459 unsupported combinations are
  rejected**, covering 44 maps and 14 rulesets. These are menu-model checks,
  not new map playthroughs.
- The same model test checks roster minimums, score/time limits, duplicate and
  invalid map selections, engine argument construction, invitation versions 1–4,
  CRLF normalization, oversized/truncated files and invalid fields.
### Cross-device fixes found during these checks

- UDP clients bind to an OS-selected outgoing interface. The headless bot's old
  loopback binding could not send to another device. Hosts still bind explicitly
  to the chosen local IPv4 address.
- Temporary UDP route/backpressure errors count as packet loss rather than
  terminating the entire host. Authentication, retry and timeout rules remain
  authoritative. A Wi-Fi loss exposed the old failure; deliberate network-loss
  recovery was not re-tested after this change.
- Lobby/intermission time no longer consumes the ten-second playing-world
  timeout. Both rendered clients and bots receive a fresh grace interval when
  play resumes. The original 45-second initial join timeout remains.
- The playing HUD names the actual mode instead of always saying Deathmatch.

### Device evidence

Local diagnostic logs, screenshots and timing data are in
`../../outputs/multiplayer-ui-2026-10-28/` relative to the repository root.
They include private invitations and remain outside Git.

| Mode | Representative map | Observed device result |
| --- | --- | --- |
| Deathmatch | DM-Hildir | Tablet hosts and autojoins; laptop bot joins slot two over Wi-Fi; touch Jump/Attack/Block, scoreboard, invitation share sheet/export and host stop exercised. |
| Team Deathmatch | DM-Team_ships | Tablet imports laptop-hosted invitation through the actual Android file picker; joins, leaves without stopping the host, rejoins and leaves again. |
| Arena | AR-Champions | Tablet hosts, waits in lobby for more than ten seconds, then enters Playing with the laptop peer without the former timeout. Map deselection/reselection, queue prompt, wins/losses and host stop verified. Arena combat rounds were not replayed. |
| HeadBall | HB-Agora | Tablet hosts two teams with limb scoring enabled, laptop joins, countdown reaches active match; team/points display and host stop verified. Goals were not replayed. |

Each session had four configured slots and two connected clients, not four human
players. Tablet-hosted DM delivered 3,516 active snapshot updates over 117.27 s
(maximum active gap 98 ms); Arena 1,023 over 34.196 s (71 ms maximum); HeadBall
2,792 over 95.319 s (242 ms maximum). All three peers completed normally when
Stop hosted match was pressed. The laptop-hosted TDM session was intentionally
terminated after both leave checks; its remaining bot then reported its expected
world timeout. It is not counted as a graceful host-shutdown test.

Actual touchscreen controls were used. An injected Android gamepad A event also
cycled the focused mode from Team Deathmatch to Arena and retained focus; this
is not a new physical-controller test. Android's Share chooser was inspected and
cancelled; no invitation was sent to anyone. Notification-stop interaction,
remote internet routing, sustained combat, full mode objectives and sixteen-player
Android hosting are not established by this run.

Native **2520×1680 / 4× MSAA** DM hosting presented **42.17 FPS over 13.257 s**
(560 frames), median interval 20.844 ms, p95 27.793 ms, maximum 34.741 ms and
no interval above 50 ms. SurfaceFlinger reported a 144 Hz hardware panel. The GPU
was 96–99% busy at a reported 646 MHz maximum in this sample. This does not
establish the cause of any single-player/multiplayer gap, a sustained combat rate
or 120/144 FPS. Arena and HeadBall screenshots are UI evidence, not timed FPS
benchmarks. Rendering optimization remains separate work.

The installed APK SHA-256 matches the built artifact:
`4353ea6a221cd7e41a0edfc698051ef521b597a854d52f1cde8226f15e90cebd`.
The existing signing key/package identity is preserved. All four original save
and save-backup hashes match, and `MainActivity.xml` preferences are byte-identical
to the pre-install copy. Native/AA diagnostics were session-only; the final launch
restores the stored Hardware Scaling 50% / 4× AA settings with the existing 144 Hz
policy. Test saves remain isolated. No unrelated device settings were changed.
The test hosts/peers are stopped and the tablet is left on Multiplayer.

The model tests and these connection/UI checks complement the prior laptop
coverage; they do not replace human combat, audio or endurance testing.

Model test command, from the repository root:

```sh
javac -d /tmp/open-rune-host-model Android/RuneHostConfig.java Android/RuneInvitation.java Tools/RuneValidation/MultiplayerMenuModelTest.java
java -cp /tmp/open-rune-host-model org.runeclassic.port.MultiplayerMenuModelTest Docs/RunePort/evidence/private-multiplayer/map-variant-matrix.csv
```

APK: `build-android/apk/open-rune.apk`. Keep `notices.zip`, `lgpl-sources.zip`
and `distribution-manifest.json` with any distribution, as described in the
[distribution guide](DISTRIBUTION.md).
