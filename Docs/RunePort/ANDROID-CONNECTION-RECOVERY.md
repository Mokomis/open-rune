# Android multiplayer connection recovery

October 28, 2026. Protocol 17 is unchanged. This follow-up tests common Android
connection/lifecycle failures; it does not repeat the map/mode matrix or establish
internet routing, human combat fairness, player capacity or endurance.

## Player behavior

- A connected client with stale playing-world updates (1.5 seconds), or an
  unavailable synchronized connection clock, enters **Connection interrupted**.
  Android opens Multiplayer, releases touch/controller input and disables Resume.
  Native code clears queued actions and prediction and sends neutral input while
  waiting. Existing action-age limits still reject obsolete combat commands.
- A brief interruption can recover the same authenticated session. The menu says
  **Connection restored. Resume when ready.** It does not put the player back in
  combat without their input. Lobby/intermission snapshot silence alone is not
  treated as an outage.
- An expired connection returns directly to Multiplayer with an explanation and
  **Rejoin last match**, using the invitation actually used for this session.
  A local host instead offers **Join hosted match** while its service is alive.
  Rejoining from this recovery screen does not show the campaign replacement
  confirmation; joining from an active campaign still does.
- A host may retain an abandoned endpoint briefly. A new client retries failed
  initial handshakes with two-second backoff, within the existing 45-second join
  deadline. It never resets an agreed session/replica, including map travel, and
  does not retry an explicit content mismatch or host close. Leave cancels retry.
- Normal remote closure says the host ended the match; silence says the
  connection was lost. Failed initial joins explain that the host/invitation
  should be checked. Terminal messages can no longer be overwritten by a
  misleading loading status.
- Temporary UDP receive route/ICMP errors, like the existing send-side handling,
  are treated as packet loss. Authentication and existing timeouts still decide
  session validity. No credentials or content checks were weakened.

The foreground host continues while the client app is backgrounded. Android may
suspend the local rendered client long enough to expire its connection; returning
then offers rejoining. This is not host migration: a dead host requires a new
match/invitation, and an address change may require restarting hosting.

## Device checks

OPPO OPD2515, USB debugging with actual Wi-Fi interruptions, laptop peer on the
same LAN, DM-Hildir. Rendering used session-only native 2520×1680 and 4× MSAA;
saved Hardware Scaling 50%, 120 Hz/120 FPS/pacing and campaign saves are preserved.

| Case | Observed result |
|---|---|
| Baseline 25-second background pause | Old build returned to the title screen with no visible connection explanation. |
| Brief client Wi-Fi loss | Waiting state appeared; updates resumed within the same session; Resume enabled with the recovery notice. Wi-Fi was disabled for about 4.8 seconds including screenshot overhead. |
| Longer client Wi-Fi loss | World timeout returned to Multiplayer. One touch on Rejoin succeeded after a logged handshake retry while the host retired the old slot. |
| Three-second background pause | Session preserved. |
| Thirty-second background pause | Expired connection explained; touch Rejoin successfully entered the existing match. |
| Abrupt laptop host termination | Tablet returned to Multiplayer instead of remaining in a frozen match. |
| Normal remote host closure | Final-build tablet showed the explicit host-ended message; laptop server and peer both exited normally. |
| Cancel during retry | Actual menu Leave/confirmation stopped retry and returned to Multiplayer. |
| Unavailable host | Final-build fixture retried initial authentication, then returned with the host/invitation explanation at the 45-second join deadline (53.7 seconds including app/map startup and capture). |
| Tablet host backgrounded for thirty seconds | Host process PID stayed unchanged, laptop peer kept receiving updates, and local Join hosted match succeeded on return. |
| Brief tablet-host Wi-Fi loss | Host and laptop peer recovered without restarting the match. The peer's deliberately impaired interval reached 8.568 seconds between worlds; this is recovery evidence, not smooth gameplay. |
| Stop hosted match | Host state became stopped and laptop bot completed normally. |
| Abrupt tablet host death in a lobby | Host heartbeat/session loss was detected; Host game returned; a fresh hosted match started successfully. |

Builds pass for Android ARM64 and macOS. Focused network checks pass: replication,
authentication/clocks, independent replication wire fixtures and DTLS loopback.
The first wire-test invocation used a stale test executable; rebuilding the test
targets resolved that mismatch without changing protocol code.

## Reproduction and evidence

`Tools/RuneValidation/android_connection_recovery.py` provides bounded
`short-wifi` and `join-timeout` cases against a supplied disposable invitation.
It requires USB debugging, isolates saves, restores Wi-Fi in `finally`, retains
logs/screenshots and never prints invitation credentials. Short-Wi-Fi needs a
running host and another joined player; join-timeout needs an unavailable host.
Use a new evidence directory for each run. The test restarts the app.

The final APK SHA-256 is
`0d19c39d79143010c7e7faffe267804ad0dbb14d7b45ca58c94de4f1553cf495`.
Installed APK hash matches. All four checked campaign save/backup hashes and
`MainActivity.xml` match the pre-test copies. Wi-Fi is restored at the original
LAN address, package refresh rule remains 3 (120 Hz), and normal launch restores
saved 50% scaling. Test hosts/peers are stopped; the title menu is left visible.
The reusable short-outage and unavailable-host fixtures passed with the final
native library. The final Java changes prioritize the current session message
over an old host error and handle an empty/null notice safely. The final UI check explicitly verifies the remote host-ended message in the
presence of an older local-host error. The final source also restricts initial handshake retries to sessions that
have never agreed a match, so a stalled map transition cannot restart against the
invitation's old map contract.

Detailed local evidence is in `outputs/android-recovery-2026-10-28/` above the
repository's `work/SurrealEngine` directory. It includes private test invitations
and remains outside Git. Logs distinguish intermediate builds and failed
exploratory runs from passing captures. An early harness read stale launch logs;
subsequent launches clear the test log and wait for a fresh join. Accessibility
captures were moved outside timed Wi-Fi outages after an idle wait accidentally
lengthened a short outage. Screenshots/native logs support the passing results.

No full map replay, router/NAT changes, thermal-policy changes, commits or pushes
were performed. Human combat/audio tests and real internet connectivity remain
separate release work.
