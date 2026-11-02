# Open Rune — themed touch controls

The Android overlay uses charcoal octagonal controls, antique-gold borders and
ivory action icons/labels matching the menu and HUD. Dive depicts a swimmer
heading downward. This artwork is drawn locally with Android Canvas paths;
no mod assets or generated bitmap assets are required at runtime.

## Controls and editing

Controls → Edit touch controls, also under Enhancements, pauses gameplay and
opens the layout editor. Drag any action button, Menu, either joystick or the
swipe-look region to reposition it. The selected element has four resize handles;
the size slider offers 50–200%, and individual opacity offers 20–100%. Done saves;
Cancel/Back discards the draft. Reset layout is also a draft until Done.
Hide/Show editor permits access to controls beneath the panel. The existing HUD
editor remains separate. Controller editor navigation uses the left stick/D-pad
as cursor, A/L3 to drag, shoulders to select, Start to save, B to cancel.

The left stick supplies analog movement; the right stick supplies continuous
camera rotation with the existing sensitivity/inversion settings. Swipe-to-look
also remains available in its own editable region. Stick axes are combined so
releasing one stick does not clear the other. Android split touch events allow
simultaneous movement, looking and actions. Input cancels on pause, opening a
menu/editor, or connection of a controller. Shield bash still uses double Block;
Use/Throw retain the existing Android key bindings. Rune Power has its own
moveable and resizable button; it activates the equipped weapon's ability after
collecting enough rune energy. With the original manual pickup setting, press
Use near a rune to collect it.

During gameplay any connected Android gamepad hides both sticks, action buttons
and swipe-look region. Menu remains available and retains its edited transform.
Explicitly opening the editor shows previews even with a controller connected;
these do not issue gameplay commands. Disconnect restores the saved visibility.

Normalized free-space anchors and bounded scaling keep controls inside the
current viewport when its aspect ratio changes. Independent touchLayout.*
preferences do not alter HUD layouts, saves, bindings or video options. Existing
Control opacity initializes new layouts; changing that setting later sets all
non-Menu touch opacities together, with individual overrides available in the
editor. The first release does not prevent user-created overlapping controls.

## Validation

Host test: TouchLayoutTest.java with RuneTouchLayout.java and the existing
HudLayoutTest preference stub. Checks draft isolation, legacy opacity migration,
restart persistence, independent transforms, extreme aspect/size bounds, invalid
saved numbers, and non-overlapping default visible controls.

Device drivers: android_touch_layout.py exercises real Android editor touches
and restores preferences; android_touch_pickup.py exercises real Throw and Use
against isolated weapon/food fixtures. RuneTouchProbe is an opt-in (`touchProbe=true` plus `saveTest`) Android View
dispatch fixture with a recording host: it checks simultaneous sticks/actions,
independent release/cancel, two fingers per button and connection visibility
without sending any commands to the game. Runtime results are
recorded separately below after installed-build verification.

## Installed OPPO checks, October 25, 2026

- Host transform/persistence/bounds/default-spacing checks passed.
- Actual editor touch drag, corner resize, independent opacity, Cancel, all
  eleven elements' size sliders and process-restart persistence passed.
- Actual Android Throw and two Use taps recovered the weapon and consumed food
  with a native 2520×1680 render target and 4× AA.
- The opt-in Android View dispatch fixture passed concurrent movement, camera
  and Attack; releasing movement preserved camera/Attack; last-pointer release
  and Cancel cleared input. Two fingers on one action retained that action
  until the final finger lifted. Hiding controls released held inputs and kept
  Menu visible; restoring visibility restored all controls. This is a View
  dispatch test with a recording host, not physical-controller hotplug proof.
- All four normal save hashes and all pre-existing preferences matched their
  pre-test snapshots. The handoff uses the copied checkpoint in the existing
  seven-inch 16:9 preview, with the user's saved graphics settings restored.

Evidence: [editor](evidence/touch-layout/editor-check.log),
[pickup](evidence/touch-layout/pickup-check.log),
[input](evidence/touch-layout/input-check.log).
No sustained FPS or full-campaign performance claim is made for this UI change.

Physical hotplug was subsequently verified on the installed build: Android
reported the Kishi V3 Pro interfaces reconnecting; the live accessibility tree
contained Menu and none of Move, Camera, Attack, Block, Use, Dive, Kick, Jump or
Throw. See [connected tree](evidence/touch-layout/controller-connected.xml).
No automated gameplay input was sent after the user was invited to try the sticks.

The user clarified the seven-inch preview was a one-off. Final handoff restores
the full tablet viewport (no aspectPreview/previewInches extras) and loads the
same isolated copied checkpoint; future routine testing uses the full viewport.

## Circular sticks and icon revision

Both stick bases and thumb pucks are circles without directional chevrons. The
right stick's default anchor moves to the right edge, near the previous Attack
position. Block sits above, Attack/Kick to the left and Throw/Dive/Jump below.
Existing saved custom transforms are retained; Reset layout adopts the defaults.
Kick is a forward-kicking boot; Jump is three centered horizontal lines decreasing
in length upwards; Throw is a bent forearm/hand gripping a raised axe. Dive uses a
head-first pose with two straight trailing legs and forward-reaching arms.

The [no-diving pictogram reference](https://freesvg.org/no-diving) was inspected
for the recognizable diving pose. Our icons remain original Android Canvas
geometry: no downloaded SVG, image or third-party drawing code is bundled.

Revision checks: default spacing/bounds tests passed; the installed full-tablet
editor was visually inspected; Android View multi-touch/release/hiding checks
passed with the right-edge circular-stick layout. Existing preference bytes were
unchanged. Show editor was moved below the top HUD to avoid obscured text.
