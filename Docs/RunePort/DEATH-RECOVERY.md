# Open Rune — death recovery

October 25, 2026.

The Android menu previously had no connection to the player's Dying state. The
original campaign scripts did not provide a usable restart through the replaced
menu, leaving the player looking at the corpse with combat controls still shown.

The engine now publishes death state separately from HUD visibility. After a
short delay, Android clears held inputs, pauses the world and opens a themed
“You have fallen” screen. Retry checkpoint loads slot 10; Load game offers the
existing save list; Return to title remains available. Missing campaign checkpoints disable
Retry and explain that no checkpoint is available. B/Start and title Continue cannot return the user to a dead gameplay session.
No new autosave is made, and no original game script/package was changed.

A real GoblinWarrior killed the player in an isolated native-resolution 4x MSAA
fixture. Automatic recovery, hidden gameplay/save controls, B/Start guards, load
list back navigation, title Continue, a missing-checkpoint case and controller A
checkpoint recovery passed. Controller keys were injected; this is not a new
physical-controller confirmation. Preferences were byte-identical. The test is
`Tools/RuneValidation/android_death_menu.py`; compact results are under
`evidence/death-recovery`.

The user's crab report was clarified as minor sliding, with attacks working.
Crab AI/physics have not been changed based on that observation.

All ten protected save files and the Activity preferences match the pre-test
snapshot. The final device handoff reloads DeepUnder1Odin from the original
campaign checkpoint and leaves it paused at native resolution with 4x MSAA.

## Practice-fight recovery — October 27

A practice fight in DM-Hildir does not create a campaign checkpoint. Its death
screen previously showed a disabled Retry checkpoint button, leaving no direct
way to repeat the fight. Practice sessions now show **Restart fight** instead.
The engine retains the map, enemy class, starting shield and spawn delay, clears
held inputs and actor references, then recreates the encounter between frames.
Campaign load/new-campaign and network transitions clear the practice marker.
Even if a campaign checkpoint exists, practice restart does not load it.

The loading notice now clears when the arena is ready. Campaign checkpoint
loading remains the existing slot-10 path. The touch regression uses actual
Android button presses and stick drags, including after map replacement;
`android_practice_retry.py` covers two deaths/restarts, with a copied checkpoint
present for the second. `android_death_menu.py` covers the recovery navigation
and explicit checkpoint loading from the save list. Both isolate their saves.

### Short touch presses

The follow-up touch check found a separate input issue: an Android press and
release can arrive in one event batch. The release previously removed a Rune
`Button` binding before the next simulation tick, so quick Attack taps reached
`Fire` but produced no swing. Held presses worked. Rune button presses now remain
visible for one simulation update and clear on the next update if no longer
held. Pause, map changes and session changes discard pending pulses. This adds
no fixed input delay and does not change the network client's input path.

The restart test now asserts an actual attacking animation or weapon sweep after
each short touch, as well as Fire/Jump bindings. Its startup deletes its own old
log before launching so the previous run's death cannot satisfy a new wait.

Final installed-build checks passed twice: each real death-menu tap recreated
DarkViking on DM-Hildir, and each quick Attack tap after restart produced a
weapon swing. Movement, camera turns and Jump also responded. The second restart
ignored the copied campaign checkpoint as intended. All 196 pre-existing save
and backup hashes and the Activity preferences matched the pre-fix snapshot.
Native 2520×1680/4× MSAA remained active. Results and the installed APK hash are
under `evidence/death-recovery/practice-retry/`. The final handoff is a fresh,
paused Viking encounter with a shield and no automated combat inputs.
