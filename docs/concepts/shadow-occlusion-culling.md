# Shadow occlusion culling

This sample follows the camera-depth-reprojection receiver/caster idea in Haar and
Aaltonen, *GPU-Driven Rendering Pipelines* (SIGGRAPH 2015). Receiver generation is
per pixel, while caster culling remains per mesh rather than per cluster.

## Frame sequence

1. Update lighting constants, then render the camera GBuffer/depth.
2. Clear a 64x64 receiver grid on the GPU. Reproject every covered camera-depth
   pixel into the orthographic directional-light view. Skip background depth.
3. Atomically store the farthest receiver depth in the projected cell, without
   expanding coverage to neighboring cells. Wave reductions merge depths for the
   same cell, then a workgroup-shared cache merges results across waves. Hash
   collisions use linear probing. Each occupied cache entry issues one global atomic Max.
4. Build all six max-depth mips in one dispatch with a single 256-thread workgroup.
   Each lane reduces a 4x4 tile, then the group finishes in ping-pong shared memory.
5. Project each caster world AABB on the GPU. Reject bounds outside the light
   projection, over empty receiver regions, or downstream of all covered receivers.
   Query a conservative covering mip rectangle of at most four cells.
6. Write instanceCount to a separate indirect buffer and draw the 4096x4096 D16 shadow map.
   Camera draw lists and original baseInstance/draw-data mappings remain untouched.
   Commands are not compacted; culled commands retain zero instanceCount.

Use max reduction, not occluder-style min reduction. Positive floating-point depths
are encoded as ordered uint bits plus one; zero denotes an empty receiver cell.
This preserves receivers at depth zero. Off-camera upstream casters can survive.
The grid is an R32_UINT storage texture with seven mip levels (64x64 to 1x1).
Each mip has an RWTexture2D<uint> view. All levels remain in GENERAL layout,
with explicit compute barriers before and after the hierarchy dispatch; only the owning texture is
passed to LVK dependencies so its views do not track conflicting image layouts.
The finite-resolution receiver mask is an approximation, not an exact
continuous-geometry coverage proof; check disocclusions and grazing surfaces visually.

Enable **Shadow Mapping / Shadow occlusion culling** and disable **Ray Tracing / RT
shadows** to compare. Both culled and reference raster shadow maps update every frame.
Transparent receivers are not represented in opaque depth, so showing transparent
meshes falls back to the full caster list. Mesh bounds are currently static, as in
camera culling; animated geometry would need updated bounds.

Command-buffer labels identify reprojection and caster culling; CPU work has Tracy
instrumentation. Visual equivalence and performance require GPU measurements.

Reference: [GPU-Driven Rendering Pipelines, SIGGRAPH 2015](https://advances.realtimerendering.com/s2015/aaltonenhaar_siggraph2015_combined_final_footer_220dpi.pdf).
