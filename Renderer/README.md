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
match the VR backend in descriptor set 0: sampled images at 0, samplers at 1,
sampled 2D arrays at 4, and storage 2D arrays at 5. Stage inputs/outputs use FP32;
FP16 is retained for internal lighting and OIT data.

The VR shaders preserve per-eye matrices through `SV_ViewID`, layered HDR and
OIT images, shared eye adaptation, and OpenXR visibility-mask rendering. The
mesh push constants contain the OIT state inline to match the VR C++ layout.
The existing fragment shading rate attachments remain configured by C++.
