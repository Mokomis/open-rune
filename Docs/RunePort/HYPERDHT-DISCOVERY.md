# HyperDHT discovery and Android joining — October 28, 2026

Character selection now uses protocol/ALPN 20, rules revision 20 and discovery topic `open-rune/multiplayer/protocol-20/v1`; both endpoints must update. Snapshot 6 and invitations 4 remain. See [character selection](CHARACTER-SELECTION.md). Earlier version statements below describe their original checkpoint.
Public hosting, Find games and automatic joining are integrated. The Android
client contains its own Bare Kit worklet and HyperDHT stack; no laptop discovery
bridge is required. The existing private invitation path remains available.
Native protocol/ALPN 17, rules 17, snapshot 6 and invitation format 4 are unchanged.

## User flow

- Multiplayer → Host game → choose Public or Invitation only. Match name,
  maps, mode, rotation and limits are set before starting.
- Public hosts appear in Find games with their name, mode, current map, player
  count and available capacity. Choose a result to join; no invitation export is
  needed. Refresh searches again; Cancel stops a pending search/join.
- Rejoin last match remembers the discovered host identity and negotiates a new
  local connection. Expired public hosts produce a bounded error rather than an
  endless loading screen. Existing campaign replacement confirmation remains.
- Android hosts run in the existing separate foreground service. Public mode
  binds the native game sockets to loopback and assigns external slots through
  authenticated HyperDHT admission. Slot 1 stays reserved for the device owner.
- Desktop `private_session.py --public` publishes a host; `--join-key` connects.
  A public headless host offers all slots. See the discovery README for build steps.

## Implementation

[Tools/RuneDiscovery](../../Tools/RuneDiscovery/README.md) contains the portable
control protocol, peer manager, unordered datagram bridge, embedded entry point,
Android preparation tool and regression tests. Android `RuneDiscovery` owns the
worklet; `RuneHostService`, `RuneHosting`, `MainActivity` and `RuneMenuView` connect
its lifecycle and UI. `RuneMultiplayer.cpp` publishes bounded authoritative
metadata and reloads admission credentials only for vacant public slots.

The public DHT holds host identities, never invitation keys. Browsing authenticates
and challenges each host before displaying it. Admission rotates per-player keys
for a new owner; same-peer reconnect leases are retained briefly. Packets stay
unordered and retain DTLS protection; no reliable-stream gameplay fallback is
used. Runtime dependencies and notices are pinned and bundled with the APK.

## Validation and limits

Evidence is outside Git at `outputs/hyperdht-2026-10-28/` under the task root.
Checks use isolated session saves/configuration; original campaign saves and
user graphics preferences are preserved. Android launches use session-only
native 2520×1680 and 4× AA overrides. The user's 120 Hz / 120 FPS pacing choice
remains unchanged; this task is connection validation, not a new FPS benchmark.

Observed on the OPPO over USB debugging and the real public DHT:

- Tablet Find games verified a laptop host and joined through the public UI.
  Native authority/client logs confirmed the player and live world replication.
- A short Wi-Fi interruption recovered without restarting the match. The normal
  recovery menu offered Resume. Leave and Rejoin returned to the same host.
- Tablet public hosting accepted a laptop peer through HyperDHT. Native host
  reported two joined players and a running match. A bounded laptop peer observed
  5,504 world updates, with a 52 ms maximum active-world gap over about 183 seconds.
  This is snapshot cadence, not rendered FPS or human combat fairness.
- Stopping the tablet host ended the peer session. Cancelling a join after the
  selected laptop host stopped returned to the menu.
- Local DHT tests cover frame size/truncation/timeouts, invitation validation,
  concurrent distinct slots, full hosts, 1200-byte datagram integrity, reconnect,
  lease expiration/key rotation, stale-host filtering, deadlines and cancellation.
- Final installed-build checks also verified a named tablet host, all slots on
  a public headless host, and rejection of an additional player when full. The
  public tablet host kept serving its laptop peer during 30 seconds in the
  background. A 120-second peer run received 3,521 worlds with a 155 ms maximum
  active-world gap. The host remained available after its local player timed out.
- Four focused discovery tests pass, and the menu model still validates 157
  supported combinations while filtering 459 unsupported combinations.
- ARM64 and macOS engine builds and APK signing/notice verification pass.

The initial checks above used one router. The follow-up below validates one
separate-network pair; it does not establish universal NAT compatibility.

## Separate-network follow-up — October 28

Evidence: `outputs/hyperdht-cross-network-2026-10-28/` under the task root.
The OPPO used a phone's cellular hotspot while the laptop stayed on home Wi-Fi.
Independent HTTPS checks confirmed distinct public IPv4 addresses. USB supplied
debugging only: ADB forward and reverse lists were empty before and after tests.
No router forwarding, system network changes, owned relay or hosted directory
was needed. The existing signed APK passed; no gameplay/transport fix or new
installation was necessary.

- **Laptop host → tablet client:** the real Android Find games screen found the
  laptop public host (verified status response 1,755 ms), then joined DM-Hildir.
  Native logs confirmed admission and match start; real touch movement changed
  position and touch actions were accepted by the remote authority. Leave and
  Rejoin last match returned successfully to that host. Screenshots and native
  logs confirm 2520×1680, 4× MSAA and the preserved 120 FPS pacing target.
- **Tablet host → laptop client:** the laptop discovered and verified the public
  tablet host (status response 2,013 ms), then joined through HyperDHT. The tablet
  authority admitted both players and started the match. A 150-second native
  peer received 4,425 snapshots; its 147.6-second playing interval had a longest
  update gap of 112 ms (119 ms including the initial non-playing interval).
- A fresh remote peer then exercised the existing movement/attack/kick/block/jump
  stress input for 120 seconds while the tablet's local game was visible at
  native resolution and 4× AA. It received 3,469 snapshots, with a longest active
  gap of 78 ms and no disconnect or native validation failure. The tablet host
  logged accepted remote actions. This exercises transport and replication; it
  does not establish human melee fairness or prove every attempted move was legal.
- World update rates were approximately 30/s. These are network snapshot cadence
  measurements, **not rendered FPS or end-to-end input latency**. Screenshot FPS
  is incidental and is not a sustained performance benchmark.
- Test hosts were stopped afterward. Original campaign save hashes and graphics
  preferences match their pre-test values. The tablet remains on its hotspot;
  normal launch restores the saved 50% hardware scaling after the session-only
  native overrides. No assets, saves, source changes from other work or existing
  packs were replaced.

Public HyperDHT bootstrap infrastructure is still needed. Other router/carrier
combinations, incompatible NAT, UDP-blocked networks, relay requirements,
16-player internet performance, long endurance and human internet melee fairness
remain unvalidated. No owned public directory or relay service has been deployed.
Hosts can still be unreachable despite a valid directory record. This result
covers one real internet pair in both directions, not every network or map.
