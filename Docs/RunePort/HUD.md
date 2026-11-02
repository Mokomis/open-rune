# Open Rune — HUD and layout editor

October 25, 2026. Android implementation of the approved compact HUD concepts.

## Using it

Open **Menu → Edit HUD**, or **Settings → Gameplay → Edit HUD layout**.
The world pauses. Health, Shield, Rune power, Bloodlust, Air, Messages, FPS and
Score/time each have their own position and size. Touch an element and drag it;
drag its gold bottom-right corner to resize. The inspector also has a size slider
and −/+ buttons (50–200%). Each of the five meters can be vertical or horizontal.
The presets arrange the meters in the approved compact vertical or horizontal
layout. Hidden/conditional indicators appear as previews in the editor.

For a controller, move the cursor with the left stick or D-pad. Hold L3 or A over
an element/corner, move the stick, then release to place/resize it. A also clicks
editor buttons. LB/RB selects the previous/next element. Start saves; B cancels.
**Hide panels** exposes widgets beneath the editor; **Show editor** restores it.

**Save layout** commits the draft. **Cancel/Back** discards it. Reset element and
presets affect the draft only until saved. Layouts survive app restarts. Anchors
use the space remaining around each widget, keeping widgets within the screen
when their size/orientation or the screen aspect ratio changes.

**HUD style: Open Rune / Original** appears in Gameplay and Enhancements, sharing the
same setting. Original restores the game's original meter/message drawing.
**Video → Show FPS** controls the independently movable FPS widget. Existing
movement/combat controls retain their positions and behavior.

## Integration

`RuneHudLayout.java` owns the versioned `hud.*` preferences. `RuneHudView.java`
draws original vector ornaments/icons and the editor at screen resolution; no
mod code, mod artwork or original game textures were copied into these widgets.
The drawing uses the same charcoal, ivory and gold family as `RuneMenuView`.

`RuneHud.h` publishes a mutex-protected copy from the engine render thread.
The Android UI never dereferences game actors. Meter values come from the actual
player and equipped shield; shield maximum comes from that shield class's default
health. Rune's original HUD draw calls control visibility, with Air additionally
requiring a submerged head. Numbers update at up to 30 Hz; the game continues at
its selected presentation target. Unchanged UI does not continuously redraw.

Only this viewport's five meter draw methods are replaced. The original HUD
Tick, player PostRender, fades, cinematic state, scoreboard and script behavior
continue. Text drawn by DrawMessages is routed to the movable Messages widget;
DrawFragCount/DrawRemainingTime feed Score/time. Original world-positioned
RuneMessage text and cinematic-specific art remain in the game renderer so that
world annotations retain their meaning. Save/load notices use the Messages anchor.
The desktop renderer retains its original HUD.

## Validation

Host model test: `HudLayoutTest.java`, compiled with `RuneHudLayout.java` against
the installed Android SDK jar. Checks draft/cancel isolation, persistence,
independent transforms, resizing/aspect bounds, corrupt preference recovery and
presets. Android ARM64 and desktop builds pass.

Live OPPO checks: touch move, corner resize, size buttons, independent orientation,
presets, Cancel, restart persistence and panel hiding. Injected Android gamepad L3
and real joystick-source axis events exercised the Activity input path; this is
not a physical-controller compatibility certification. Actual Throw/Use/food and
native4x presentation results are recorded with the final evidence below.

Tests use isolated validation saves; normal and campaign-village-ledge saves are
hash-compared. Same package/signing key and existing Rune-only refresh rule.

### Installed result

Actual screenshot evidence is in [evidence/hud](evidence/hud). The final installed
APK hash is in `installed-build.json`; it matches the device package. Three menu/
editor pause cycles preserved world time and player position exactly. Original/
Rune switching and reopening the editor work. The copied campaign quick save
loaded successfully via the actual Load menu. All ten original normal/campaign
save files and existing preferences were unchanged; the Rune refresh rule stayed
unchanged. The real Throw/Use test recovered the axe and healed health50→65.

Native2520×1680,4×MSAA presentation samples (about14seconds each):

| Scene | Displayed FPS | p95 frame interval |
| --- | ---: | ---: |
| Active synthetic combat | 119.40 | 8.33ms |
| SinkingShip2 underwater opening | 106.18 | 16.66ms |

Existing surface-detail settings were retained; enhanced bloom was disabled in
these measurements. These bounded samples are not a sustained120FPS guarantee
or an isolated HUD performance comparison. Hardware compositor still reports
120Hz. Air was observed at64/90 underwater and hidden again in the dry campaign
preview. FPS labels continue to report engine FPS, separately from presentation.

`Tools/RuneValidation/android_hud.py` reproduces the main touch/editor checks and
restores preferences in a finally block. Joystick-source axis injection with L3
held also moved Health successfully; physical controller hardware was not used.

The final tablet launch returns to the title menu with the original
`campaign-village-ledge` save namespace. HUD changes are available through
Menu→Edit HUD; testing used separate `hud-ui`/`hud-preview` copies.
