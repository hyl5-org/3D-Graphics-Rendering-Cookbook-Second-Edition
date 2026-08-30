param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$tempRoot = Join-Path $tempBase "ptrenderer-direct-pbr-$PID"
$tempRootFull = [IO.Path]::GetFullPath($tempRoot)
$tempPrefix = $tempBase.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
if (-not $tempRootFull.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Unsafe shader-test temp path: $tempRootFull"
}

function Invoke-NativeChecked {
    param(
        [Parameter(Mandatory)] [string]$FailureMessage,
        [Parameter(Mandatory)] [scriptblock]$Command
    )

    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw $FailureMessage
    }
}

function Copy-ShaderTextWithoutBom {
    param([Parameter(Mandatory)] [string]$RelativePath)

    $sourcePath = Join-Path $RepoRoot $RelativePath
    $targetPath = Join-Path $tempRoot $RelativePath
    $targetDirectory = Split-Path -Parent $targetPath
    New-Item -ItemType Directory -Force -Path $targetDirectory | Out-Null
    $text = [IO.File]::ReadAllText($sourcePath).TrimStart([char]0xfeff)
    [IO.File]::WriteAllText($targetPath, $text, [Text.UTF8Encoding]::new($false))
}

function Compile-GBufferShader {
    param(
        [Parameter(Mandatory)] [string]$RelativePath,
        [Parameter(Mandatory)] [string]$OutputName
    )

    $sourcePath = Join-Path $RepoRoot $RelativePath
    $source = [IO.File]::ReadAllText($sourcePath).TrimStart([char]0xfeff)
    $includeOffset = $source.IndexOf('#include', [StringComparison]::Ordinal)
    if ($includeOffset -lt 0) {
        throw "No include insertion point in $RelativePath"
    }

    $bindlessDeclarations = @'
layout(set = 0, binding = 0) uniform texture2D kTextures2D[];
layout(set = 0, binding = 1) uniform sampler kSamplers[];
vec4 textureBindless2D(uint textureId, uint samplerId, vec2 uvIn)
{
    return texture(nonuniformEXT(sampler2D(kTextures2D[textureId], kSamplers[samplerId])), uvIn);
}

'@

    $compiledSource = @'
#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference_uvec2 : require
#extension GL_EXT_debug_printf : enable
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_samplerless_texture_functions : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
'@ + "`n" + $source.Insert($includeOffset, $bindlessDeclarations)

    $temporarySource = Join-Path $tempRoot $RelativePath
    $temporaryDirectory = Split-Path -Parent $temporarySource
    New-Item -ItemType Directory -Force -Path $temporaryDirectory | Out-Null
    [IO.File]::WriteAllText($temporarySource, $compiledSource, [Text.UTF8Encoding]::new($false))

    $spv = Join-Path $tempRoot "$OutputName.spv"
    $assembly = Join-Path $tempRoot "$OutputName.spvasm"
    $includeArgument = "-I$tempRoot"
    Invoke-NativeChecked "Failed to compile $RelativePath" {
        & glslangValidator -V --target-env vulkan1.2 $includeArgument -S frag $temporarySource -o $spv
    }
    Invoke-NativeChecked "Invalid SPIR-V generated for $RelativePath" {
        & spirv-val --target-env vulkan1.2 $spv
    }
    Invoke-NativeChecked "Failed to disassemble $RelativePath" {
        & spirv-dis $spv -o $assembly
    }

    $sampleCount = @(Select-String -Path $assembly -Pattern 'OpFunctionCall %v4float %textureBindless2D').Count
    if ($sampleCount -ne 4) {
        throw "$RelativePath must sample only base-color, normal, metallic-roughness, and emissive textures; found $sampleCount sample sites"
    }
}

function Assert-SourcePattern {
    param(
        [Parameter(Mandatory)] [string]$RelativePath,
        [Parameter(Mandatory)] [string]$Pattern,
        [Parameter(Mandatory)] [string]$FailureMessage
    )

    $sourcePath = Join-Path $RepoRoot $RelativePath
    if (-not (Select-String -Path $sourcePath -Pattern $Pattern -Quiet)) {
        throw $FailureMessage
    }
}

function Assert-NoSourcePattern {
    param(
        [Parameter(Mandatory)] [string]$RelativePath,
        [Parameter(Mandatory)] [string]$Pattern,
        [Parameter(Mandatory)] [string]$FailureMessage
    )

    $sourcePath = Join-Path $RepoRoot $RelativePath
    if (Select-String -Path $sourcePath -Pattern $Pattern -Quiet) {
        throw $FailureMessage
    }
}

try {
    New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
    @(
        'PTRenderer/src/common.sp',
        'data/shaders/gltf/common_material.sp',
        'data/shaders/UtilsPBR.sp',
        'data/shaders/AlphaTest.sp'
    ) | ForEach-Object { Copy-ShaderTextWithoutBom $_ }

    Compile-GBufferShader 'PTRenderer/shaders/gbuffer_opaque.frag' 'gbuffer_opaque'
    Compile-GBufferShader 'PTRenderer/shaders/gbuffer_masked.frag' 'gbuffer_masked'

    Assert-NoSourcePattern 'shared/Scene/VtxData.h' 'normalScale' `
        'Bistro CPU materials must not store a constant normal scale'
    Assert-SourcePattern 'shared/Scene/VtxData.h' 'int\s+metallicRoughnessTexture\s*=\s*-1' `
        'CPU Material is missing the metallic-roughness texture index'
    Assert-NoSourcePattern 'shared/Scene/VtxData.h' '(?:occlusionStrength|occlusionTexture)' `
        'CPU Material must not expose an AO texture or strength'
    Assert-NoSourcePattern 'PTRenderer/src/scene/MeshUtils.h' `
        '(?:normalScale|AI_MATKEY_GLTF_TEXTURE_SCALE\(aiTextureType_NORMALS)' `
        'Bistro material import must not read a constant normal scale'
    Assert-SourcePattern 'PTRenderer/src/scene/MeshUtils.h' 'aiGetMaterialTexture\(M,\s*aiTextureType_METALNESS' `
        'Material import does not read the packed metallic-roughness texture'
    Assert-NoSourcePattern 'PTRenderer/src/scene/MeshUtils.h' '(?:aiTextureType_LIGHTMAP|occlusionTexture|occlusionStrength)' `
        'Material import/upload must not handle AO textures'

    foreach ($uploadPath in @('PTRenderer/src/scene/MeshUtils.h', 'PTRenderer/src/scene/VKMesh11Lazy.h')) {
        Assert-NoSourcePattern $uploadPath 'mat\.normalScale' `
            "$uploadPath still uploads the constant Bistro normal scale"
        Assert-SourcePattern $uploadPath 'surfacePropertiesTexture\s*=\s*getTextureFromCache\((?:mat|mtl)\.metallicRoughnessTexture\)' `
            "$uploadPath does not upload the metallic-roughness texture"
        Assert-NoSourcePattern $uploadPath '(?:mat|mtl)\.occlusionTexture' `
            "$uploadPath must not upload an AO texture"
    }
    foreach ($shaderPath in @('PTRenderer/shaders/gbuffer_opaque.frag', 'PTRenderer/shaders/gbuffer_masked.frag')) {
        Assert-NoSourcePattern $shaderPath 'metallicRoughnessNormalOcclusion\.z' `
            "$shaderPath still applies a per-material normal scale"
        Assert-NoSourcePattern $shaderPath '(?:tangentNormal|normalize\s*\(\s*normalSample\s*\*\s*2\.0\s*-\s*1\.0)' `
            "$shaderPath redundantly decodes and re-encodes the normal map before perturbNormal"
        Assert-NoSourcePattern $shaderPath `
            'roughness\s*=\s*clamp\s*\(\s*roughness\s*,\s*0\.045\s*,\s*1\.0\s*\)' `
            "$shaderPath redundantly clamps roughness before writing to an UNORM GBuffer"
    }
    Assert-SourcePattern 'shared/Scene/MergeUtil.cpp' 'replaceTexture\(i,\s*&m\.metallicRoughnessTexture\)' `
        'Merged scenes do not remap metallic-roughness texture indices'
    Assert-NoSourcePattern 'shared/Scene/MergeUtil.cpp' 'replaceTexture\(i,\s*&m\.occlusionTexture\)' `
        'Merged scenes must not remap an AO texture index'
    Assert-NoSourcePattern 'PTRenderer/src/RayTracing.h' `
        '(?:RayTracingShadowData|RayTracingInstanceMetadata|shadowDataBuffer|instanceMetadataBuffer|shadowDataAddress)' `
        'Opaque-only shadow queries must not allocate alpha-test metadata buffers'
    Assert-NoSourcePattern 'PTRenderer/src/RenderPasses.cpp' `
        'rt\.(?:indexBuffer|attributeBuffer|materialBuffer|instanceMetadataBuffer|shadowDataBuffer|shadowDataAddress)' `
        'Direct-PBR dispatch still depends on alpha-test shadow buffers'
    Assert-SourcePattern 'PTRenderer/src/RayTracing.h' `
        'material\.flags\s*&\s*sMaterialFlags_Transparent' `
        'Opaque-only shadow acceleration structures must exclude transparent materials'
    Assert-NoSourcePattern 'PTRenderer/src/RayTracing.h' `
        '(?:geometryFlags\s*=\s*lvk::AccelStructGeometryFlagBits_Opaque|const\s+bool\s+castsShadow|instanceCustomIndex\s*=\s*0)' `
        'RayTracing.h contains redundant changes already covered by LVK defaults or unused shadow metadata'
    $vulkanClasses = Get-Content -Raw -LiteralPath `
        (Join-Path $RepoRoot 'deps/src/lightweightvk/lvk/vulkan/VulkanClasses.cpp')
    $updateFunction = [regex]::Match(
        $vulkanClasses,
        '(?s)void\s+lvk::CommandBuffer::cmdUpdateBuffer\b.*?(?=\nvoid\s+lvk::CommandBuffer::)'
    ).Value
    if ($updateFunction -notmatch `
        '(?m)^\s*dstStage\s*\|=\s*VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT\s*\|\s*VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT') {
        throw 'Storage-buffer updates are not made visible to compute and fragment shaders'
    }
    $primarySource = Join-Path $RepoRoot 'PTRenderer/shaders/primary_ray_shade.comp'
    Assert-NoSourcePattern 'PTRenderer/shaders/primary_ray_shade.comp' `
        '(?m)^\s*n\s*=\s*-n' `
        'Direct PBR must not flip normals toward the camera'
    Assert-NoSourcePattern 'PTRenderer/src/common.sp' `
        'reconstructWorldPosition' `
        'The clip-to-world helper still uses its old reconstruction name'
    Assert-SourcePattern 'PTRenderer/src/common.sp' `
        'vec3\s+clipToWorld\s*\(\s*mat4\s+transform' `
        'The clip-to-world helper is missing or retains an ambiguous matrix parameter name'
    Assert-SourcePattern 'PTRenderer/shaders/primary_ray_shade.comp' `
        'clipToWorld\s*\(\s*pc\.clipToWorld\s*,\s*uv\s*,\s*depth\s*\)' `
        'Direct PBR does not call the renamed clip-to-world helper'
    Assert-SourcePattern 'PTRenderer/shaders/primary_ray_shade.comp' `
        'rayQueryInitializeEXT\s*\(\s*query\s*,\s*kTLAS\s*\[\s*0\s*\]' `
        'The single TLAS must use descriptor slot zero'
    Assert-NoSourcePattern 'PTRenderer/shaders/primary_ray_shade.comp' `
        'pc\.tlas' `
        'The shader still treats an acceleration-structure pool handle as a TLAS descriptor index'
    Assert-NoSourcePattern 'PTRenderer/src/RenderPasses.cpp' `
        'pc\.tlas' `
        'The CPU still pushes an acceleration-structure pool handle as a TLAS descriptor index'
    Assert-SourcePattern 'PTRenderer/src/DemoConfig.h' `
        'bool\s+enabled\s*=\s*true' `
        'Directional lighting must be enabled by default in LightParams'
    Assert-SourcePattern 'PTRenderer/src/DemoUI.cpp' `
        'ImGui::Checkbox\(\s*"Directional light"\s*,\s*&gSettings\.light\.enabled\s*\)' `
        'The controls UI is missing the directional-light toggle'
    Assert-SourcePattern 'PTRenderer/src/SceneResources.cpp' `
        '\.enabled\s*=\s*light\.enabled' `
        'LightFrame does not receive the directional-light enabled state'
    Assert-SourcePattern 'PTRenderer/src/RenderPasses.cpp' `
        'pc\.lightDir\s*=\s*vec4\(\s*lightFrame\.dir\s*,\s*lightFrame\.enabled\s*\?\s*1\.0f\s*:\s*0\.0f\s*\)' `
        'The directional-light enabled state is not packed into lightDir.w'
    Assert-NoSourcePattern 'PTRenderer/src/RenderPasses.cpp' `
        '^\s*if\s*\(\s*!rt\.valid\s*\(' `
        'PrimaryRayPass must not keep a ray-tracing-unavailable fallback'
    Assert-NoSourcePattern 'PTRenderer/src/main.cpp' `
        '\bcurrentColor\b' `
        'The fixed lighting output path must not keep a dynamic currentColor target'
    Assert-SourcePattern 'PTRenderer/src/main.cpp' `
        'hdr\.execute\(\s*buf\s*,\s*targets\.lightingColor' `
        'HDR must consume the fixed direct-PBR lighting target'
    Assert-SourcePattern 'PTRenderer/src/RenderPasses.cpp' `
        'pc\.punctualLightBuffer\s*=\s*ctx->gpuAddress\(\s*punctualLightBuffer\s*\)' `
        'The punctual-light buffer reference must receive a GPU address'
    Assert-NoSourcePattern 'PTRenderer/src/RenderPasses.cpp' `
        'pc\.punctualLightBuffer\s*=\s*punctualLightBuffer\.index\(\s*\)' `
        'A bindless buffer index cannot be used as a buffer-reference address'
    Assert-SourcePattern 'PTRenderer/src/RenderPasses.cpp' `
        'sizeof\(PunctualLightBufferHeader\)' `
        'Punctual-light data must begin after its 16-byte std430 header'
    Assert-SourcePattern 'PTRenderer/src/RenderPasses.cpp' `
        'loadTexture\(\s*ctx\s*,\s*"data/blue_noise_128x128x64\.png"\s*\)' `
        'The reserved blue-noise resource must remain loaded for later use'
    $primaryShaderText = Get-Content -Raw -LiteralPath $primarySource
    if ($primaryShaderText -notmatch `
        '(?s)if\s*\(\s*pc\.lightDir\.w\s*>\s*0\.0\s*\).*?traceShadow\s*\(') {
        throw 'Direct PBR and its shadow ray must be guarded by the directional-light toggle'
    }
    if ($primaryShaderText -notmatch `
        '(?s)if\s*\(\s*nDotView\s*<=\s*0\.0\s*\)\s*\{\s*imageStore\([^;]+scene[^;]*;\s*return\s*;\s*\}') {
        throw 'Back-facing shading normals must preserve scene color and skip direct lighting'
    }
    if ($primaryShaderText -notmatch `
        '(?s)light\.color\s*\*\s*light\.intensity\s*/.*?max\(\s*distanceSquared\s*,\s*radiusSquared\s*\).*?float\(\s*punctualLightCount\s*\)') {
        throw 'A sampled punctual light must use intensity, inverse-square attenuation, radius clamping, and 1/pdf compensation'
    }
    if ($primaryShaderText -notmatch `
        '(?s)shadowDistance\s*=\s*length\(\s*shadowToLight\s*\).*?traceShadow\([^;]+shadowDistance') {
        throw 'A punctual-light shadow ray must terminate at the sampled light'
    }
    if ($primaryShaderText -notmatch `
        'rand\(\s*randomSeed\s*\)\s*\*\s*float\(\s*punctualLightCount\s*\)') {
        throw 'Punctual-light selection must use uniform frame-indexed random sampling'
    }
    if ($primaryShaderText -notmatch `
        'uvec3\s+randomSeed\s*=\s*uvec3\(\s*uvec2\(pixel\)\s*,\s*pc\.frameIndex\s*\)') {
        throw 'Uniform light selection must hash pixel coordinates and frame index independently'
    }
    $primarySpv = Join-Path $tempRoot 'primary_ray_shade.spv'
    $primaryAssembly = Join-Path $tempRoot 'primary_ray_shade.spvasm'
    $repoIncludeArgument = "-I$RepoRoot"
    Invoke-NativeChecked 'Failed to compile primary_ray_shade.comp' {
        & glslangValidator -V --target-env vulkan1.2 $repoIncludeArgument -S comp $primarySource -o $primarySpv
    }
    Invoke-NativeChecked 'Invalid primary-ray SPIR-V' {
        & spirv-val --target-env vulkan1.2 $primarySpv
    }
    Invoke-NativeChecked 'Failed to disassemble primary-ray SPIR-V' {
        & spirv-dis $primarySpv -o $primaryAssembly
    }

    $reflection = (& spirv-cross $primarySpv --reflect | ConvertFrom-Json)
    if ($LASTEXITCODE -ne 0) {
        throw 'Failed to reflect primary-ray SPIR-V'
    }
    $pushTypeName = $reflection.push_constants[0].type
    $pushType = $reflection.types.PSObject.Properties[$pushTypeName].Value
    $actualMembers = @($pushType.members | ForEach-Object { $_.name })
    $expectedMembers = @(
        'clipToWorld', 'lightDir', 'cameraPos', 'sceneColor', 'lightingColor',
        'gbuffer1', 'gbuffer2', 'gbuffer3', 'depth', 'smpl', 'frameIndex', 'texBlueNoise', 'punctualLights'
    )
    if (($actualMembers -join ',') -ne ($expectedMembers -join ',')) {
        throw "Unexpected direct-PBR push constants: $($actualMembers -join ', ')"
    }
    $expectedOffsets = @(0, 64, 80, 96, 100, 104, 108, 112, 116, 120, 124, 128, 136)
    $actualOffsets = @($pushType.members | ForEach-Object { [int]$_.offset })
    if (($actualOffsets -join ',') -ne ($expectedOffsets -join ',')) {
        throw "Unexpected direct-PBR push-constant offsets: $($actualOffsets -join ', ')"
    }
    $reflectedBlockSize = [int]$pushType.members[-1].offset + 8
    if ($reflectedBlockSize -ne 144) {
        throw "Unexpected direct-PBR push-constant size: $reflectedBlockSize"
    }

    $imageProperty = $reflection.PSObject.Properties['separate_images']
    $separateImages = if ($null -eq $imageProperty) { @() } else { @($imageProperty.Value) }
    if ($separateImages | Where-Object { $_.type -eq 'textureCube' }) {
        throw 'Direct-only PBR shader still declares a cube-map lighting resource'
    }

    $shadowCallCount = @(Select-String -Path $primaryAssembly -Pattern 'OpFunctionCall %bool %traceShadow').Count
    if ($shadowCallCount -ne 1) {
        throw "Sampled punctual lighting must call traceShadow once; found $shadowCallCount call sites"
    }
    if (Select-String -Path $primaryAssembly -Pattern 'OpRayQueryConfirmIntersectionKHR') {
        throw 'Opaque-only shadow queries must not manually confirm masked candidates'
    }
    if (-not (Select-String -Path $primarySource -Pattern 'gl_RayFlagsOpaqueEXT')) {
        throw 'Opaque-only shadow query does not force all shadow geometry opaque'
    }
    if (Select-String -Path $primarySource -Pattern '(?:ShadowData|maskedCandidateCastsShadow|shadowVertexUV)') {
        throw 'Opaque-only shadow shader still contains masked-material lookup code'
    }
    if (Select-String -Path $primarySource -Pattern 'depth\s*>=\s*0\.9999') {
        throw 'Depth epsilon still misclassifies distant geometry as background'
    }
    if (-not (Select-String -Path $primarySource -Pattern `
        'roughness\s*=\s*clamp\s*\(\s*g2\.z\s*,\s*0\.045\s*,\s*1\.0\s*\)')) {
        throw 'GGX lighting must retain its single numerical roughness clamp'
    }
    if (-not (Select-String -Path $primarySource -Pattern `
        'normalize\s*\(\s*pc\.cameraPos\.xyz\s*-\s*worldPos\s*\)')) {
        throw 'View direction must be computed explicitly from camera position and shaded world position'
    }
    if (Select-String -Path $primarySource -Pattern '(?:nearPoint|farPoint)') {
        throw 'View direction still reconstructs redundant near/far points'
    }
    if (-not (Select-String -Path $primaryAssembly -Pattern 'OpConstant %float 0\.0?39999|OpConstant %float 0\.04')) {
        throw 'Direct-only PBR SPIR-V is missing the dielectric F0 constant 0.04'
    }
    if (Select-String -Path $primaryAssembly -Pattern 'OpConstant %float 0\.0?79999|OpConstant %float 0\.08') {
        throw 'Legacy 0.08 * specularFactor dielectric F0 remains in direct-only PBR'
    }

    Write-Output 'Direct PBR shader contract passed.'
}
finally {
    if (Test-Path -LiteralPath $tempRootFull) {
        Remove-Item -LiteralPath $tempRootFull -Recurse -Force
    }
}
