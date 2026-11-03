# Menu cleanup — November 1, 2026

The user approved removing Enhancements and implementing the remaining menu
review recommendations. Existing setting keys/defaults and saved HUD/touch
layouts are retained. Protocol/ALPN 22, native rules 20 and snapshot 6 are
unchanged. No original game data or normal saves are changed.

## Organization

Android and Windows now share the following hierarchy, with platform-specific
controls shown only where supported:

| Category | Sections |
| --- | --- |
| Video | Display & performance; Camera & field of view; Lighting & shadows; Textures & surfaces; Effects & weather |
| Audio | Volume; Sound effects; Environmental & headphone audio |
| Controls | Look & keyboard; Controller; Vibration; Android touch controls |
| Interface | HUD & FPS display; Android HUD style/layout editor |
| Gameplay | Campaign difficulty/character; Combat moves; Enemy behavior |
| About & support | Diagnostics; About & licenses; Android developer tools |
| Multiplayer connection | Connection smoothing, reached from Multiplayer |

There is no Enhancements tab or shortcut. Each registered setting has one parent
section: 48 Android settings (including custom-rate backing values), 42 Windows
settings. Android has 18 sections including Connection; Windows has 17 because
touch controls are omitted. The menu preserves the dark stone/amber artwork,
uses a category sidebar, larger setting rows and a separate explanation area.
The crowded duplicate footer controls have been removed.

The title-screen saga tagline was removed on both platforms at the user's
request, and the menu choices moved upward into the vacated space. Pause/death
headings remain functional.

WIP texture artwork retains its WIP labels; ambient occlusion is marked
Experimental. Material maps and extra shadow lights explain their Remastered
lighting dependency. Native resolution explains that it bypasses upscaling.
Inactive choices remain saved, rather than being silently reset.

Reset this section operates on its displayed controls only, with confirmation.
It does not reset another section or HUD/touch layouts. Multiplayer combat
controls are marked host-controlled and cannot be changed or reset through a
client's settings page. New-campaign and host-match scopes are described.

Back returns to the actual previous page. Navigation retains the origin,
controller focus and relevant scroll position; campaign character browsing
returns to Campaign. Android Close remains an explicit return to the title/game;
there is no ambiguous settings Done button. Layout editors retain Save layout
and Cancel. Windows uses directional controller/keyboard navigation between
visible controls, with Tab for sequential traversal.

## Visual comparison

HUD preview follow-up: Android title-menu HUD editing now uses the existing
themed artwork rather than exposing the paused startup-world camera beside a
torch. Editing during an actual game retains the current scene. The editor
background is temporary, cleared on Save/Cancel, and counted as covering the 3D
view only while the artwork is visible. Actual title/gameplay editor entry,
Hide/Show panels, Cancel/parent return and unchanged preferences were checked on
the OPPO. No actor positions, gameplay camera or original saves are modified.
Evidence: `outputs/hud-preview-fix-20261101`.

Video → Effects & weather provides Compare original visuals / Restore my visuals.
Comparison is temporary and never saves replacement values. Audio, resolution,
frame target, combat settings and layouts are retained. The Android comparison
now preserves environmental/headphone audio bits; Windows has a matching
temporary renderer/effects comparison and a visible restore action.

## Multiplayer

Find games, Host game, Character & skin and conditional Rejoin are the normal
entry points. Invitation files and shared host identities live under Advanced
joining. Connection smoothing is reached from Multiplayer; detailed network
counters are under Support → Diagnostics.

Hosting shows name, visibility, mode, player capacity, starting bots and map
rotation first. Match rules contains teams/arena size, score/time limits,
HeadBall scoring, private LAN address and normal-combat leap/rush. The latter is
now an explicit match configuration value on both platforms rather than an
implicit read of a campaign preference at launch. Its initial value still comes
from the existing saved preference; editing match rules does not overwrite it.

Map selection separates selected order from the installed catalog and provides
Move earlier/later and Remove controls. The host retains the existing supported
map filtering and 32-map rotation limit. In-match menus focus on resume/respawn,
current player/team information, settings, invitations, bot management and
leaving/stopping the match. All session-ending actions retain confirmation.

## Validation and evidence

Evidence and pre-change source/preferences are outside Git at
`outputs/menu-cleanup-20261101` relative to the Rune project root. Tests use
isolated saves/configuration and restore Android preferences after changes.

- `MenuSettingsTest` checks existing migration/persistence behavior.
- `MenuNavigationTest` verifies every Android setting has one parent and section
  resets preserve unrelated preferences.
- `menu_structure.cpp` verifies complete, unique Windows setting coverage.
- Rendered Windows checks traverse each section, reset scope, comparison,
  campaign-character return, advanced joining/connection return, host-rule scope
  and map reordering.
- A separate rendered Windows hosting check starts/joins a private match with a
  bot, adds/removes a bot through the menu and stops the host.
- Android validation covers the actual touch pages and their layout, comparisons,
  character/editor return paths and multiplayer setup. See the final checkpoint
  for actual completed checks and hashes, rather than assuming device coverage
  from a compiled Java model.

Older UI scripts that name Enhancements or the former flat pages describe the
historical layout. Use the current menu-cleanup navigation checks for this
hierarchy; do not infer a gameplay/renderer regression from a missing old label.
This cleanup does not establish new FPS, campaign, all-map or NAT coverage.

Both updated clients are deployed. The normal Windows executable and saved
server-browser launcher use the new menus; the OPPO APK is installed and its
hash matches the local package. Tests restore the tablet's preferences; normal
save hashes match. A character selection changed after the tests while the user
was navigating the menus, so the latest choice is retained rather than restoring
the older pre-update preference snapshot. The tablet is returned to its
normal title menu and no Windows match remains. Broader navigation checks
preceded the final title/focus-only changes; the exact final builds additionally
pass focused title, Back/focus and touch rotation-order checks. Windows hosting
was exercised with a real private match/bot; Android hosting was checked through
the actual setup pages without starting another match. A user-reported Find
games/Multiplayer Back loop was traced to explicit Back handlers opening a new
parent page. All those handlers now use the history return path, and ancestor
navigation removes the old child instead of creating a cycle. A focused device
regression covers both on-screen and system Back through to the title. No commits
or pushes.
