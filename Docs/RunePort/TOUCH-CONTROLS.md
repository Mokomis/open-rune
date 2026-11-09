# Android touch controls

The dev.7 defaults use the Unity experiment’s circular button shapes and edge-anchored layout in the Surreal client. Movement is bottom left, camera control bottom right, actions surround the camera control, and Menu stays top right. Block and Crouch / Dive are fitted above the right stick to avoid overlapping touch targets. Rune Power remains available. Existing action icons and combat bindings are retained.

All controls remain independently moveable/resizable with adjustable opacity in the touch editor. A physical controller hides gameplay controls while retaining Menu. Double-tapping the centre of the right stick produces the existing jump pulse; drags, long presses and off-centre taps do not.

The one-time touch-layout version 2 migration adopts the new default positions/sizes and retains the previous positions/sizes under `touchLayout.previous.*`. It preserves opacity, HUD, graphics, gameplay settings and saves. Subsequent custom layouts are preserved. Reset layout restores the new defaults.

Validation: layout persistence/migration, viewport bounds and non-overlapping defaults pass. Actual Android View dispatch checks pass for simultaneous sticks/attack, independent release, multi-touch action ownership, Crouch / Dive and Rune Power, centre double-tap timing/cancellation/shared release, hidden controls and Menu retention. The new layout was visually reviewed in Thorstadt at 2520×1680 with 4× MSAA. This is not a new physical-controller reconnect test or sustained performance qualification.
