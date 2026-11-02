# Host-controlled bots (Windows, October 30)

Host setup includes Starting bots (0 through the player limit minus the host).
During a hosted match, open Multiplayer for Add bot and Remove a bot. Bots use
the normal tactical client, including random installed character/skin selection.
The player limit includes humans and bots. Removing a bot may leave its slot
reserved briefly by normal connection recovery. Stopping hosting ends all owned
bot clients and discovery runtimes. Joining another person's server does not grant
bot controls.

Public bots use authenticated discovery admission and slot leases. Private bots
use unoccupied invitation slots, starting at the highest slot. Separate process
jobs, configuration directories and saves contain their lifetime and files.
Windows discovery response reads now allow atomic replacement, fixing a reproduced
Node EPERM startup failure when a menu reader held the old state file open.

Validation: real menu-driven private DM-Hildir Deathmatch and public DM-Hildir
Team Deathmatch. Each started two bots, added a third, rejected adding beyond the
four-player limit, removed a bot, detected a deliberately terminated bot, and
stopped all owned processes. Final build and tests pass; this is not an Android
UI test, FPS benchmark, all-map test or new combat-policy validation.

The final Host a match screen is open on Windows, with no match running.
Android bot-hosting UI is now built and installed; see the Android follow-up below. Laptop-only muted blood is preserved
and was not transferred to Windows by this change. No commits or pushes.

Evidence: outputs/host-bots-20261030/checkpoint.json, validation.json,
build-final.log, changes.patch, source-hashes.json, host-menu.png.

## Android follow-up

The Android host setup now offers Starting bots. Multiplayer offers Add bot and
Remove bot while this device hosts, both inside and outside its own player session.
Counts include pending connections; the host always retains player slot 1. Bots
use separate bound Android service processes because the engine owns process
globals. The foreground host owns all bindings and discovery worklets and releases
them when hosting ends. Process components are reused only after the previous
process has exited. Public bots use normal discovery admission/leases, while
private bots choose free invitation slots. They use the existing tactical AI and
random installed character/skin selection, without stress-fixture flags.

ARM64 and Java/Dex builds, signing, and in-place installation passed October 30.
User instructed: "Unlocked. Install but do not test." The game was not launched
and no tablet hosting/gameplay checks were run. Preferences remained unchanged;
saves/game data were retained by the in-place update. The APK includes the prior
AI overhaul and muted blood. Windows was not contacted during this follow-up.

Evidence: outputs/android-host-bots-20261030/checkpoint.json, native-build.log,
package.log, install.log, source-hashes.json and changes.patch. Runtime hosting,
service lifetime and public bot admission on Android still need device testing.
