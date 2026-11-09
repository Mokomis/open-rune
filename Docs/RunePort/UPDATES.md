# Releases, update checks and multiplayer compatibility

Implemented November 7, 2026. This is the notification/download-page stage, not
an automatic installer. Nothing is published by building or running the checker.

## Player experience

Windows and Android: About & support → Updates. The Windows title/pause menu and
Android home footer show an Update available button after a newer stable release
is found. The desktop launcher has an Updates tab, including on Mac. `--updates`
opens the standalone desktop update window; `--legacy-launcher` opens the launcher.
Mac's default first-time-setup/game path does not yet expose the modern Windows
in-game menu; launcher/standalone entry points are the available Mac UI.

The page shows installed version/build, network protocol and gameplay rules,
check status, release-note preview and an external release/download-page button.
Startup checks can be disabled. Checks are asynchronous, once per process, with
a 60-second manual retry cooldown. Native headless tools never check automatically.
The desktop launcher starts an automatic check when opened; Windows/Android also
start it when the game menu is initialized/opened. No gameplay thread waits on HTTPS.

Only GitHub's published stable `/releases/latest` endpoint is queried, without
credentials. No drafts, prereleases, arbitrary download URLs, API keys or retail
assets are involved. Responses are bounded to 256 KiB, notes to 6,000 characters;
requests time out and retain normal HTTPS certificate validation. External links
are restricted to this repository's releases pages. Rate limiting, offline errors,
invalid responses and private/missing releases have explicit messages, never a
false “up to date” result. Full notes are available on the release page.

The repository remains private. An unauthenticated check currently reports no
public stable release. This is expected until the owner publishes a stable release
in a public release repository. Do not embed private GitHub tokens in clients.
Changing repository visibility or publishing releases requires explicit instruction.

## Version identities

`SurrealEngine/OpenRuneVersion.h` is the shared release identity:

- Version: `0.1.0-dev.3` currently, a development identity, not a released v1.
- Build: monotonic integer, currently 3. Increment for every distributed APK.
- Repository: `Mokomis/open-rune`.
- GameplayRulesRevision: 32 for quicker shared kick timing. Revision31 introduced
  normal-combat move costs/rush knockdowns; matching host/client updates are required.

The Android packager reads this header and the network protocol header, generates
Java constants and stamps APK versionName/versionCode. Native menus and `--version`
read the same identity. `Network/include/openrune/net/Protocol.h` owns wire protocol
25 and DTLS ALPN; discovery's GAME_PROTOCOL must agree. The native match builder
now uses the shared gameplay revision constant, with no actual revision change.

## Servers requiring client changes

Release version and multiplayer compatibility are deliberately separate:

| Change | Release version/build | Wire protocol | Gameplay rules |
|---|---|---|---|
| Rendering, UI, optional textures, harmless diagnostics | Increment | Keep | Keep |
| Server-only correction preserving the client contract | Increment | Keep | Keep |
| Damage, movement, authority or other required simulation behavior | Increment | Keep if format unchanged | Increment |
| Packet layout, handshake or required replication changes | Increment | Increment native + ALPN + discovery together | Increment if simulation also changes |
| Client correction that every participant must have | Increment | Keep if format unchanged | Increment to require the coordinated release |

Example: client 1.0.2 and server 1.0.3 may play together if their protocol, gameplay
revision and required content hashes agree. Server 1.1.0 with new packet fields
requires the corresponding client family; a client with the previous protocol
must not join. Release-name equality alone is neither required nor sufficient.

Existing enforcement remains authoritative: protocol/DTLS rejects incompatible
wire traffic; discovery partitions searches by protocol; authenticated match
contracts compare gameplay revision and gameplay/map hashes before play, including
map travel. Cosmetic replacement textures are excluded. This feature does not add
a minimum-version wire field or weaken any existing gate. Some protocol failures
still surface as a connection failure, rather than reporting the remote release
number, because an incompatible DTLS session cannot exchange a match contract.
Gameplay/content mismatch text now explicitly advises updating both host and client
to compatible releases. The installed compatibility numbers are visible in Updates.

Publish client and dedicated-server assets together under one stable GitHub Release
with compatibility numbers and hashes. Keep older releases for groups staying on
an older compatible server. Do not silently update a running match, kick players,
or force a host to restart. A future installer should stage an update for after
hosting ends. Publish supported platform binaries before making the stable release
visible, so clients are not notified about a release they cannot install.

## Staging a release

Use `Tools/RuneRelease/prepare_release.py --tag vX.Y.Z --asset PATH ... --output PATH`
to generate release-metadata.json outside Git beside staged binaries. It verifies
that tag and compiled identity match, native and discovery protocols agree, and
asset filenames are unique; it records file sizes and SHA-256 hashes. It does not
prove each binary came from that source: build provenance and each binary's
`--version`/APK metadata must also be checked before publishing. `--development`
permits local staging of the current development identity, never a stable release.
The runtime checker reads GitHub metadata; this staged manifest is for release
validation and the later verified-download installer, not currently consumed by it.

## Checks

- `SurrealEngine --rune-update-probe`: native SemVer, release filtering, malformed/
  oversized response, URL allowlist and error-path checks, no game data or renderer.
- `SurrealEngine --check-updates`: one real HTTPS check; also usable by headless hosts.
- `Tools/RuneValidation/ReleaseVersionTest.java`: host-JDK Android version precedence.
- `python -m unittest discover -s Tools/RuneRelease -p test_release.py`.
- Existing network contract/replication tests cover incompatible protocol/rule/content
  rejection. No protocol or simulation behavior changed in this update.

Automatic downloading, signature/hash verification of downloaded executables,
replacement helpers and rollback are deferred to the installer stage before v1.
The current checker never downloads binaries, modifies game files, or restarts apps.
