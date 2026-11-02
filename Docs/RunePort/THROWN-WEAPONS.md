# Open Rune — Thrown weapons following Ragnar

October 25, 2026. The user reported that a thrown warhammer would follow Ragnar
when he moved near it. They also requested left-stick click as an easier Use key.

## Cause and correction

A spawned inventory actor can inherit a physics `Base` from an overlapping actor.
Joint pickup previously set `AttachParent`/`JointChild` and `bCarriedItem` without
removing that independent physics link. Throw/drop cleared joint attachment, but
the old base retained the item in its native `BasedActors` list. Moving the base
therefore moved a supposedly detached, resting weapon too.

The diagnostic reproduced this with original SpawnCopy/AcquireInventory, a
non-respawning DwarfBattleHammer and ordinary Throw/movement bindings in DM-Hildir.
Before the fix, the landed weapon was in Pickup, PHYS_None, with no owner or joint
attachment, but `Base=Ragnar0`. Ragnar walked **271.46 units** and the hammer moved
**107.11 units**, obstructed by the surrounding geometry. This establishes the
physics-base issue independently of camera/rendering appearance. The earlier
respawning arena fixture instead inherited a base from another inventory actor
and did not reproduce player-following; it is not the failing control.

Joint attach now removes any previous physics base. Joint detach also removes it
so carried items loaded from older saves are released cleanly. Detachment still
captures and preserves the animated joint's world position and orientation.
Original game scripts/assets, throw velocity, gravity and bounce behavior are
unchanged. Existing already-dropped objects in old saves are not mass-rewritten;
reacquiring and releasing one applies the corrected attachment lifecycle.

Left-stick click (L3) now invokes the same Use action as Y and touch Use. It retains
Rune's normal pickup conditions: close, stationary, and not busy attacking or
throwing. HUD editing still uses L3 for dragging. The controller's unselected ID
now differs from Android's virtual device ID -1, preserving release events during
injected L3 regression tests.

## Verification

The headless attachment test starts with a physics base, attaches to a joint,
and requires removal of both Base and the old parent's BasedActors entry. It then
reintroduces a legacy link before detachment and requires cleanup without changing
the world release pose. The complete desktop Rune model/native probe passes.

`android_weapon_follow.py` grants non-respawning inventory through original
scripts, throws, waits for Pickup/PHYS_None, walks away without Use and compares
positions. It uses isolated saves, native resolution and 4x MSAA. Fixtures are
not campaign-playthrough or physical-controller proof. Before/after evidence is
under [evidence/weapon-follow](evidence/weapon-follow).

On the installed fix, DwarfBattleHammer, DwarfWorkHammer and Handaxe each stayed
exactly stationary (`weapon_moved=0`, `Base=None`) while Ragnar moved about 274
units. The recorded non-respawning grants had inherited `Base=Ragnar0` before joint
pickup, so these exercise the failing lifecycle rather than starting unbased.

Both real touch Use and two injected Android L3 presses retrieved the thrown axe
and consumed food. The repeat L3 press also verifies release handling. Physical
L3 itself has not been separately retested by the user; the earlier hardware
controller test was confirmed working. Isolated save-flow and fresh-process
save-resume passed. The pickup fixture now waits for a settled weapon before
positioning Ragnar: an initial fixed-frame run attempted Use during Settling and
failed; the corrected fixture passed without relaxing its pickup/healing assertions.

Final native 2520×1680 / 4x MSAA presentation sample: **119.92 displayed FPS** over
13.28 seconds, median 8.328 ms, maximum 16.657 ms, no intervals above 50 ms. HWC
reported 120 Hz and the Rune-only refresh rule remains 3. This short sample is not
a campaign-wide frame-rate guarantee. All 10 normal/campaign saved-file hashes
and the pre-test preferences are unchanged. The final unbounded DM-Hildir scene
uses isolated controller-hardware saves with no automated input.
