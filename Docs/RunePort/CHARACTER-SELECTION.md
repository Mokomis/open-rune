# Original character selection

October 29, 2026. Multiplayer first, with a separate opt-in campaign selection.

## Player controls

Android: **Multiplayer → Character & skin**. The themed horizontal carousel shows
approximately four cards at once. Swipe left/right, use the previous/next buttons,
or move focus with a controller. Each card shows the character name and a live
animated preview using the original model, textures and idle animation. Selecting
the card opens a larger animated preview; the separate **Select** button below it
chooses the character directly. Cards with alternate skins show a skin-count badge.
The expanded view previews each skin and commits only when **Select this character**
is pressed; Back or close preserves the previous choice. Karl offers Default and
Jarl skins. The edition labels have been removed.

**Settings → Gameplay → Campaign character & skin** uses the same roster, with an
**Original story character** choice. This is independent of the multiplayer
selection and applies to new campaigns. Existing saves retain their character.
An overridden campaign carries the chosen character and skin through saves and
level transitions. Original story mode keeps the game's authored Ragnar variants.
Unusual characters still use the original campaign dialogue and scripted story;
this feature does not rewrite cutscenes or certify every campaign interaction.

The Mac client accepts `--character=ID` (or a full allowlisted class name),
`--character-skin=0|1`, and `--campaign-character=ID`. It has no new desktop
graphical carousel. The Android menu is compiled on the Mac. The animated carousel has been installed
and touch-tested on the OPPO; physical controller testing of the new cards remains
pending.

## Installed roster

There are **36 character entries / 37 character-and-skin combinations**, including
the Halls of Valhalla and bonus entries supplied by the installed Rune Classic
content. This is not a claim that the original 2000 launch included all 36.
The shared allowlist is `Network/include/openrune/CharacterRoster.h`.

| IDs | Characters |
|---|---|
| 0–4 | Leather Ragnar, Town Ragnar, Snow Ragnar, Trial Pit Ragnar, Shipwreck Ragnar |
| 5–10 | Zombie, Zombie variant, Alric, Berserker, Conrack, Dark Viking |
| 11–18 | Karl (Default/Jarl), Loki Guard, Sigurd, Sven, Ulf, Wolfgar, Elder, Dark Warrior |
| 19–25 | Valkyrie, Sark Spawn, Sark Sword, Sark Axe, Sark Hammer, Sark Conrack, Sark Ragnar |
| 26–32 | Gold Valkyrie, Snake Berserker, Dwarf, Swashbuckler, Mongol, Syrian, Wren Valkyrie |
| 33–35 | Remus, Romulus, Jun |

No proprietary models, skins or extracted scripts are added to the repository or
APK. Players still supply the matching original game content.

## Implementation

- A bounded character/skin ID is sent on the authenticated ordered control lane
  before the match contract. Clients cannot request arbitrary script classes.
- The authority spawns the original selected class. The normal game scripts
  still control team colors, equipment, death and respawn. The selected profile
  survives reconnects and map travel.
- Expansion characters can use `SubstituteMesh`: replacement body geometry and
  textures driven by the original Ragnar skeleton. The renderer now respects
  that geometry, and appearance replication carries it to corpses as well.
  Animation interpolation, weapon attachments and combat keep the shared skeleton.
- Kick, shield bash, dual wield/cross block, bloodlust leap and shield charge use
  the existing combat implementations. No character-specific damage bonuses were
  introduced by the selector.
- Saved campaign metadata carries only the two character options. A pre-existing
  URL parser bug that dropped the final option was fixed, preserving Jarl skin
  through save/load and travel.
- Simultaneous joins now refresh bootstrap worlds after reserved pawns are
  replaced; pending handshakes are not retired when the first players start.

This changes **protocol/ALPN, discovery topic and gameplay rules to 20**. Update
both endpoints together. Snapshot encoding and invitation format are unchanged.
Old running hosts and previous APKs are intentionally left intact.

## Validation and remaining review

Local evidence is under `outputs/characters-20261029` outside Git. Tests use
isolated saves, configuration and private invitations. The tablet is untouched.

- Headless original-script checks: all 37 combinations execute kick, bash, dual
  wield/guard and bloodlust movement. Eight representative character/skin cases
  additionally pass save/load and campaign stage transition checks.
- The protocol suite passes all 12 tests, including strict profile decoding,
  appearance round trips and profile retention across travel. Discovery passes
  its four local DHT/validation tests.
- `character_roster.py`: all 37 combinations pass authenticated joining, forced
  death/respawn and same-map travel (10 batches). Team Deathmatch reconnect and
  repeated rounds pass with Karl/Jarl and Jun. Arena passes with four expansion
  characters. HeadBall passes team readiness, head scoring and map travel with
  Mongol, Wren, Romulus and Karl/Jarl. One representative map per mode was used.
- Rendered dual wield/guard passes with Gold Valkyrie and Dwarf; bloodlust moves
  pass with Jun and Sark Ragnar; bash and target stagger pass with Karl/Jarl and
  Remus. The test-only bash aiming was corrected after its initial miss; normal
  player aiming and combat ranges are unchanged.
- Rendered native-combat fixtures use native window pixels, scale 1 and 4× AA.
  Two simultaneous windows share the Mac; their FPS is not a single-client or
  Android performance claim. These tests are not full campaign playthroughs.

Both Mac and Android ARM64 builds pass. The signed APK, notices and LGPL sources
are saved in `outputs/characters-20261029/android`; signature and notice hashes
were verified during packaging. The existing optional Turnip driver was retained.
No tablet installation, public host restart, commit or push was performed.
`checkpoint.json` records exact binary/package hashes and individual results.

Visual review follow-up: Gold Valkyrie's low-polygon skirt/hip pose looks angular
in a dual-attack capture. Idle and guard look coherent and the replacement joint
names/order match; no speculative skeleton adjustment was applied. Keep this
capture for review in motion rather than blocking the rest of the roster.

Representative commands (run in the engine checkout; output paths must be new):

```sh
python3 Tools/RuneValidation/character_roster.py \
  --binary build/SurrealEngine.app/Contents/MacOS/SurrealEngine \
  --game ../rune-classic --output /absolute/path/roster-check

python3 Tools/RuneValidation/network_native_combat.py \
  --binary build/SurrealEngine.app/Contents/MacOS/SurrealEngine \
  --game ../rune-classic --fixture dual --characters 26:0 28:0 \
  --output /absolute/path/rendered-check
```


## Animated-card follow-up — October 29

`RuneCharacterPreview` renders bounded 192×224 transparent thumbnails from class
defaults and sampled idle poses, including substitute bodies and Karl's Jarl skin.
It does not spawn actors, modify live poses, execute preview gameplay scripts or
write derived artwork into the game packages. UI requests and copied pixels cross
a mutex; all package/model access remains on the engine thread. A gentle turn
helps show depth. The same cards serve multiplayer and campaign selection.

Only visible cards request frames; at most five are rendered per update. Requests
expire after 250 ms offscreen. The approximately 20 Hz preview refresh runs inside
the covered-menu wait, preserving the existing 10 Hz hidden-world cadence. Leaving
the page detaches callbacks and releases Java preview buffers. Missing previews
show an explicit unavailable label without blocking selection.

All 37 choices produced nonempty, changing images in the Mac probe; the contact
sheet was visually checked. The installed Android build passes live four-card
animation, next/previous scrolling, touch swipes and Default/Jarl preview changes.
The pre-test Leather Ragnar selection was restored. No new protocol change.
Evidence, screenshots and signed APK: `outputs/character-previews-20261029`.

Reproduce the isolated Mac asset check using `--rune-tactics-probe=previews`,
`--preview-output=/absolute/new/directory` and an isolated `--save-directory`.
Do not commit the generated frames; they are derived from the user's game art.


## Expanded character preview — October 29

Both Android browsers now use the enlarged preview and explicit quick-select
controls described above. Expanded previews render at 384×448, twice the card
resolution in each dimension. Background card requests pause while the dialog is
open, and expired native pixel buffers are released. Previewing a skin does not
change the saved character or any live pawn.

Mac and ARM64 builds and signed APK packaging pass. The Mac asset probe renders
all 37 combinations twice plus four larger preview cases. On the installed OPPO
build, touch checks pass for quick-select, opening/cancelling without changing the
selection, Karl's skin badge, Jarl preview without commitment, and committing
Karl/Jarl from the expanded view. Two captured larger frames confirm animation.
The user's pre-test Zombie selection was restored. No preview failures or fatal
Java exceptions appeared in the captured recent device log. Physical controller
interaction remains unverified. No gameplay or protocol changes in this follow-up.
Evidence and the signed APK: `outputs/character-details-20261029`.


## Touch rotation follow-up — October 29

The enlarged preview and its surrounding panel no longer react with a gold
pressed/focus fill. Selection and skin buttons retain normal feedback. Touchscreen
horizontal drags rotate the model through 720 degrees per preview width; flicks
coast with bounded angular speed and decay, and touching again stops the spin.
Pausing before release suppresses coasting. Idle animation continues independently.
Rotation is local to the expanded view, keeps its angle across skin previews, and
resets when reopened. Mouse/controller events do not rotate it.

Expanded previews request roughly 30 Hz updates; small cards retain their existing
cadence and pause while the modal is open. The hidden world's 10 Hz cadence is
unchanged. The Mac probe passes all 37 appearances at four explicit yaw angles.
Mac/ARM64 builds and signed APK packaging pass. Installed OPPO checks verify
touch drag to a rear view, flick/coast, touch stop and the absence of the yellow
fill while holding the preview. The selected Zombie II profile remains unchanged.
Evidence and signed build: `outputs/character-rotation-20261029`.


## Preview responsiveness follow-up — October 29

Expanded preview callbacks now follow Android display frames instead of a 33 ms
timer. Drag events submit their angle immediately through a bounded JNI request;
they do not wait for the next image poll or copy a bitmap on every touch event.
Native rendering permits an 8 ms expanded / 16 ms card interval and wakes more
frequently only while preview requests are active. Expanded requests take priority
over stale small-card requests. Generation counters skip duplicate image uploads.
The covered world remains at 10 Hz. Preview idle animations run at 1.25× speed;
gameplay animation speed and physical controller behavior are unchanged.

Installed OPPO measurements from native preview counters: about 96 generated
frames/s for the enlarged Zombie II preview (5.1–5.8 ms rendering per frame), and
52–54 card-batch updates/s (9.7–10.1 ms per batch). These measure preview generation,
not gameplay FPS or proof that every frame reached the display. Touch rotation and
the expanded view remain working. All 37 appearances pass four-angle Mac checks.
The first isolated probe completed its image tests but failed saving its log due
to a missing config directory; after creating that directory the probe exited
cleanly. Mac/ARM64 builds and signed packaging pass. Evidence and installed APK:
`outputs/character-preview-response-20261029`.


## Full roster moves, interpolation and remasters — October 29

All 37 selectable character/skin combinations pass the original-script roster
probe: kick and recovery, shield bash and recovery, dual equip/guard/four attacks,
and bloodlust leap/charge/recovery. This exercises move execution per appearance;
it does not exhaustively test hits against every possible opponent pairing.
Every combination also passes fractional skeletal interpolation across all
multiframe sequences, loop seams, non-loop clamping, animation transitions and
interrupted transitions, plus rendered skin/attachment transform agreement.
All 37 pass authenticated multiplayer join, death, respawn and map travel.

Rendered Mac two-client fixtures additionally pass dual wield (Sark Conrack/Jun),
bloodlust (Valkyrie/Dwarf), and shield bash (Jarl/Wren). A final Wren/Jun dual-wield
fixture passes with the merged WIP pack, native render scale 1.0 and 4× AA.
Both clients explicitly load their replacement body/head/limb atlases as ASTC;
no pack fallback appears. The direct Mac multiplayer entry point now honors
`--texture-pack`, which previously only took effect in the campaign settings loop.
Embedded clients retain their UI selection. Mac and Android ARM64 targets compile.
These are automated checks, not exhaustive human playtesting or a sustained
Android FPS measurement. The tablet was not used or updated for this task.

The roster now has 120 approved unique remastered atlas masters, including four
previous masters, covering 212 logical character texture identities. Original
colors are calibrated, UV layout and masks retained, and both 2×/4× sizes have
complete mip chains and conservative material maps. All 37 models were rendered
at four angles, with stock/WIP front/back comparisons visually reviewed. Eleven
appearances have cosmetic notes retained for later review; none showed a concrete
mapping defect requiring exclusion. All remain explicitly WIP.

The offline preview probe accepts `--preview-texture-review=<directory>` for
bounded P6 PPM review textures named by full texture identity. This is a validation
path: normal character-browser cards still use the original artwork. Gameplay
uses the replacement pack through the existing WIP texture setting.

Local evidence: `outputs/character-complete-20261029` (relative to project root),
including `roster.log`, `network-roster/result.json`, the native fixture results,
`model-review.json`, `suspect-models.json`, and stock/WIP comparison images.
Derived artwork and the pack remain outside Git. See the texture-pipeline document
for pack verification and location.


### October 29 tablet installation follow-up

The user subsequently requested installation. Updated same-signature APK and the
complete merged character pack are now installed on OPD2515 `99cc517a`. All 2,857
deployed index/payload files pass on-device SHA-256 verification. The former pack
is retained at `files/game/TexturePacks/OpenRune-before-characters-20261029`.
A bounded 240-frame DM-Hildir launch completed at native 2520×1680 with 4× AA,
loading Ragnar WIP 2× color and material maps. This is a launch/upload check, not
full roster device validation or sustained FPS evidence. Saves and shared
preferences match their pre-install hashes. Returned to normal title launch with
saved settings, including WIP 2×. Evidence: `outputs/character-install-20261029`.
