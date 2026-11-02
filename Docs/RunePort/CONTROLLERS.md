# Open Rune — Android physical controllers

Android gamepad events now have an explicit gameplay route in `RuneController`.
Previously SDL opened controllers at startup and translated a few buttons into
menu-style keys, but its window backend did not handle controller axes or hotplug.
An injected R3 key test was not proof of working physical-controller gameplay.

The OPPO detects the USB Razer Kishi V3 Pro. On the tested configuration it exposes
both a physical interface and a `Razer Kishi V3 Pro (rumble)` interface. A user-led
hardware recording produced 4,725 events on the latter and none on the former.
The app therefore chooses the interface that sends meaningful input, rather than
binding to the first enumerated device. Existing device/rumble services are not
modified. This change does not add game-generated rumble feedback.

## Mapping

| Control | Gameplay |
| --- | --- |
| Left stick | Move |
| Right stick | Look; shares Controls sensitivity/inversion |
| A | Jump / swim up |
| B | Dive / crouch |
| X or RT | Attack |
| Y or left-stick click (L3) | Use / pick up / eat |
| LB or LT | Block; double tap for shield bash |
| RB | Throw |
| Right-stick click | Kick |
| Select / View | Activate the equipped weapon's Rune Power ability, when charged |
| D-pad left / up / right / down | Weapon slots 1 / 2 / 3 / 4 |
| Start | Pause / resume |

In menus, the left stick/D-pad navigates, A selects and B returns. The existing
HUD editor keeps its own stick/click-drag controls. Controls shows the connected
controller, updating on reconnect. While any Android gamepad/joystick is connected,
touch movement, action buttons and the touch-look area are hidden. Disconnecting
the last controller restores them if the saved Touch controls setting is On.
The compact iron-and-gold Menu button always remains available during gameplay;
health and other HUD elements are unaffected. Device capability flags drive this
behavior, including virtual gamepad bridges; no controller brand is hardcoded.
Hiding controls releases touch-owned keys without resetting physical input.

Stick dead zones honor Android's reported flat range with a 0.16 minimum. This
removes the measured Kishi right-stick neutral drift (approximately -0.10, 0.03),
then rescales the remaining travel. Trigger hysteresis uses 0.55 press / 0.35
release. Analog and digital trigger reports, and alternate buttons for the same
action, are combined so one physical press does not double-fire or double-bash.
Touch/controller key ownership is independent. Pause, focus loss, HUD editing and
device removal clear held input. Reconnection is monitored by InputManager.

The UI sends a coherent four-axis sample across a mutex to the engine thread.
Movement uses the existing Rune inputs; look rates are integrated by the original
PlayerMove/UpdateRotation scripts using DeltaTime. A stationary held stick keeps
working between Android events. Gameplay events are consumed before SDL so they
cannot also execute its older menu-oriented button mappings. One controller is
active at a time; disconnect/reset allows another device to become active.

## Validation

`ControllerMathTest` covers measured drift, device dead zones, rescaled full
travel, invalid samples, trigger hysteresis and gameplay button mappings. Compile
it with RuneController/RuneControllerMath and the Android SDK jar. The optional
`--ez controllerProbe true` launch extra enables bounded-rate logcat axis and
button diagnostics under `RuneController`; normal launches do not log each input.
Use an isolated `--es saveTest` launch with `--ez cameraProbe true` and
`--ez traceCombat true` to compare actual engine position, rotation and actions.
Do not describe injected Android key events as physical-hardware validation.

Android ARM64 and desktop builds pass, as does the desktop Rune probe. The clean
real-touch regression passes thrown-axe retrieval and food consumption. An earlier
run also completed both interactions, but concurrent physical controller presses
invalidated its exact Use-count assertion; the clean repeat is the recorded pass.

A 14.24-second stationary DM-Hildir sample at 2520×1680, native resolution and
4× MSAA measured **119.93 presented FPS** (median 8.328 ms, maximum 16.657 ms,
zero intervals above 50 ms). HWC reported 120 Hz. This is a short fixture sample,
not a campaign-wide or sustained performance claim. The existing Rune-only
refresh rule remains 3. All normal/campaign save hashes and preferences match
the pre-install snapshot. Compact evidence lives in [evidence/controllers](evidence/controllers).

The final installed build received real user-operated input from Kishi device 85
(the rumble interface). Both movement and view coordinates changed; Jump, Fire,
AltFire, Throw and Use reached their original native bindings. R3 started a Rune
kick. No injected input was used in this hardware scene. Evidence includes the
Android button/axis trace and matching native engine samples. These establish
input routing and movement/look, not every combat outcome. The user subsequently confirmed the controls work. Physical unplug/reconnect was
not separately confirmed. L3 now shares Use with Y during gameplay; HUD editing
retains L3 click-and-drag. Virtual input device ID -1 is accepted for injected-key
regressions without losing its key-up events.

## Menu focus recovery (October 25, 2026)

SDL's rendering surface can keep focus behind the Android menu after startup or
resume. Window-level dispatch sent D-pad/confirm keys to that surface, and
synthetic stick navigation bypassed Android's normal focus-search fallback. This
was reproduced on the Open Rune title: D-pad down left SurfaceView focused.
Gameplay routing itself was working.

Menu keys now route directly through the visible page or modal. The first input
focuses the first enabled control in layout order; subsequent directions perform
spatial focus search within that page. Sliders retain their directional input,
A/Enter activates the focused widget, and B returns. Stick navigation uses the
same route. Transparent shortcuts also receive a gold focus outline. Gameplay
input, native libraries and controller device selection are unchanged.

`android_controller_menu.py` passes cold title navigation/A selection, switching
from touch into a choice dialog, modal containment/B return, background/foreground
recovery, and Start-pause/A-resume in an isolated native-4x scene. Preferences
are byte-identical, and all 10 normal/campaign saved-file hashes match the
pre-update baseline. These injected-key checks establish menu routing, not physical
stick validation. ControllerMathTest also passes. See
[evidence/controller-menu/results.json](evidence/controller-menu/results.json).

The user confirmed physical Kishi menu controls work after this fix. They also
reported Continue stayed highlighted while navigating: the home page had an
unconditional first-row highlight. Removed that decoration so the gold highlight
now follows actual focus, including on the pause menu's Resume row.

## Controller-aware touch visibility (October 25, 2026)

Installed on the OPPO with a connected Kishi: accessibility inspection shows
only Menu among the touch controls; visual inspection confirms the HUD remains
and the Menu button uses the menu's iron panel, corner ornaments and gold serif
lettering. Tapping it opens Pause; injected Start resumes. Cold-title navigation,
choice-dialog containment, background recovery and pause/resume checks pass,
as do ControllerMathTest and MenuSettingsTest. Tests use native rendering and
4x MSAA; this UI change has no new measured presentation-FPS claim. Preferences
and protected campaign/save hashes are unchanged.

Physical disconnect restored all eleven movement/action buttons while retaining
Menu and the HUD. The real-touch pickup regression then passed one Throw tap and
two Use taps: the axe was recovered and food consumed at native resolution with
4x MSAA. Preferences and protected saves remain unchanged. Physical reconnect
hid the touch controls again without restarting the app; the retained Menu button
still opened Pause when tapped. See
[results](evidence/controller-visibility/results.json).
