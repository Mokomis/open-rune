# First-run game-file setup

Open Rune's normal desktop launch and Android launcher now offer **Use an existing Rune installation** and **Buy Rune Classic on Steam** when game files are missing. Setup runs before the engine loads a map and uses bundled Open Rune artwork/branding. The purchase button opens the official store page, <https://store.steampowered.com/app/210950/Rune_Classic/>. It does not buy, download or import the game automatically.

On Windows and Mac, select the complete installation folder (or its System subfolder). On Android, first transfer the complete Rune Classic folder to accessible device storage, then select its root through Android's system folder picker. The app does not request broad storage access or try to read another app's private files. Steam's Rune Classic product supplies Windows game data; buying it on a Mac or Android device still requires obtaining and transferring those files.

## Imported files and preservation

The importer copies package data from System, Maps, Textures, Meshes, Sounds and Music. Default.ini and DefUser.ini seed fresh settings; imported default package/save/cache paths point inside the separate copy. Rune.exe is retained only to recognize the original version; Open Rune never runs it. Supported executable identities match the existing engine database: Rune Gold 1.07 and Rune Classic 1.10/1.11. Core/Engine/RuneI/RMenu package headers, the DM-Hildir bootstrap map and resource-folder contents are checked. This is a startup completeness check, not validation of every map/dependency in the installation.

Original saves, User.ini, Rune.ini, SE settings, native DLLs, caches and optional texture packs are excluded. Nothing is written to the selected original installation. Existing Open Rune data is never overwritten by this importer. If a pre-existing private game folder is incomplete, it is left in place and the screen explains that it needs repair; automatic repair/replacement is outside this change.

Desktop imports live under `Directory::localAppData()/OpenRune/game` (currently `%LOCALAPPDATA%/OpenRune/game` on Windows and `~/.config/OpenRune/game` on Mac). Android uses the existing app-private `files/game` location, preserving the installed client's data layout, settings and saves.

Copying happens on a worker thread with progress. Files are staged beside the final folder, validated, then renamed into place. A failed import cleans up its own stage. A killed process can leave an inactive, uniquely named staging folder; a later attempt does not overwrite or activate it. Canceling the folder picker changes nothing. The Steam link leaves setup available for returning after installation.

Successful imports are recognized on later launches; there is no separate completion flag that can be set before copying finishes. Existing Android previews with valid files skip setup. Desktop developer launches with an explicit game path or `--direct` retain their existing behavior. `--legacy-launcher` still opens the original multi-game developer launcher. Normal desktop launches use the verified managed copy rather than writing into a discovered Steam installation.

## Validation, November 1

Evidence: `outputs/first-run-20261101` outside the source checkout.

- Portable C++ import checks: valid/invalid packages, root/System selection, excluded files, fresh configuration paths, progress, preservation of existing installs/saves and rollback after a failed copy.
- Real owned-game import; before/after original executable, configuration and save hashes match.
- Host-JVM Android checks: real executable version and package checks, missing/incomplete folders, safe file names, import filters and configuration preparation.
- Mac: fresh isolated startup opens setup before engine initialization. Normal startup recognizes the imported managed copy; DM-Hildir renders at native scale and 4× MSAA using isolated saves/configuration.
- Native Mac, native Windows and signed ARM64 Android builds pass. The new Android launcher activity is included in the APK; the existing signing key and optional app-local Turnip payload are retained.

No tablet installation or device-picker test has been performed for this change. Computer-use permission was unavailable for a visual/click-through review of the Mac folder picker. Do not equate file-level tests and successful compilation with complete picker, controller or accessibility testing. No multiplayer host was started and no release/publication was performed.

Relevant source: `SurrealEngine/RuneGameFiles.h`, `SurrealEngine/UI/Launcher/RuneSetupWindow.cpp`, `Android/RuneGameFiles.java`, `Android/RuneSetupActivity.java`. Fixtures: `Tools/RuneValidation/first_run_files.cpp`, `Tools/RuneValidation/FirstRunFilesTest.java`.
