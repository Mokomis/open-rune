# Open Rune — Android Use-button correction

October 24, 2026. Follow-up to v0.6.0 after the user reported that nearby weapons and items could not be picked up.

## Cause

Android's Use button sends `KEYCODE_CTRL_LEFT`. SDL translates this to `IK_LControl`. The installed Rune configuration binds `Ctrl=Use`, leaves `LControl=` empty, and binds `RControl=AltFire` for Block. The engine looked up only the exact key, so the actual touch button did nothing.

The earlier weapon probe injected `IK_Ctrl` directly. It validated the original pickup script and attachment, but bypassed the Android key identity and did not validate the Use button. The v0.6.0 results must be read with that limitation.

## Fix

When Rune receives Left Control and its explicit binding is empty, input-command lookup falls back to the configured legacy Ctrl binding. Explicit Left Control assignments and Right Control/Block are preserved. This is a runtime fallback; no INI bindings or save files are changed. The generic weapon probe now sends Left Control so it cannot silently bypass this mismatch again.

`--trace-rune-combat` records resolved input bindings. The opt-in `weaponTouch` fixture suppresses automatic Throw/Use injection, positions the player beside the landed axe, and later spawns a nearby HealthFruit1 at 50 player health. All actual Throw/Use actions come from Android screen taps.

## Reproduce

Run `python3 Tools/RuneValidation/android_touch_pickup.py --device DEVICE --output /absolute/path/touch-pickup` with the tablet unlocked, physical controllers disconnected and Touch controls enabled. Connected gamepads now automatically hide these buttons. The tool finds the real buttons in the accessibility tree and taps Throw once and Use twice. It requires resolved `Q=Throw` and `LControl=Use`, recovered/attached weapon state, food healing, and bounded completion. Native rendering and isolated saves are selected.

Pickup still follows Rune's rules: stand close, stop moving, be on the ground, and wait until the current attack/throw/pickup animation is finished before pressing Use. A running player, busy animation, full-health food item or unsupported interaction may be rejected by the original scripts. This fixture does not prove every item or terrain situation.

## Verified device result

On the installed correction, the actual Android Throw tap emitted `Q=Throw`, and both Use taps emitted `LControl=Use`. The thrown axe was recovered and attached; HealthFruit1 consumption raised player health from 50 to 65. The 3,700-frame run completed. Evidence is under [evidence/touch-pickup](evidence/touch-pickup/). Desktop headless native/model/dynamic checks also passed.

Installed APK SHA-256: `c101db0d2ab664c972e682c1b7e5291a521ee7d72686c27a33324599169aceea`. Device bytes match the packaged APK and the prior refresh properties are unchanged.

The native Block regression also passed: the raised shield prevented health loss until durability ran out, then subsequent hits damaged the player. Right Control still resolves to AltFire.

## Follow-up: thrown weapon base cleanup and L3

The fixture now waits for Pickup/PHYS_None before positioning Ragnar, then allows
time for him to reach the floor before requesting Use. A fixed frame previously
allowed slow Android accessibility/tap delivery to put him beside a weapon still
in flight. `--controller-use` substitutes two injected Android L3 presses for the
Use taps; Throw still uses the screen. This checks mapping/release through Android,
not physical hardware. See [THROWN-WEAPONS.md](THROWN-WEAPONS.md).
