Open Rune native Windows adapter — 2026-10-29

Working checkout: C:\OpenRune\work\SurrealEngine
Game installation: C:\OpenRune\work\rune-classic
Executable: build-windows\SurrealEngine.exe (native Windows x64, Vulkan)

Build from PowerShell:
  .\Windows\Build.ps1
  .\Windows\Build.ps1 -NetworkTests
Requires Visual Studio C++ Build Tools, Windows SDK, CMake and Python.
The build script locates the installed MSVC/Ninja tools. It supports Ninja's
single-configuration layout as well as the existing CMake project.

Start a host and local player:
  .\Windows\Start-Multiplayer.ps1
Headless host:
Join an invitation:
  .\Windows\Start-Multiplayer.ps1 -Invitation C:\path\player2.rune-invite
Discoverable host (Node.js and npm ci in Tools/RuneDiscovery required):
  .\Windows\Start-Multiplayer.ps1 -Public
Join a public host identity:
  .\Windows\Start-Multiplayer.ps1 -JoinKey <64-character-host-key>

The default address is loopback for local play. For direct LAN invitations use
this PC's current LAN IPv4 address. Windows Firewall must allow the executable's
UDP traffic on the intended network. Validation used a temporary rule scoped
to the Mac's address, removed afterward; firewall protection was not disabled.
Each session uses a fresh folder below %LOCALAPPDATA%\OpenRune\sessions.
Invitations contain access credentials; send each guest only their own file.
Run the launcher in the logged-in desktop session to see the game window.
A plain SSH process is not an interactive Windows desktop application; use
SSH for builds/headless testing or an explicitly launched interactive task.
The Windows client now has the themed in-game menu, hosting, Find games and joining.
Launch C:\OpenRune\Open Rune.lnk. See Windows/README-menu.txt for current UI details.

Implementation:
- Shared production UdpSocket, Windows Winsock support, RAII socket cleanup,
  exclusive ports, bounded receive work, oversized/empty datagram rejection,
  transient route/ICMP errors isolated from other peers.
- Windows server, rendered client and automated client use the same secure
  session/replication implementation; the protocol was not changed.
- Invitation directories receive an owner/SYSTEM-only inheritable protected
  ACL. Exclusive file creation prevents replacing existing credentials.
- Windows atomic replacement keeps discovery status current.
- Fixed Windows SDK macro collisions with geometry identifiers and font APIs.
- Fixed single-config stripped-PDB output paths.
- Windows honors SURREALENGINE_CONFIG_DIR. Shutdown log saving creates its
  parent directory and cannot terminate the game on an I/O exception.

Validation and limitations:
Evidence: C:\OpenRune\outputs\windows-adapter-20261029
13/13 standalone networking tests passed, including the production UDP class.
Four authenticated players passed DM-Hildir (DM), DM-Team_ships (TDM),
AR-Champions (Arena) and HB-Agora (HeadBall), one map per mode. HeadBall's full
fixture includes pump/countdown, goal scoring and match restart. An initial
22-second HeadBall run was too short for its countdown and is retained as a
failed test, not counted as a pass.
Reconnect passed with 75 ms each-way latency, 15 ms jitter, 3% loss and 2%
duplication. Two rendered Windows clients passed shield bash/reaction at
50 ms each-way latency, native window pixels, 4x MSAA and a 120 FPS cap.
The 900-frame samples were 119.81 and 119.77 FPS; these are short fixture
measurements, not a general performance guarantee or a complete campaign test.
The original rendered harness hardcoded "macOS x2" into its result JSON;
that label is inaccurate for this Windows run. The harness now records the
actual OS. Original test records were preserved rather than silently edited.
Lifecycle regression checks seven changing discovery status timestamps,
private directory/file ACLs, isolated settings and rejection of an existing
invitation directory without altering its files.
Windows public Internet discovery/NAT traversal is not certified by these
local/LAN tests. Existing discovery unit tests passed during migration.

Source preservation:
Only the Windows checkout was modified. The Mac checkout and its uncommitted
work were not reset or replaced. Before-images, a patch and source hashes are
kept with validation evidence. No commits or pushes were made. Original game
assets and ordinary saves were preserved; tests used separate save folders.

LAN interoperability also passed: the existing Mac ARM client joined the Windows host with a Windows peer, received 279 world snapshots (277 active updates), and exited cleanly.
