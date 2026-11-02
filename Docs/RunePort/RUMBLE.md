# Open Rune — event feedback and Android rumble

Android is the first output backend. No controller brand, vendor/product ID,
root service or proprietary packet protocol is built into Open Rune.

## Player-facing controls

Controls and Enhancements both open **Rumble & vibration**, backed by the same
settings: On/Off, intensity (0–100%, default 65%), Automatic/Controller/Device
output, a live capability description, and Test rumble. Automatic prefers the
active controller; before any input, it selects a controller only if exactly one
rumble-capable controller is present. It falls back to the phone/tablet vibrator
when available. Controller mode never falls back to the device. Multiple
controllers require input to identify the intended player.

The backend uses Android InputDevice vibration APIs, with VibratorManager on
Android 12+ and the earlier vibrator API on Android 9–11. It checks hasVibrator
and hasAmplitudeControl. Without amplitude control, intensity changes pulse
length. Unsupported hardware reports no supported output and performs no
vibration. Bluetooth/USB support depends on what the OS and driver expose; an
advertised capability is not proof that the physical actuator works.

Rumble stops when paused, editing the HUD, loading a session, losing focus,
backgrounded, disabled, or disconnected. Hotplug invalidates capability caches.
The test button is deliberately allowed in menus and uses a 400 ms preview;
gameplay pulses are capped at 180 ms. Zero intensity suppresses all output.

## Event architecture

`GameFeedback.h` owns portable semantic events, low/high-frequency strength
intent, duration and priority. It has no platform API, UObject, device identifier,
or saved state. A mutex-protected single-pulse mailbox coalesces overlapping
impacts by priority/maxima, suppresses same-kind duplicates within 60 ms, and
discards pulses older than 100 ms. Disabling clears pending output immediately.

`RuneFeedback` observes original script completion: successful inventory
acquisition, local-player health loss, shield impacts, and locally instigated
weapon hits. Nested superclass damage calls produce one event. Remote NPC fights
and zero-damage calls are silent. Weapon strength follows attack damage. Kick and
bash impacts use explicit production hooks, with 35% strength when the target
blocks; incoming unblocked damage uses the health-loss path. The physics landing
hook requires downward speed above 450, so normal small landings do not thump.
No script result, collision, health, animation timing or save format is changed.

| Event | Maximum profile duration | Priority |
| --- | --- | --- |
| Weapon hit | 75 ms | 2 |
| Shield impact | 65 ms | 3 |
| Damage received | 150 ms | 4 |
| Kick connects | 85 ms | 2 |
| Shield bash connects | 100 ms | 3 |
| Heavy landing | 110 ms | 1 |
| Weapon/shield pickup | 35 ms | 0 |

`RuneRumble.java` is the Android adapter. It polls at 16 ms while foreground,
selects the appropriate output by capabilities and player input, scales strength,
and prevents overlapping low-priority pulses from restarting a stronger effect.
Android currently receives a mono pulse using the stronger channel; the portable
low/high intent remains available to future backends. Only primitives cross JNI.

Future Windows/Linux/iOS adapters can consume the same Pulse data and implement
capability detection, playback and stop without rewriting game-event hooks.
Those adapters are not implemented or claimed working by this change. Desktop
builds compile the shared hooks with feedback disabled until a backend enables it.

API references: [Android controller features](https://developer.android.com/games/sdk/game-controller/controller-features)
and [VibrationEffect](https://developer.android.com/reference/android/os/VibrationEffect).

## Validation

- Portable mixer tests pass priority, bounded strengths, duplicate suppression,
  stale-event expiry, disable/re-enable cancellation and transport packing.
- Android and desktop builds, the desktop Rune package/model/level probe, settings
  migration tests and controller mapping/dead-zone tests pass.
- The installed OPPO identifies the active controller's vibration capability.
  Initial previews reached Android and the existing device bridge but were not
  felt. The user unplugged/reconnected the controller and confirmed physical
  vibration works. No device bridge, driver, global strength or policy was changed.
- The isolated original-script/physics fixture emitted pickup, damage, weapon,
  shield and heavy-landing events and Android output requests. Bystander and
  zero-damage cases were silent.
- Real touch kick checks pass unblocked and shield-blocked cases, with strengths
  1.0 and 0.35. Bash checks pass outgoing unblocked and incoming shield-blocked
  cases with their expected feedback events and unchanged combat outcomes.

- Actual touch weapon recovery and food consumption pass; recovery emits the
  pickup event. Off/zero suppression, shared menu values, unsupported Device
  output, defaults-page behavior and background cancellation pass on the OPPO.
- A 17.79-second synthetic combat sample at 2520×1680 native resolution and 4×
  MSAA presented **119.26 FPS**, median 8.334 ms, maximum 16.669 ms, and no
  intervals above 50 ms. Hardware reported 120 Hz. This is a short combat fixture,
  not a sustained or campaign-wide performance guarantee.
- Eight protected save files match the initial snapshot. The campaign checkpoint
  rotated at 15:56:52, before the first rumble-build installation at 15:58; its
  previous bytes remain in Save10.rsa.old. The newer checkpoint is preserved.
  All unrelated preferences are unchanged. The user's tested 100% rumble choice
  is retained; new installs default to 65%. The existing refresh rule remains 3.

Compact evidence is in [evidence/rumble](evidence/rumble). Reproduction tools are
`game_feedback.cpp`, `android_rumble.py`, `android_rumble_menu.py`, and the existing
kick/bash/touch-pickup tools with `--rumble-probe`. Physical hardware is confirmed
only for this installed OPPO/controller setup; other Android devices and drivers
have not been physically tested. The built-in vibrator was absent on this tablet,
so its unsupported-output path was tested, not physical phone vibration.
