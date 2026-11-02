# Open Rune public discovery

Current gameplay protocol is 21; discovery uses topic `open-rune/multiplayer/protocol-21/v1`. Both endpoints and their discovery runtimes must update together. Snapshot 6 and invitations 4 remain. See [character selection](../../Docs/RunePort/CHARACTER-SELECTION.md). Earlier version statements below describe their original checkpoint.
HyperDHT 6.35.0 supplies the public directory and authenticated peer connections.
Android embeds Bare Kit 2.5.5 locally; it does not depend on a laptop, Node server,
web service operated by us, or downloaded JavaScript. Desktop uses Node.js 24.
This integration is private to this fork; do not submit it upstream.

## Build Android

From this directory:

```sh
npm ci --no-audit --no-fund
npm test
python3 build_android.py
```

`build_android.py` downloads the pinned Bare Kit release and verifies its SHA-256,
then prepares the ARM64 bundle, libraries, notices and hash manifest in ignored
`build/android/`. `--archive /path/to/prebuilds.zip` reuses a cached release.
`Android/build_apk.py` includes this runtime by default; `--discovery-runtime DIR`
selects another prepared directory and verifies its manifest before packaging.
Do not commit generated libraries, bundles, caches, session files or credentials.
Notices are included in the app's existing license browser and release archive.

## Play

Android: **Multiplayer → Host game → Visibility: Public · Find games**.
Set **Match name**, mode, maps and player count, then **Start & join**. Other
players choose **Multiplayer → Find games**, select the host and join. Public
matches allocate slots automatically. Invitation-only hosting remains available.
A local player host reserves slot 1; a headless public server offers every slot.

On desktop, after `npm ci` here, use the existing launcher:

```sh
python3 Tools/RuneMultiplayer/private_session.py --headless --public \
  --binary build/SurrealEngine.app/Contents/MacOS/SurrealEngine \
  --game ../rune-classic --session /path/to/new-session --players 4
```

Use `--host --public` for host-and-play. To join the printed public host identity,
use `--join-key HEX_ID` instead of `--join INVITATION`. All processes, configuration
and saves remain scoped to that launcher's fresh session directory. Public hosts
bind the native game sockets to loopback; never share their internal invitations.

## Protocol and transport

- Topic `open-rune/multiplayer/protocol-17/v1`, hashed with hypercore-crypto's hash.
  Only public host identities are announced. No player credentials enter the DHT.
- Search verifies each candidate over a Noise-authenticated connection with a
  fresh nonce, host identity, schema and game protocol. At most 64 candidates,
  four concurrent checks, five seconds per check and 20 seconds overall.
  Partial results can be selected while the search continues.
- Admission assigns a slot over the authenticated connection. Each new owner
  gets fresh DTLS identity/key material. The native authority reads the private
  invitation only while that slot is vacant; its roster/map/rules cannot change.
  A returning peer can reclaim its lease. Abandoned leases have a 45-second grace;
  clients that never enter the game cannot reserve a slot indefinitely.
- The reliable stream carries only status/admission/keepalive. Actual 1–1200-byte
  game datagrams use the pinned secret-stream `trySend`/`message` unordered API,
  with its authenticated encryption plus the game's existing DTLS protection.
  Loopback UDP adapters retain message boundaries. No ordered-stream fallback.
- Per-peer ingress is bounded to 2048 datagrams and 2 MiB per second, frames to
  4 KiB, host connections to 32. These are resource bounds, not an abuse audit.
- Native protocol/ALPN 17, snapshot 6 and invitation format 4 are unchanged.
  New public admissions reset the previous occupant's score; same-peer rejoin
  retains its lease and normal reconnect behavior. Simulation/interpolation,
  combat defense windows and game rules remain in the native engine.
- Host metadata comes from the native authority's atomic status file. Stale
  hosts are not listed; a longer grace lets map loading finish without destroying
  discovery. Search completion never proves that every public host was found.

## Runtime boundaries

Public HyperDHT bootstrap infrastructure is still needed for a cold start.
There is no Open Rune master directory or hosted relay service. HyperDHT can
attempt peer hole punching; blocked UDP or incompatible NAT may still prevent a
join. Network addresses are exposed to participating peers. This is public host
discovery, not accounts, matchmaking, host reputation or cheat prevention.

`runtime.cjs` owns one worklet/desktop process. Its bounded commands and metadata
use app-private atomic files. Only bundled local code executes. The Android
foreground host service owns the host worklet; the activity owns browsing/client
transport. Cancellation and timeout return to the menu, and old hosts must pass a
fresh status check on every refresh. A process restart creates a new peer identity.

See [the integration checkpoint](../../Docs/RunePort/HYPERDHT-DISCOVERY.md) for
actual device evidence and remaining internet/NAT testing. The earlier prototype
was a separate synthetic laptop experiment, not proof of Android or game joining.
