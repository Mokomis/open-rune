# Open Rune — Underwater touch controls

October 24, 2026. Android now exposes **Dive**, holding the original C/Duck
binding. Jump remains the swim-up control. Existing preferences, rendering and
normal saves are unchanged.

The packaged APK was installed over the existing app with the same package and
local signing key. The actual Dive button was located in the Android accessibility
tree at [567,1480][735,1627]. A real touch hold resolved `C=Duck`; in open water
Ragnar descended from z=-1959.48 to -2158.71, with vertical velocity reaching
-255.04. Jump had first produced upward velocity +299.99. The test used the
original swimming scripts and normal collision, without relocation or health
overrides. Quick save/load in the isolated campaign-village-ledge directory
restored the underwater session after installation.

An unpaused SinkingShip sample at native 2520x1680 and 4x MSAA measured
119.105 displayed FPS over 11.82 seconds, p95 8.337ms, maximum 16.674ms,
with the panel at 120Hz. This is a short scene sample, not sustained campaign
performance or a before/after speed comparison.

The earlier natural village-to-ship progression reached SinkingShip with 8 health.
Subsequent control/navigation testing resumed isolated saves; launching directly
into SinkingShip for the APK update also created a fresh 50-health checkpoint.
That checkpoint is separate from the continuous campaign run. The SinkingShip
exit was subsequently reached by traversal, as recorded below. An offline fine-grid world-collision route
reached the lower wreck at normal Ragnar dimensions; this is navigation evidence
only, not a completed level. No speculative world-collision changes were installed.

Evidence: [underwater-controls](evidence/underwater-controls/). The installed
APK hash matches the local artifact; preferences and normal save hashes were
verified unchanged, and Rune's existing refresh rule remains 3.

## Natural wreck exit confirmed

After checking independent walkthroughs, ordinary movement entered the lower
wreck compartment and reached its original exit trigger. The original exit cinematic
started at frame117840 (Scripting, CineCamera2, CineInterpolationPoint2).
SinkingShip2 then loaded and returned control in PlayerSwimming at approximately
(-787.52,-4056.16,-810.24), health50. This was the isolated 50-health checkpoint
run described above, not an uninterrupted continuation of the 8-health village
run. No teleports, manual event calls, collision changes or script changes were
used to complete this section. The unchanged installed Dive build was used.

The lower compartment required returning around the stern/upper structure to its
entry; being physically below the cabin did not mean its entry had been reached.
The final useful approach was around(-814,-89,-3463), through(-592,-95,-3483),
(-439,-44,-3627), and into(-334,-62,-3898). Entering the trigger region near
(-326,-80,-3820), then swimming toward negative Y and diving, reached the original
exit cinematic. These are observed waypoints, not a collision bypass.

The offline world-only path search was temporary desktop instrumentation and has
been removed from source. It was never packaged or installed. Coarse-grid failures
were navigation uncertainty, not evidence warranting a collision fix.

Native4x displayed FPS: lower wreck85.673 (p95 16.668ms, no intervals over50ms);
exit/cinematic/transition107.892 (p95 16.666ms, one125ms interval). The hardware
panel remained120Hz. These scene samples reinforce that120FPS is not sustained
throughout the campaign.

Route references: [GameFAQs](https://gamefaqs.gamespot.com/pc/913855-rune/faqs/9409),
[eXtragry](https://www.extragry.pl/solucje/tytul/rune.html), and
[Kerstin](https://www.kerstins-spieleloesungen.de/loesungen/rune.htm). The first two
describe the wreck-entry/hole sequence; the third describes a falling section
opening the route. We verified the original exit and travel on the device; no
frame-by-frame comparison to original-PC video has been completed.

## SinkingShip2 ruins continuation

The isolated SinkingShip2 checkpoint was reloaded on the OPPO, then Ragnar swam
through the entry corridor and main pool by ordinary controls. He climbed the
east ruins ledge twice using the game's EdgeHanging/pullup state and picked up
the placed RustyMace0 with the actual on-screen Use button. The game displayed
"You have found a Rusty Mace" and the runtime reported RustyMace0 active. An
isolated slot 9 quick save now preserves this weapon-ledge position; its previous
contents were backed up locally first. An automatic slot 10 checkpoint also
occurred while swimming near the cracked-wall cluster. Normal save slots were
not used for this traversal.

An active native 2520x1680, 4x MSAA sample at the mace ledge presented 74.97
frames/s over 11.76 seconds (median/p95 16.675/16.676 ms), with the hardware
still at 120 Hz. This is a scene sample, not sustained performance or proof of
the cause of the shortfall. The engine overlay varied separately from the
actual presentation result.

The original map places a destructible RunePolyobj0 at (48,388,1190), with an
event named WaterRise. Nearby Trigger4 carries the same tag and displays the
break-wall hint. An initial dive reached approximately (70,418,1037) below this
cluster, where a ceiling blocked upward swimming. That did **not** establish a
collision bug; the player subsequently climbed onto the low ledge at the wall.
The map also places a lower-room trigger and a later exit to DeepUnder1Odin;
neither has been reached by ordinary traversal in this continuation.

The next live play pass reached the low ledge at approximately (70,418,1227).
With the Rusty Mace equipped, a normal attack registered a sweep hit on
RunePolyobj2, and the player confirmed that the cracked wall broke and flooding
began. The map's original chain is RunePolyobj2.Event=`BiteMe1` to
RunePolyobj0.Tag=`BiteMe1`, then RunePolyobj0.Event=`WaterRise`. This is a
player-observed flood sequence, not a fixture invocation. The subsequent dive,
tower door, grate, and natural exit remain unverified.

Repeated manual short swims consumed disproportionate time. For the next pass,
use map actor positions and the walkthrough to select meaningful waypoints,
then run `Tools/RuneValidation/android_waypoint.py` for bounded, ordinary-input
travel between them. It stops on no XY progress and saves a screenshot/trace;
the result is navigation assistance only. Inspect original game events and
on-device behavior at each puzzle, and keep direct exit/save fixtures labeled
separately from completed traversal. Current local evidence is under the
untracked game analysis folder `analysis/campaign-ruins/`; no original game
assets or screenshots are committed.
