# Terrain and moving-shadow follow-up

November 3, after the user confirmed the offline stair fix and reported remaining
terrain/shadow pop-in while running.

## BSP bounds

The original rendering walker rejected entire BSP subtrees using stored bounds.
Those bounds can omit part of a subtree's geometry, and they do not necessarily
include moving actors assigned to that node. Correcting individual character
bounds alone cannot protect actors or terrain discarded at an ancestor node.

Rune now builds conservative static subtree bounds from actual polygon vertices
and child bounds once per model. Each rendered frame unions the current visible
actor bounds upward through that tree. Rendering consults this transient cache;
original map bytes, serialized bounds, collision and navigation are unchanged.
Other games keep their existing path. `--bsp-conservative-bounds=0` retains the
reference for bounded isolated comparisons.

An automatic audit loaded all **94 installed map files**. All 94 corrected trees
contain the geometry checked by the audit, with zero corrected-bound failures.
87 maps had at least one mismatch with their stored subtree bounds. A mismatch
is evidence of an unsafe bound for this traversal, not proof it was visible in
every affected room; some geometry/positions may never contribute to an image.
No manual playthrough of 94 maps was performed or required for this check.

## Shadow transitions

Light selection previously replaced shadow maps abruptly as the player moved.
Atlas slot identities now remain stable when rankings change. Departing lights
fade out over 0.16 seconds before replacement; incoming shadows fade in over
0.16 seconds. The existing atlas and pass budget remain fixed. Shadow strength
also fades near the 900-unit selection distance, instead of cutting off there.
The candidate scan no longer truncates the touching-light list before ranking.

The Vulkan receiver shader applies the continuous per-slot strength. The actual
GPU readback reports shadowed/half/zero pixel values of **11 / 116 / 212** on the
Mac, and all existing 28 three-slot cases pass. Five-rate UBSan checks verify
stable identities, zero-strength replacement and the fixed budget. This adds
neither a second atlas nor an extra shadow pass. `--shadow-transitions=0` retains
the abrupt reference path.

Native 2816×1762/4× MSAA short Hildir comparisons measured about 67.81 FPS
reference versus 64.83 FPS corrected, with CPU scene time 1.35 versus 1.27 ms.
Additional correctly included geometry can cost GPU work; these short samples
do not establish a sustained map-wide performance change. Representative
rendered checks supplement the complete static audit. Human confirmation of
the remaining running pop-in is still needed; the updated build is installed.
OPD2515 native 2520×1680/4× checks pass all 68 shadow GPU cases and
full/half/zero-strength readback (11 / 116 / 211).

The finite shadow-light budget and resolution still apply. This work does not
claim every possible visibility artifact is eliminated. Evidence, including
exact arguments, all map results, GPU results and captures, is outside Git at
`outputs/visibility-slam-weapons-20261103`.

## Hidden-light correction

The initial transition integration incorrectly discarded a source when its
`bHidden` flag was true. Normal Rune light actors hide their editor sprites in
gameplay; this does not disable their illumination. Resetting those slots every
frame kept their fade near zero. Source lifetime checks now use level membership
and deletion, while the normal lighting candidate selection handles active
lights. Integrated scene traces include source hidden state and strength, so
validation verifies that real hidden lights reach full strength in addition to
the synthetic atlas/readback checks.
