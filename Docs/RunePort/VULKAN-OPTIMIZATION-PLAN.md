> Follow-up: see [RENDERER-FIXES.md](RENDERER-FIXES.md) for implemented changes, selected defaults and validation. This document retains the original proposal.

# Native Vulkan bandwidth optimization plan

October 24, 2026. Read-only source audit; neither experiment below is implemented
by this audit. Preserve native 2520x1680 rendering, RGBA16F color, 4x MSAA, existing
visual controls, hit testing and frame capture. No renderer edits, build or device
actions were performed for this document.

## Evidence and limits

The parent task measured Hel3a at **92.2504 displayed FPS with 4x MSAA**, then
**113.8024 displayed FPS without MSAA**, both at native resolution. These are
sequential diagnostic samples, not controlled thermal evidence or a measured gain
from the proposed changes. The restored 4x repeat measured 54.375 FPS; see the repeat caveat below.
The preceding 4x sample reported about 3.5 ms CPU tick, including 0.8 ms particles,
and 9.27 ms GPU scene time. This supports investigating GPU work; it does not
isolate memory bandwidth from sampling/rasterization costs or guarantee 120 FPS.

`CommandBufferManager.cpp` places the scene-end GPU timestamp after scene resolve
and optional bloom, before presentation. Thus `gpu_scene_ms` includes resolve and
bloom, not just scene geometry/fragment shading. Add intermediate timestamps only
if needed to distinguish these costs, updating query-pool size and readback too.

`SceneTextures.cpp` allocates 4x RGBA16F color, R32_UINT hit and D32 depth targets.
`RenderPassManager::CreateRenderPass` clears/stores all three. The continuation
pass loads/stores all three. `VulkanRenderDevice::BlitSceneToPostprocess` resolves
color externally to single-sample `PPImage[0]`; hit data is resolved/read only when
`HitData` is non-null, although the hit MRT is rendered and stored on every frame.

At native resolution, uncompressed logical surface sizes are 135.5 MB for 4x
RGBA16F color and 67.7 MB each for hit and depth. These are not measured DRAM
traffic: tiling and compression can change actual cost.

## Experiment 1: discard unused hit contents

This is the smallest standalone first experiment. Capture whether hit testing is
active at `VulkanRenderDevice::Lock`, and use that same frame decision through
all flushes and `Unlock`.

- In `RenderPassManager::CreateRenderPass`, provide compatible normal-frame and
  hit-test variants. For normal frames, change only the hit attachment store to
  `DONT_CARE`; in the continuation variant, also use hit load `DONT_CARE`.
- Keep color/depth load/store unchanged. Keep existing hit clear initially to
  minimize the experiment; normal hit contents are never consumed.
- Select the matching initial pass in `Lock`, and matching continuation pass in
  `Flush` and `FlushDrawBatchAndWait`. Preserve existing hit-test LOAD/STORE and
  readback paths. Cache variants; do not rebuild pipelines every frame.
- Load/store differences preserve render-pass compatibility, so scene, line,
  point pipelines and the existing framebuffer can remain unchanged. Test with
  Vulkan validation rather than assuming compatibility from a successful draw.

This can avoid unused hit stores but retains hit export and tile footprint. Full
MRT removal is a later experiment: use a one-color subpass, matching scene/line/
point pipeline blend attachment counts, and a fragment shader variant without
`outHitIndex` (embedded `FileResource.cpp`). Retain the original hit-test family.
Do not reuse a two-color pipeline against an incompatible one-color pass.

## Experiment 2: resolve color inside the scene pass

Keep experiment 1 separately switchable for attribution.

- Extend `RenderPassBuilder` in `SurrealGPU/include/surrealgpu/vulkanbuilders.h`
  and `SurrealGPU/src/vulkanbuilders.cpp` with owned resolve-reference storage
  and `pResolveAttachments`. The array must match color-reference count; use
  `VK_ATTACHMENT_UNUSED` for the hit output. Refresh pointers after vector growth.
- In `RenderPassManager::CreateRenderPass`, append the existing single-sample
  RGBA16F `PPImage[0]` as color's resolve attachment in both initial and continuation
  variants: resolve load `DONT_CARE`, store `STORE`. Keep MSAA color/depth `STORE`.
- Append `PPImageView[0]` to `FramebufferManager::CreateSceneFramebuffer` in the
  matching attachment order. Its existing image usage supports color attachment.
- At `Lock` and continuation boundaries, synchronize the resolve target for color
  attachment writes. After final scene completion, transition it from color
  attachment layout to shader-read layout with COLOR_ATTACHMENT_WRITE to
  SHADER_READ access and COLOR_ATTACHMENT_OUTPUT to FRAGMENT_SHADER stages.
- In `BlitSceneToPostprocess`, omit the external color resolve for this 4x path.
  Keep the existing 1x copy/blit fallback and actual hit-test resolve/readback.
  Do not accidentally resolve again after bloom, overwriting its result.

Single-subpass resolve-reference differences are ignored for render-pass
compatibility; framebuffer attachment count still must match. Removing an actual
color reference is a different change and requires matching pipelines.

## Continuation, capture and synchronization constraints

**Do not discard multisample color or depth.** `Flush`, vertex/index overflow in
`ReserveVertices`, and bindless-array exhaustion through `GetTextureIndexes` all
end and submit the scene pass, then resume with LOAD. Store ops were chosen at
pass begin and cannot be changed retroactively when overflow occurs. Discarding
these attachments would lose earlier draws. Every in-pass continuation resolve
is acceptable; the last pass resolves the final complete scene.

Do not add transient/lazily allocated storage to the existing sampled/transfer
usage combination. Fully memoryless MSAA needs separate work on continuation and
image usage; it is not part of these two bounded experiments.

`ReadPixels` reads `PPImage[0]` (with optional present/gamma conversion into
`PPImage[1]`), not the multisample color image. Bloom/Enhanced also consume
`PPImage[0]`. Preserving its format, descriptor and final shader-readable contents
preserves their contract, but both must be visually tested. Include read-after-write
and write-after-write hazards when the same target is reused; UNDEFINED layout
does not remove execution/memory synchronization requirements.

Keep existing submission/resource-drain rules, resize/MSAA recreation, mailbox,
Swappy semaphore bridge and device refresh policy unchanged. Shared scene images
are protected by the current one-active-GPU-submission arrangement.

Related existing barrier defects worth correcting narrowly when implementing:

- `Lock`, `Flush`, and `FlushDrawBatchAndWait` put pipeline-stage constants into
  `srcDepthAccess`/`dstDepthAccess`; use depth/stencil attachment access bits.
- Their depth barriers use `DEPTH_ATTACHMENT_OPTIMAL` while scene passes declare
  `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`; make layouts consistent.
- `BlitSceneToPostprocess` ORs `VK_ACCESS_TRANSFER_WRITE_BIT` into a stage mask.
  It currently has the same numeric value as TRANSFER stage, but use the correct
  stage constant rather than relying on that coincidence.

## Required validation and acceptance

1. Run core and synchronization validation for normal and actual hit-test frames,
   including a hit-test frame followed by an ordinary frame.
2. Force the real mid-frame flush/overflow path; verify geometry before and after
   continuation survives. Include depth occlusion and overlapping translucent
   drawing, not only a clear-color or triangle test.
3. Capture the same views with Original, Enhanced and bloom. Compare edges,
   transparency, HUD, gamma and effects through both display screenshots and the
   engine `ReadPixels` path. No resolution/format/MSAA reduction is acceptable.
4. Exercise 1x/4x switching, resize, map loads and repeated capture. Hit selection
   and the requested hit rectangle must retain existing results.
5. Measure actual displayed FPS plus CPU/GPU timings on Hel3a, SailingShip and
   HelLift. Use matched view/settings and short alternating baseline/experiment
   runs; record order and starting temperature to identify confounding, without
   starting an extended heat campaign. Keep the 120 Hz per-app rule unchanged.
6. Accept each experiment separately only if correctness passes and repeat
   measurements support a benefit. Keep a diagnostic fallback for comparison.

## Specification references

- [Khronos MSAA sample](https://github.com/KhronosGroup/Vulkan-Samples/blob/main/samples/performance/msaa/README.adoc):
  in-pass resolve and the additional store-discard/transient-memory opportunity.
- [Vulkan render-pass specification](https://docs.vulkan.org/spec/latest/chapters/renderpass.html):
  attachment operations, resolve references and render-pass compatibility.
- [Khronos tile-rendering guidance](https://docs.vulkan.org/guide/latest/tile_based_rendering_best_practices.html):
  keeping multisample resolve within the render pass.

### Sequential repeat caveat

After the no-MSAA diagnostic, restoring native 4x MSAA measured 54.375 displayed
FPS in another stationary Hel3a sample. CPU tick increased to 5.923 ms and the
combined GPU scene/resolve/postprocessing interval to 13.958 ms. The earlier
92.250 FPS sample is therefore not a stable sustained result. These sequential
runs do not isolate device state, and the source of this variation is unresolved.
Do not attribute the full difference to antialiasing or advertise a fixed gain.
The latest ARM headless suite passes, including SystemCoords, Core.VSize2D and
hidden-particle assertions.
