# Open Rune release completion plan

Scope established November 4, 2026. This records the user's requirements after
the release-readiness review. It supersedes the suggestion to defer texture
completion or ship Mac without the modern menus. It is a plan, not a claim that
these requirements have been implemented or validated. No publication, change
to repository visibility, deployment or remote-machine operation accompanies
this document.

## Required deliverables

| Deliverable | Current evidence | Completion criteria |
| --- | --- | --- |
| Complete texture updates | Live production queue: 245 approved masters, 51 review, 1,374 pending. Character remasters are already partly included; do not generate them again. | Reconcile the complete inventory; finish all eligible replacements; account explicitly for animation, sky, masked/effect and utility groups; build and validate final 2x/4x packs with mip chains and appropriate material maps. |
| Android, Mac and Windows feature parity | Shared gameplay/rendering core, but newest changes are Mac candidates. Modern desktop menu/session sources are currently Windows-only. | All three clients expose the same applicable gameplay, graphics, multiplayer, character and settings features, with matching network rules and preserved saved preferences. Platform-specific controls remain platform-specific. |
| Linux client | Existing upstream Linux build recipe; no current Open Rune Linux runtime certification. | Reproducible Linux x86-64 build, packaged dependencies/discovery, desktop menus, successful startup/import, saves and network checks in a controlled Linux environment. Distinguish software-rendered checks from hardware GPU validation. |
| Client updates | No completed shared GitHub updater identified in this audit. | All clients can check GitHub releases, show installed/available versions and release notes, and offer the correct platform download. Add verified download/install where supported. |
| Dedicated Windows and Linux servers | Existing headless authority and public-discovery launcher. | Distributable headless packages, bundled runtime dependencies, documented configuration, clean shutdown, logs, bots, map/mode rotation, discovery/direct join and reconnect tested on both platforms. |
| Desktop menu interaction | Windows has themed menus; Mac modern-menu parity remains work. | Mouse/keyboard-first interaction on desktop, retaining optional controller navigation and Rune theming. No controller requirement or Android touch-layout assumptions. |
| Revamped touchscreen controls | Existing themed movable/resizable dual sticks and action buttons; user requests a further redesign. | Review layout, reach, readability, touch targets and responsiveness. Retain configurable positions/sizes, simultaneous movement/look/actions, clear move cues and controller-aware hiding with Menu accessible. Validate actual touch input. |

## Texture completion

Use the existing resumable production queue and faithful generative art direction:
retain original colors, brightness, UV positions, masks and restrained material
detail. Use the built-in image generation tool only; no paid API fallback. Inspect
sources and results, save exact prompts and source/master hashes, and preserve
previous assets and packs.

The latest user acceptance policy remains applicable: cosmetic concerns go on the
suspect list and do not automatically exclude usable textures. Concrete functional
problems such as broken UV alignment, masks, seams or animation coherence require
repair or an explicitly recorded original fallback. Do not repeatedly regenerate
small cosmetic differences. Do not count unsupported or omitted asset groups as
completed remasters.

Reconcile the queue with the later character work before resuming generation. The
overnight checkpoint's 128-approved count is historical; the live queue reports
245. The 1,670 static masters are not a complete count of every effect/animated
frame or procedural asset. Inventory those groups before estimating full effort.

Validate portable GPU formats as well as Android ASTC. Confirm desktop format
fallbacks, masked mip edges, residency and full-pack loading behavior; a pack that
fits the pilot residency allowance is not proof that the finished pack will.
Keep derived artwork outside source Git. Decide its distribution package and
review redistribution requirements before publication.

References: [texture pipeline](TEXTURE-PIPELINE.md),
[character remasters](CHARACTER-SELECTION.md).

## Client parity and desktop menus

Create a feature/setting matrix from current sources, with separate columns for
implementation, menu exposure, packaging and runtime evidence. Include campaign,
all multiplayer modes, discovery, hosting/bots, character/skin previews, new moves,
physics, interpolation/prediction, lighting, shadows, materials, textures, audio,
HUD editing, controls, first-run import and updates. Successful compilation alone
does not establish parity.

Share the desktop menu/settings/session implementation across Windows, Mac and
Linux. Isolate OS-specific file pickers, windowing, controller and process code.
Retain the existing Rune visual theme and saved preference identities.

Desktop acceptance includes:

- Direct mouse hover/click selection, wheel scrolling and draggable sliders.
- Keyboard-editable text and numeric fields, including normal clipboard behavior.
- Tab focus and predictable Escape/Back behavior; no navigation loops.
- Appropriate desktop layout, readable resize/DPI behavior and useful tooltips.
- No mandatory controller prompts, giant touch-only controls or carousel-only
  access where a desktop list/grid is easier to use.
- Retained controller support for users who choose it.

References: [menu organization](MENU-CLEANUP.md),
[last committed deployment checkpoint](CLIENT-SYNC-20261103.md).

## Linux validation without a physical Linux PC

Use a controlled Linux VM or WSL environment on existing hardware for real Linux
execution, with GitHub Actions for clean builds once configured. Do not assume
either local environment is already installed. Start with x86-64 as the desktop
and dedicated-server package target; ARM64 can follow separately.

Run server/network and configuration tests without graphics. Use a virtual display
and Mesa software Vulkan rendering for bounded client startup, UI and screenshot
checks when available. Test a Linux server against a Windows or Mac client.
Software-rendered or virtualized results do not certify native GPU performance,
driver behavior or every distribution. Publish that graphics-validation limit
until physical Linux GPU testing is available.

Keep proprietary Rune data off public CI. Public CI can run source-only and
synthetic checks; full game tests need a controlled local environment. Inspect
existing VM/WSL state and disk space before installing infrastructure. Do not
introduce paid runners or services as a silent fallback.

## Updates and release identity

Use one release version/source identity across clients and servers, with explicit
protocol/rules compatibility metadata and platform/architecture assets. Query
published GitHub Releases rather than treating an arbitrary source commit as an
installable release. Provide startup/background checks that do not block gameplay,
manual Check for updates, release notes, and clear offline/rate-limit handling.
Stable is the default channel; previews require an explicit channel choice.

Notification and a release/download link are the required baseline everywhere.
Download/install should be user-controlled and preserve game data, saves,
settings, optional texture packs and server configuration. Verify downloaded
artifacts; require authenticated release metadata before automatically executing
an update, not merely an untrusted checksum beside a download. Never install
during a match. Handle interruption and retain a recoverable previous version.

Desktop updates need a platform-specific installer/relaunch path; Linux packages
managed by a system package manager should defer to that manager. Android must
retain its existing application ID/signing identity and hand installation to the
system installer, including user confirmation when required. Do not depend on
root, silent privileged installation or embedded maintainer credentials.

While GitHub repositories remain private, unauthenticated public update checks
cannot retrieve their private releases. Treat an unpublished/private channel as
unavailable; do not make the repository public or ship an access token to solve it.
Update checks must remain independent of match discovery and the DHT.

Sources: [GitHub Releases API](https://docs.github.com/en/rest/releases/releases),
[Android installation API](https://developer.android.com/reference/android/content/pm/PackageInstaller).

## Dedicated server packaging

Reuse the current native headless authority and discovery transport; this is not
a second networking implementation. Package Windows x86-64 and Linux x86-64 with
their needed discovery runtime, libraries, notices and example configuration.
Users supply their own original game files.

Configuration must cover server name, visibility/listen address, mode, ordered
maps, capacity, starting bots, match rules and limits. Supply straightforward
foreground launchers first, with optional service instructions for unattended
hosting. Validate operation without a display/audio device, idle lobbies,
joining/rejoining, travel, player/bot changes and graceful termination. A server
must announce only once ready and disappear or expire after stopping.

Servers report newer-version availability but do not force-update an active
match. Update/restart remains under the operator's control.

References: [headless launcher](../../Tools/RuneMultiplayer/README.md),
[discovery runtime](../../Tools/RuneDiscovery/README.md).

## Work order and release gate

1. Reconcile the feature/texture inventory and freeze additional feature requests.
2. Share the desktop menu/session layer and complete Mac/Windows parity.
3. Establish Linux builds/runtime tests and package both dedicated servers.
4. Add common release identity, update notifications, then supported installers.
5. Revamp the touchscreen controls and validate real multi-touch interaction.
6. Complete the texture queue in resumable batches and validate the final packs.
7. Build all final artifacts from the same source, run bounded platform/interoperability
   checks, regenerate notices, and refresh the clean source mirror.

Texture generation can be scheduled independently once inventory is reconciled;
this ordering does not authorize paid processing or unrequested agents. It is a
substantial remaining workload, not part of a short packaging-only pass.

Final checks include first-run import in isolated profiles, existing-save load,
settings retention, desktop input/navigation, each mode on a representative map,
cross-platform host/join/reconnect, final-pack loading and updater failure paths.
Use native resolution with requested 4x AA for meaningful rendered checks; virtual
Linux rendering is functional evidence only. Preserve unrelated concurrent work.

This plan does not reopen perfect bot traversal of every secret, new sound
remastering, additional rendering features, iOS, or original Rune-server protocol
compatibility. Existing campaign/bot coverage limitations must remain visible in
release notes. All source and artifacts stay private until publication is
explicitly authorized.
