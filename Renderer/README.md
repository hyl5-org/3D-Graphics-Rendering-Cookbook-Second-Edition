# Renderer shaders

All Renderer shader stages and shared shader code are in `shaders/*.slang`.
Stage files use `.vert.slang`, `.frag.slang`, or `.comp.slang` and the `main`
entry point. Shared files have a single `.slang` extension.

The build requires Python 3 and `slangc` (included in the Vulkan SDK). CMake
finds `slangc` through `VULKAN_SDK` or `PATH`; pass
`-DRENDERER_SLANGC=/path/to/slangc` to select another installation.

The `RendererShaders` target compiles SPIR-V and embeds it in the generated
`RendererShaders.h`. Renderer loads those binaries through `loadRendererShader()`.
Shader edits trigger compilation on the next build. Android uses the host
compiler and embeds the same binaries, so no runtime compiler or shader asset
copy is needed.

Matrices use column-major storage to match GLM, with explicit `mul()` operations.
Physical pointers and push constants use scalar layout. Descriptor bindings
match LVK: sampled images at 0, samplers at 1, storage images at 2, and TLAS at 4
in descriptor set 0. Stage inputs/outputs use FP32; FP16 is retained for internal
lighting, denoising, and OIT data.

The TLAS array has a fixed size of 128 to preserve LVK's Adreno workaround when
loading precompiled SPIR-V.
