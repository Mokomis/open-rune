# November 3, 2026 client deployment

This checkpoint supersedes the earlier November 3 “not deployed / not pushed”
statements. Both development repositories remain **private**; v1 is unreleased.
No release, public-visibility change or upstream submission was made.

## Matching source

Runtime changes are committed at `ec8ca837ab88b318a35eecd9d937b507789915c6`:

- `4800323d`: material lighting/local probes, enhanced gore, darker blood,
  corpse pickup/throw and relaxed, proportion-preserving body poses, including
  the accumulated validation tools and documentation.
- `ec8ca837`: rename a corpse contact-point variable that conflicts with the
  Windows SDK `near` macro; restore the pinned Swappy archive when cleanup has
  removed its ABI library while leaving extraction metadata behind.
- Existing November 3 moving-brush, rider and script-relocation fixes are included.

Multiplayer wire protocol and ALPN are **22**; native match rules are **22**.
Use matching clients/hosts for the new corpse interactions. Discovery bundles
were checked against the native protocol. No new artwork or sound pack was
installed, and existing optional-feature choices were retained.

## Deployed clients

| Platform | Updated runtime | SHA-256 |
| --- | --- | --- |
| Mac ARM64 | `build-multiplayer-20261101/SurrealEngine.app/Contents/MacOS/SurrealEngine` | `dbbbe35f50bcb305167e3dd9e7115a40d6d775868c4d1af4d2b3eb0511b89caf` |
| Windows x64 | Usual `build-windows/SurrealEngine.exe` and `SurrealEngine-Menus-20261101.exe` launcher | `c020c9bef2ab1b286bce9fc9d7fdbb23f08fa60c19a2dada62400d066f389f62` |
| Android ARM64 | In-place `org.runeclassic.port` APK update on OPD2515 | `75dc42b74a832e91a58962710345aa2a11858b76c5572f96871b792553f7bd15` |

Windows was built from a separate `SurrealEngine-client-sync-20261103`
checkout. Its previous working tree, including 173 changed/untracked source
files, was backed up and verified unchanged. The installed runtime's resources,
libraries, discovery code and notices were updated alongside both executable
names. `WINDOWS-HANDOFF.txt` identifies the active source for subsequent builds.
The older dirty checkout must not be rebuilt as if it were the current source.

Android retained its signing certificate and optional app-local Turnip payload.
The update used replacement installation, with no uninstall or data clear.
Normal launch has no test-save or diagnostic-intent extras.

## Verification and preservation

- Fresh Mac and Windows builds pass the single-player, network and team
  corpse-interaction fixtures, including pickup, throwing, guarding, eligible
  bots, relaxed poses, flight/settle frames and snapshot geometry parity.
- Windows lifecycle/discovery checks pass: advancing match status, isolated
  configuration, owner/system invitation ACLs and rejection of invitation-folder
  reuse without changing existing invitations. The bounded host exits normally;
  no public match is left running.
- Android package signatures match the previously installed APK. All 14 recorded
  save, INI and preference files match their pre-install hashes immediately
  after installation. The normal menu launches, and the read-only original-save
  check confirms existing save slots without loading or overwriting a save.
- Mac's eight recorded original INI/save files and Windows's nine recorded normal
  save/settings files remain unchanged. Prior binaries, patches and data backups
  are retained outside Git.

Evidence and deployment manifests are under `outputs/client-sync-20261103`
on the Mac, with corresponding Windows records under the same output name.
Android includes notices, matching LGPL sources and a distribution hash manifest.
Windows notices and matching LGPL sources accompany the updated runtime.

These checks establish build/deployment and bounded native behavior. They do
not establish new sustained Android FPS, an end-to-end human cross-platform
match, every campaign encounter, or exhaustive visuals on each platform.
