Windows menus and Internet discovery
===================================

Launch C:\OpenRune\Open Rune.lnk, or run Windows\Start-OpenRune.ps1.
The normal Windows game also enables the desktop menu automatically. Diagnostic
runs retain their existing behavior; --no-desktop-menu explicitly opts out.
Escape opens/closes the menu. Mouse, arrows/Tab, Enter and Escape navigate it.
A connected XInput controller uses D-pad/left stick, A, B and Start.

The Windows menu uses the Android menu artwork and Open Rune mark, clean Noto
fonts, a shared settings model, animated character cards and enlarged rotatable
previews. Karl/Jarl skin selection and campaign character overrides use the same
native character implementation. Multiplayer character changes apply next join.
Video, Audio, Controls, Gameplay and Enhancements share the same saved values;
features repeated under Enhancements are not independent copies.

Scaling: Native, 75%, 50%, 25%. Below native, the existing portable GSR shader
runs on the Windows GPU. Android compositor/hardware scaling is not offered.
The menu remains native resolution and displays scene and output dimensions.
Windows has a real frame limit (30-1000 FPS), separate from VSync/frame pacing.
Display refresh is controlled by Windows; Android refresh controls are omitted.
Native resolution, 4x MSAA and a 120 FPS target are the Windows defaults.
Touch-control/HUD placement editors remain Android features. Windows has mouse
look, XInput controls and optional XInput rumble; no Android rumble API is used.

Multiplayer
-----------
Host game supports names, four modes, player/team limits, time/score limits,
compatible map rotation, HeadBall limb scoring, and Internet/private visibility.
Internet hosts publish through HyperDHT; no Open Rune master server is needed.
Find games shows live, authenticated, compatible replies with player counts.
Select a result to join. Alternatively paste a public host identity or the path
to your own .rune-invite file. Private invitations need a reachable LAN address.
Do not share one invitation between players, or publish invitation contents.

The host is a separate, app-owned native server process. Closing Open Rune or
choosing Stop hosting ends it and its discovery helper. Leaving your own match
can keep the host running; Stop hosting explicitly disconnects everyone.
Rejoin preserves the discovery peer identity, allowing the host's reserved
45-second slot to be reused instead of incorrectly reporting a full match.
Host loss returns to the multiplayer menu with an error and rejoin options.

Build and runtime
-----------------
Windows\Build.ps1 builds the game and, when discovery dependencies are installed,
stages discovery and packages notices plus matching LGPL library sources.
Install Node.js, run npm ci in Tools\RuneDiscovery, then build. The pinned lockfile
is authoritative. Windows\Prepare-Discovery.ps1 can restage the helper files.
Node.js is currently a separately installed runtime, not bundled in the game.
No API key, paid service or public-directory hosting account is required.

Menu preferences live in %LOCALAPPDATA%\SurrealEngine\DesktopMenu.json (or the
existing SURREALENGINE_CONFIG_DIR override). App-owned session data lives under
OpenRuneSessions in that same private configuration directory. Invitations never
belong in Git. Original Rune assets are still supplied separately by the owner.

Validation and boundaries (2026-10-29)
-------------------------------------
Evidence: C:\OpenRune\outputs\windows-menu-discovery-20261029.
- Windows UI-created public host accepted a Mac bot through HyperDHT and carried
  live game updates; Windows Find games discovered and joined a Mac public host.
- Immediate leave/rejoin and host shutdown recovery exercised in the actual menu.
- Four discovery protocol/integration tests passed on Windows.
- Native Windows mouse-message dispatch and the real engine input handlers tested.
- Settings, native-resolution menus at 75/50/25% game scale, character/skin cards,
  save/load using copied isolated saves, and process ownership checked.
These machines share one router. This is not a new separate-NAT/hotspot test,
and it does not guarantee traversal of every firewall or NAT configuration.
Physical Windows controller feel and rumble still require hands-on verification.
No new all-map playthrough or sustained GPU performance claim is made here.

The opt-in --desktop-menu-probe=<directory> requires --save-directory and exposes
bounded menu actions/state/captures only for automated validation. Normal runs do
not read these files. Isolated .rsa save fixtures require their .mapname sidecar;
copying just a legacy save file is not a complete test fixture.

October 29 Windows host timing follow-up
---------------------------------------
The network pump now uses a reusable high-resolution waitable timer on Windows.
Ordinary 1-2 ms sleeps measured about 15.6 ms and limited complete world updates
to about 21.3/sec. Four-player combat tests improved to 30/sec; max active gaps
fell from 80 ms to 37-39 ms. Four HyperDHT clients then measured 30.05-30.09/sec
with 38 ms maximum active gaps. No global timer setting or protocol change.
Evidence: C:/OpenRune/outputs/windows-live-playtest-20261029/RESULTS.txt.
The separate TimingAfter executable is validated; live-match deployment is
recorded in that evidence file. Do not infer tablet human-playtest completion.
Optional --rune-net-pose-trace=<CSV> records headless bot snapshot/pose timing.
October 29 combat presentation and audio follow-up
-------------------------------------------------
Corpse snapshot positions now compensate inherited PrePivot against the replica
class-default pivot. This removes the reproduced 38-unit hover followed by an
apparent drop without changing server physics. The 239-tick origin regression
passes (max error 0.0282 units); the user confirmed the live fix works.

Sound-bearing feedback now uses bounded 16-ms scheduling instead of waiting for
the idle 50-ms feedback cadence. Existing ordered two-offer redundancy, queue
limits, expiry and protocol remain. Authenticated synthetic delivery tests show
worst isolated-cue delay 50 -> 18 ms; a 32-cue burst now delivers all cues instead
of expiring 20. This is queue/transport latency, not end-to-end audio latency.
13/13 network tests, native combat and a four-player impaired 25-second stress
pass. Audio remains best-effort; human audio review is pending. The updated live
Windows host has three bots. No tablet update or render-setting change required.
Evidence: C:/OpenRune/outputs/windows-combat-tightening-20261029/RESULTS.txt.