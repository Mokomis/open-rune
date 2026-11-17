# Windows lighting-buffer CPU bottleneck — November 8, 2026

The ~24FPS normal-play slowdown on the RTX4080SUPER is primarily CPU actor-draw preparation, not water shading or GPU exhaustion. A native3840×2160/actual4×AA profile attributed ~39ms to CPU rendering and ~1ms to GPU execution. Actor rendering accounted for25–37ms; Gouraud submission spent~23ms constructing lighting packets, including~5ms comparing their previous values.

## Cause and retained fix

`BufferManager.cpp` allocated mesh-light and shadow-light buffers with host-visible/coherent memory but no preference for CPU caching. The selected Windows memory had Vulkan property flags6 (HOST_VISIBLE|HOST_COHERENT). DrawGouraudPolygon compares the previous512-byte MeshLightPacket and reads shadow parameters for every triangle; those reads are extremely slow from this uncached mapping. No conclusion about device-local BAR placement is required or supported by flags6.

The retained change adds HOST_CACHED as a preferred property for these two buffers on Windows, while retaining required HOST_VISIBLE|HOST_COHERENT. Actual flags14 confirm the cached coherent host mapping. Allocation sizes, shaders, light counts, geometry, textures, depth, resolution and AA remain unchanged. Unsupported preferences fall back through the existing allocator. Other platforms retain their prior default; `--render-cached-lighting=0` restores the old preference for diagnosis. No new user-facing menu option is needed.

The diagnostic trial reduced packet preparation22.80→0.72ms and comparisons4.74→0.045ms. Those two freely advancing walking runs end at different positions and are diagnostic, not the controlled throughput comparison below.

## Controlled qualification

Both scenes use3840×2160, actual4×AA, the same remastered effects/materials,120Hz deterministic simulation, and fixed seed1. The walking fixture runs the same360-frame input followed by120frames of settling; all runs reach the same position. The waterwheel uses the same above-water camera. The order is off/on/on/off. Measurements are renderer throughput, not panel refresh or sustained whole-game guarantees.

| Workload | Old memory | Cached memory | GPU before/after |
|---|---:|---:|---:|
| Seaside walking, Crest | 19.51FPS | 115.77FPS | 1.251 / 1.251ms |
| Waterwheel, Enhanced | 12.97FPS | 33.56FPS | 1.651 / 1.647ms |

Both optimized repeats in each scene match the first baseline frame pixel-for-pixel. The walking baseline repeat also matches. The last slow waterwheel baseline repeat varies in animated sprites (12,453 pixels,~0.15% of the frame); its diff is retained. The initial blanket repeat-equality assertion therefore failed, and was reviewed rather than erased. All four optimized/reference comparisons pass; no optimized/reference mismatch was waived.

Temporary fine-grained profiling was removed from the release source. Clean builds pass: the main Windows client measures123.84FPS on the controlled walking fixture, the Crest candidate117.59FPS, and the latter remains pixel-identical to its instrumented optimized reference. The existing Vulkan GPU readback probe passes at native4×AA with CPU/GPU overlap enabled, covering its depth/alpha/hit/resolve and shadow tests. This is not a Khronos runtime-validation-layer pass; that layer is unavailable on this PC.

## Scope and reproduction

The fix is in primary `C:/OpenRune/work/OpenRune-main` and the isolated Crest worktree. Main binary: `build-windows/SurrealEngine.exe`. Crest remains experimental and is not merged into main. Android/Mac defaults and installed clients were not changed. Existing primary uncommitted work, game data, configuration and saves are preserved. No commit or push.


## Earlier waterwheel FPS context

The older Android scaling comparison in [SYSTEM-SCALING.md](SYSTEM-SCALING.md) measured95.10 displayed FPS for hardware75% and89.52 for GSR75%, using1890×1260 internal rendering,4×AA and older world features8103. It must not be compared directly to this native4K Windows fixture with features200614. Other campaign-entry results also use different viewpoints/effect sets.
