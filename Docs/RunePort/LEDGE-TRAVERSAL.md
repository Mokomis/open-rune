# Open Rune — Ledge grabbing and village traversal

October 24, 2026. Continuing campaign traversal from `052a176c` exposed a
missing native call: falling/swimming never asked the original Rune scripts to
grab a ledge. A real touch route from RagnarVillage2's original player start
became stuck against its shallow water bank. The same route with the helper
implemented grabs the bank, runs EdgeHanging's animation, and returns to dry
PlayerWalking. This is a traversal fix, not a completed campaign claim.

## Recovery and implementation

REA/Ghidra analysis of the user's Engine.dll identified the internal helper at
`0x103b9640`, called by physFalling with height multiplier2.0 and physSwimming
with2.5. Its forward hull trace tests a near-vertical wall, traces above and over
the lip, checks a walkable top and clearance, then calls original GrabEdge with
height and inward direction. The temporary flag is **bWarping**, established
from assembly and the original Pawn property layout, not bCanStrafe. The
decompiler has bad stack recovery; retained assembly was necessary to interpret
the trace arguments. The upward trace whose result is unused was omitted.

`UPawn_Ledge.cpp` integrates this query with existing world/Mover/Polyobj
collision and the original RunePlayer/ScriptPawn event. Original script owns
placement, head-water/proxy-state guards, animation, and return to walking.
The helper restores bWarping on all returns. Actor overlap uses the port's
player/non-player blocking flags. Collision tolerance and encroachment behavior
are adaptations, not a claim of bit-for-bit original engine parity.

Recovery reproduction: `Tools/RuneRecovery/ledge_native.py`, with
`RUNE_GAME_ROOT` and `RUNE_ANALYSIS_DIR` pointing to the local licensed game and
an analysis directory. It uses pinned REA6.3.0 and existing Ghidra/Java only.
Evidence is in `evidence/ledge-native/`:

- TryGrabEdge: `ev_6056c9dee64dd817906f37997638281c198006a23a1386e6e3b58a1f8df4e1de`
- eventGrabEdge: `ev_b1e2f6bf3f547f14d598c02a1168e5f0db9cf183ead554905bc9c9ba49f2055d`
- physFalling: `ev_4bf1b0522ff73fca7c0d558e038a9a6871dd043f33cd7290bc9ef1caf349534d`
- physSwimming: `ev_7dfc80d0f97402fa7035926a77c740141c6a553651d245eab118494b5f786e12`

## Device evidence and limits

`android_village_ledge.py` uses actual Android arrow holds from the original
map start, seed1, a separate save directory, native2520x1680 and4xMSAA. It does
not relocate the player or invoke an exit. `--expect-blocked` reproduces the
old result near(-1085,-306,-316). After the fix the original GrabEdge reports
distance9.18, enters EdgeHanging/PHYS_Flying, and finishes walking dry beyond
the bank near(-1376,706,-213), health50. The strengthened driver requires the
animation state followed by dry walking, not merely the grab marker.

Manual normal-input traversal subsequently reached the longhouse approach near
(2020,1318,-85). The first village alarm and transition to SailingShip still
need testing. This route is a functional path, not proof every detour is needed
by the original level. No direct training/exit events were fired.

Desktop and Android ARM regression suites pass, including a new open-air ledge rejection
check that preserves location, velocity, physics, script state and the previous
bWarping value for enabled/disabled grabbing and both height multipliers.
The initial synthetic empty-level fixture lacked a collision node; it was
corrected before this pass. Actual jumping, underwater ledge rejection, moving
platform ledges, and NPC grabs require further rendered coverage.

On the final installed build, the strengthened real-touch bank replay passes.
Android rope climbing/jump release at60/120 simulation steps, isolated save
flow, and saved-slot resumption also pass. These remain separate fixtures.

The pre-fix water view measured71.95 displayedFPS over10.88seconds with hardware
at120Hz. This was native4x with the user's saved bloom/clarity/world effects;
it is not a controlled performance comparison. A final-build bank view measured
119.14 displayedFPS over11.93seconds, median/p95 8.335ms, max16.671ms, no frames
over50ms, hardware120Hz. Different viewpoints/device states mean these two
samples do not establish a speedup or sustained campaign-wide120FPS.

Normal save hashes and saved graphics preferences match before/after. The
installed final APK SHA256 is
`a47ddf724febf0507cc4c8e34f407fadcaffbf090a8b90b9cfb462a9016679d2`.
Package and signing key are unchanged; no refresh rule was modified.

Local detailed logs/images are under `rune-classic/analysis/campaign-progression`.
Normal saves and preferences were captured before testing; fixtures use
`campaign-village-ledge` and must never replace normal slots.
