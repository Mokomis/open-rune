# Surface-zone lighting correction — October 29, 2026

DM-Ruins rendered large walls and stairs solid black in both Original and Remastered modes, with snow off and on, offline and in multiplayer. A fixed native-resolution view reproduced the issue on Windows; the live OPPO screenshot showed the same failure.

## Cause and correction

`VisibleFrame::ProcessNode` passed `swapFrontAndBack` as `ProcessNodeSurface`'s `front` argument. The former is true on the negative plane side; the latter selects Zone1 when true. This inverted the zone used for surface ambient light and sky/warp portal selection. Black DM-Ruins surfaces with no direct baked lights received ambient brightness 0 from the wrong zone, instead of the authored brightness 40 from their visible side. Diagnostics showed finite lightmap calculations, with the corrected ambient restoring illumination without adding lights or altering exposure.

Each coplanar polygon now classifies the viewer against its own plane; the positive side selects Zone1 and the negative side Zone0, consistent with `FindZoneAt`. Independent classification handles reversed coplanar planes. The fix is shared by platforms, Original/Per-pixel/Remastered rendering and single/multiplayer. It does not modify game packages, add a map-specific brightness override, change gameplay/network data, or add a render pass. Temporary lightmap diagnostics were removed from production.

## Validation

Evidence is in `outputs/ruins-lighting-20261029` on the controlling Mac and corresponding Windows output folders. Matched 3840x2160 / 4x MSAA captures verify Original and Remastered restore the courtyard. Heavy-snow comparisons and Hildir/village reference renders pass. Hildir's near-black fraction is unchanged; the primary Ruins frame drops from 66.2% near-black pixels to 5.9%, including HUD/sky. This measures the captured image, not a general lighting target. The corrected courtyard run averaged 120.08 FPS over 240 profiled frames; the live host was running concurrently, so this is a short rendering smoke check, not a sustained-performance benchmark. Windows and ARM64 builds pass. Original assets/saves remain untouched.

The signed update was installed on the OPPO. An isolated 660-frame run confirmed 2520x1680 native render target and 4x MSAA with Heavy snow. The 600 profiled frames averaged 113.68 FPS, p95 9.28 ms; the final screenshot shows restored wall/stair detail. This is a short static-view test, not sustained combat. Saved preferences were byte-identical before/after: normal Hardware Scaling 50%, 4x AA and Heavy snow remain intact. APK SHA-256: 534f88477459cc081187ab9bcd9da2f4716ae6b769b1064ed006169c952fbcbf. The Windows production binary is rebuilt, with a preserved candidate SurrealEngine-LightingFix.exe. The existing host process remains running on its previous executable because this client rendering change requires no server/protocol update.
