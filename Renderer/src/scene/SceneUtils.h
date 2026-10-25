#pragma once

#include <algorithm>
#include <execution>
#include <filesystem>
#include <numeric>
#include <sstream>
#include <thread>

#include <ktx-software/lib/src/gl_format.h>
#include <ktx-software/lib/src/vkformat_enum.h>
#include <ktx.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_resize2.h"
#include "stb_image_write.h"

#include "scene/MeshUtils.h"
#include "shared/UtilsGLTF.h"

// these macros can be redefined externally
#if !defined(DEMO_TEXTURE_MAX_SIZE) && !defined(DEMO_TEXTURE_CACHE_FOLDER)
#define DEMO_TEXTURE_MAX_SIZE 512
#define DEMO_TEXTURE_CACHE_FOLDER ".cache/out_textures/"
#endif

#if defined(DEMO_TEXTURE_COMPRESSION_ASTC)
#define DEMO_TEXTURE_COMPRESSION_NAME "ASTC"
#define DEMO_TEXTURE_CACHE_SUFFIX "__rescaled_astc"
#else
#define DEMO_TEXTURE_COMPRESSION_NAME "BC7"
#define DEMO_TEXTURE_CACHE_SUFFIX "__rescaled"
#define DEMO_TEXTURE_GL_INTERNAL_FORMAT GL_COMPRESSED_RGBA_BPTC_UNORM
#define DEMO_TEXTURE_VK_FORMAT VK_FORMAT_BC7_UNORM_BLOCK
#endif

std::string logPrefix(int ofs)
{
    return std::string(ofs, '\t');
}

std::string mat4ToLogString(const aiMatrix4x4 &m)
{
    if (m.IsIdentity())
    {
        return " Identity";
    }

    std::ostringstream out;
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            out << m[i][j] << " ;";
        }
    }

    return out.str();
}

// find a file in directory which "almost" coincides with the origFile (their lowercase versions coincide)
std::string findSubstitute(const std::string &origFile)
{
    namespace fs = std::filesystem;

    // Make absolute path
    auto apath = fs::absolute(fs::path(origFile));
    // Absolute lowercase filename [we compare with it]
    auto afile = lowercaseString(apath.filename().string());
    // Directory where in which the file should be
    auto dir = fs::path(origFile).remove_filename();

    // Iterate each file non-recursively and compare lowercase absolute path with 'afile'
    for (auto &p : fs::directory_iterator(dir))
    {
        if (afile == lowercaseString(p.path().filename().string()))
        {
            return p.path().string();
        }
    }

    return std::string{};
}

std::string fixTextureFile(const std::string &file)
{
    // TODO: check the findSubstitute() function
    return std::filesystem::exists(file) ? file : findSubstitute(file);
}

std::string convertTexture(const std::string &file, const std::string &basePath,
                           const std::unordered_map<std::string, uint32_t> &opacityMapIndices,
                           const std::vector<std::string> &opacityMaps, bool sRGB)
{
    LVK_PROFILER_FUNCTION();

    const int maxNewWidth = DEMO_TEXTURE_MAX_SIZE;
    const int maxNewHeight = DEMO_TEXTURE_MAX_SIZE;

    namespace fs = std::filesystem;

    if (!fs::exists(DEMO_TEXTURE_CACHE_FOLDER))
    {
        fs::create_directories(DEMO_TEXTURE_CACHE_FOLDER);
    }

    const std::string srcFile = replaceAll(basePath + file, "\\", "/");
    const std::string newFile = std::string(DEMO_TEXTURE_CACHE_FOLDER) +
                                lowercaseString(replaceAll(replaceAll(srcFile, "..", "__"), "/", "__") +
                                                std::string(DEMO_TEXTURE_CACHE_SUFFIX) +
                                                (sRGB ? "_srgb_v2" : "_linear_v2")) +
                                std::string(".ktx");

    // Material-import revisions do not require recompressing unchanged, versioned textures.
    const std::string resolvedSource = fixTextureFile(srcFile);
    if (fs::exists(newFile) && fs::exists(resolvedSource))
    {
        const auto cacheTime = fs::last_write_time(newFile);
        bool current = cacheTime >= fs::last_write_time(resolvedSource);
        if (auto it = opacityMapIndices.find(file); it != opacityMapIndices.end())
        {
            const std::string mask = fixTextureFile(replaceAll(basePath + opacityMaps[it->second], "\\", "/"));
            current = current && fs::exists(mask) && cacheTime >= fs::last_write_time(mask);
        }
        if (current)
            return newFile;
    }

    // load this image
    int origWidth, origHeight, texChannels;
    stbi_uc *pixels = stbi_load(resolvedSource.c_str(), &origWidth, &origHeight, &texChannels, STBI_rgb_alpha);
    uint8_t *src = pixels;
    texChannels = STBI_rgb_alpha;

    SCOPE_EXIT
    {
        if (pixels)
        {
            stbi_image_free(pixels);
        }
    };

    std::vector<uint8_t> tmpImage(maxNewWidth * maxNewHeight * 4);

    if (!src)
    {
        LLOGW("Failed to load [%s] texture\n", srcFile.c_str());
        origWidth = maxNewWidth;
        origHeight = maxNewHeight;
        texChannels = STBI_rgb_alpha;
        src = tmpImage.data();
    }
    else
    {
        LLOGL("Loaded [%s] %dx%d texture with %d channels\n", srcFile.c_str(), origWidth, origHeight, texChannels);
    }

    if (opacityMapIndices.count(file) > 0)
    {
        const std::string opacityMapFile = replaceAll(basePath + opacityMaps[opacityMapIndices.at(file)], "\\", "/");
        int opacityWidth, opacityHeight;
        stbi_uc *opacityPixels =
            stbi_load(fixTextureFile(opacityMapFile).c_str(), &opacityWidth, &opacityHeight, nullptr, 1);

        if (!opacityPixels)
        {
            LLOGW("Failed to load opacity mask [%s]\n", opacityMapFile.c_str());
        }

        assert(opacityPixels);
        assert(origWidth == opacityWidth);
        assert(origHeight == opacityHeight);

        // store the opacity mask in the alpha component of this image
        if (opacityPixels)
        {
            for (int y = 0; y != opacityHeight; y++)
            {
                for (int x = 0; x != opacityWidth; x++)
                {
                    src[(y * opacityWidth + x) * texChannels + 3] = opacityPixels[y * opacityWidth + x];
                }
            }
        }

        stbi_image_free(opacityPixels);
    }

    const int newW = std::min(origWidth, maxNewWidth);
    const int newH = std::min(origHeight, maxNewHeight);

    LLOGL("Compressing texture to %s...\n", DEMO_TEXTURE_COMPRESSION_NAME);

    const uint32_t numMipLevels = lvk::calcNumMipLevels(newW, newH);

    // create a KTX2 texture for RGBA data
    ktxTextureCreateInfo createInfoKTX2 = {
        .glInternalformat = static_cast<uint32_t>(sRGB ? GL_SRGB8_ALPHA8 : GL_RGBA8),
        .vkFormat = static_cast<uint32_t>(sRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM),
        .baseWidth = (uint32_t)newW,
        .baseHeight = (uint32_t)newH,
        .baseDepth = 1u,
        .numDimensions = 2u,
        .numLevels = numMipLevels,
        .numLayers = 1u,
        .numFaces = 1u,
        .generateMipmaps = KTX_FALSE,
    };
    ktxTexture2 *textureKTX2 = nullptr;
    (void)LVK_VERIFY(ktxTexture2_Create(&createInfoKTX2, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &textureKTX2) ==
                     KTX_SUCCESS);

    int w = newW;
    int h = newH;

    // generate custom mip-pyramid
    for (uint32_t i = 0; i != numMipLevels; ++i)
    {
        size_t offset = 0;
        ktxTexture_GetImageOffset(ktxTexture(textureKTX2), i, 0, 0, &offset);

        uint8_t *dst = ktxTexture_GetData(ktxTexture(textureKTX2)) + offset;
        if (sRGB)
            stbir_resize_uint8_srgb(src, origWidth, origHeight, 0, dst, w, h, 0, STBIR_RGBA);
        else
            stbir_resize_uint8_linear(src, origWidth, origHeight, 0, dst, w, h, 0, STBIR_RGBA_NO_AW);

        h = h > 1 ? h >> 1 : 1;
        w = w > 1 ? w >> 1 : 1;
    }

#if defined(DEMO_TEXTURE_COMPRESSION_ASTC)
    // Compress to ASTC for Android. Keep the explicit mip pyramid above.
    ktxAstcParams astcParams = {};
    astcParams.structSize = sizeof(astcParams);
    astcParams.blockDimension = DEMO_TEXTURE_KTX_FORMAT;
    astcParams.mode = KTX_PACK_ASTC_ENCODER_MODE_LDR;
    astcParams.qualityLevel = KTX_PACK_ASTC_QUALITY_LEVEL_MEDIUM;
    astcParams.threadCount = std::max(1u, std::thread::hardware_concurrency());
    (void)LVK_VERIFY(ktxTexture2_CompressAstcEx(textureKTX2, &astcParams) == KTX_SUCCESS);
#else
    // compress to Basis and transcode to BC7
    (void)LVK_VERIFY(ktxTexture2_CompressBasis(textureKTX2, 255) == KTX_SUCCESS);
    (void)LVK_VERIFY(ktxTexture2_TranscodeBasis(textureKTX2, KTX_TTF_BC7_RGBA, 0) == KTX_SUCCESS);
#endif

    // KTX1 records the transfer function in glInternalformat (vkFormat is a KTX2 field).
    const uint32_t compressedFormat = [sRGB]() -> uint32_t
    {
        if (!sRGB)
            return DEMO_TEXTURE_GL_INTERNAL_FORMAT;
        switch (DEMO_TEXTURE_GL_INTERNAL_FORMAT)
        {
        case GL_COMPRESSED_RGBA_BPTC_UNORM: return GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM;
        case GL_COMPRESSED_RGBA_ASTC_4x4_KHR: return GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR;
        case GL_COMPRESSED_RGBA_ASTC_6x6_KHR: return GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR;
        case GL_COMPRESSED_RGBA_ASTC_8x8_KHR: return GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR;
        default: assert(false); return DEMO_TEXTURE_GL_INTERNAL_FORMAT;
        }
    }();
    ktxTextureCreateInfo createInfoKTX1 = {
        .glInternalformat = compressedFormat,
        .baseWidth = (uint32_t)newW,
        .baseHeight = (uint32_t)newH,
        .baseDepth = 1u,
        .numDimensions = 2u,
        .numLevels = numMipLevels,
        .numLayers = 1u,
        .numFaces = 1u,
        .generateMipmaps = KTX_FALSE,
    };
    ktxTexture1 *textureKTX1 = nullptr;
    (void)LVK_VERIFY(ktxTexture1_Create(&createInfoKTX1, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &textureKTX1) ==
                     KTX_SUCCESS);

    for (uint32_t i = 0; i != numMipLevels; ++i)
    {
        size_t offset1 = 0;
        (void)LVK_VERIFY(ktxTexture_GetImageOffset(ktxTexture(textureKTX1), i, 0, 0, &offset1) == KTX_SUCCESS);
        size_t offset2 = 0;
        (void)LVK_VERIFY(ktxTexture_GetImageOffset(ktxTexture(textureKTX2), i, 0, 0, &offset2) == KTX_SUCCESS);
        memcpy(ktxTexture_GetData(ktxTexture(textureKTX1)) + offset1,
               ktxTexture_GetData(ktxTexture(textureKTX2)) + offset2,
               ktxTexture_GetImageSize(ktxTexture(textureKTX1), i));
    }

    ktxTexture_WriteToNamedFile(ktxTexture(textureKTX1), newFile.c_str());
    ktxTexture_Destroy(ktxTexture(textureKTX1));
    ktxTexture_Destroy(ktxTexture(textureKTX2));

    return newFile;
}

void convertAndDownscaleAllTextures(std::vector<Material> &materials, const std::string &basePath,
                                    std::vector<std::string> &files, std::vector<std::string> &opacityMaps)
{
    std::vector<uint8_t> linearUse(files.size(), 0);
    std::vector<uint8_t> sRGB(files.size(), 0);
    for (const Material &m : materials)
        if (m.normalTexture >= 0)
            linearUse.at(m.normalTexture) = 1;

    // A source image may be used as both color and data: keep separate cached textures.
    std::unordered_map<int, int> colorCopies;
    auto markColor = [&](int &index)
    {
        if (index < 0)
            return;
        if (linearUse.at(index))
        {
            auto [it, inserted] = colorCopies.try_emplace(index, static_cast<int>(files.size()));
            if (inserted)
            {
                const std::string source = files.at(index);
                files.push_back(source);
                sRGB.push_back(1);
                linearUse.push_back(0);
            }
            index = it->second;
        }
        sRGB.at(index) = 1;
    };
    for (Material &m : materials)
    {
        markColor(m.baseColorTexture);
        markColor(m.emissiveTexture);
    }

    std::unordered_map<std::string, uint32_t> opacityMapIndices(files.size());

    for (const auto &m : materials)
    {
        if (m.opacityTexture != -1 && m.baseColorTexture != -1)
        {
            opacityMapIndices[files[m.baseColorTexture]] = (uint32_t)m.opacityTexture;
        }
    }

    std::vector<size_t> indices(files.size());
    std::iota(indices.begin(), indices.end(), size_t(0));
    auto converter = [&](size_t index)
    {
        files[index] = convertTexture(files[index], basePath, opacityMapIndices, opacityMaps, sRGB[index] != 0);
    };

#if defined(__cpp_lib_execution)
    std::for_each(std::execution::par, indices.begin(), indices.end(), converter);
#else
    std::for_each(indices.begin(), indices.end(), converter);
#endif
}

void traverse(const aiScene *sourceScene, Scene &scene, aiNode *N, int parent, int depth)
{
    int newNode = addNode(scene, parent, depth);

    if (N->mName.C_Str())
    {
        const std::string prefix = logPrefix(depth);
        LLOGL("%sNode[%d].name = %s\n", prefix.c_str(), newNode, N->mName.C_Str());

        const uint32_t stringID = (uint32_t)scene.nodeNames.size();
        scene.nodeNames.push_back(std::string(N->mName.C_Str()));
        scene.nameForNode[newNode] = stringID;
    }

    for (size_t i = 0; i < N->mNumMeshes; i++)
    {
        const int newSubNode = addNode(scene, newNode, depth + 1);

        const uint32_t stringID = (uint32_t)scene.nodeNames.size();
        scene.nodeNames.push_back(std::string(N->mName.C_Str()) + "_Mesh_" + std::to_string(i));
        scene.nameForNode[newSubNode] = stringID;

        const int mesh = (int)N->mMeshes[i];
        scene.meshForNode[newSubNode] = mesh;
        scene.materialForNode[newSubNode] = sourceScene->mMeshes[mesh]->mMaterialIndex;

        const std::string prefix = logPrefix(depth);
        LLOGL("%sNode[%d].SubNode[%d].mesh     = %d\n", prefix.c_str(), newNode, newSubNode, (int)mesh);
        LLOGL("%sNode[%d].SubNode[%d].material = %d\n", prefix.c_str(), newNode, newSubNode,
              sourceScene->mMeshes[mesh]->mMaterialIndex);

        scene.globalTransform[newSubNode] = glm::mat4(1.0f);
        scene.localTransform[newSubNode] = glm::mat4(1.0f);
    }

    scene.globalTransform[newNode] = glm::mat4(1.0f);
    scene.localTransform[newNode] = aiMatrix4x4ToMat4(N->mTransformation);

    if (N->mParent != nullptr)
    {
        const std::string prefix = logPrefix(depth);
        const std::string localTransform = mat4ToLogString(N->mTransformation);
        LLOGL("%s\tNode[%d].parent         = %s\n", prefix.c_str(), newNode, N->mParent->mName.C_Str());
        LLOGL("%s\tNode[%d].localTransform = %s\n", prefix.c_str(), newNode, localTransform.c_str());
    }

    for (unsigned int n = 0; n < N->mNumChildren; n++)
    {
        traverse(sourceScene, scene, N->mChildren[n], newNode, depth + 1);
    }
}

void loadMeshFile(const char *fileName, MeshData &meshData, Scene &ourScene, bool generateLODs)
{
    LLOGL("Loading '%s'...\n", fileName);

    const unsigned int flags = 0 | aiProcess_JoinIdenticalVertices | aiProcess_Triangulate |
                               aiProcess_GenSmoothNormals | aiProcess_LimitBoneWeights | aiProcess_SplitLargeMeshes |
                               aiProcess_ImproveCacheLocality | aiProcess_RemoveRedundantMaterials |
                               aiProcess_FindDegenerates | aiProcess_FindInvalidData | aiProcess_GenUVCoords;

    const aiScene *scene = aiImportFile(fileName, flags);

    if (!scene || !scene->HasMeshes())
    {
        LLOGW("Unable to load '%s'\n", fileName);
        exit(255);
    }

    meshData.meshes.reserve(scene->mNumMeshes);
    meshData.boxes.reserve(scene->mNumMeshes);

    uint32_t indexOffset = 0;
    uint32_t vertexOffset = 0;

    for (unsigned int i = 0; i != scene->mNumMeshes; i++)
    {
        LLOGL("Converting meshes %u/%u...\n", i + 1, scene->mNumMeshes);
        meshData.meshes.push_back(convertAIMesh(scene->mMeshes[i], meshData, indexOffset, vertexOffset, generateLODs));
    }

    // extract base model path
    const std::size_t pathSeparator = std::string(fileName).find_last_of("/\\");
    const std::string basePath =
        (pathSeparator != std::string::npos) ? std::string(fileName).substr(0, pathSeparator + 1) : std::string();

    std::vector<std::string> opacityMaps;

    for (unsigned int i = 0; i != scene->mNumMaterials; i++)
    {
        LLOGL("Converting materials %u/%u...\n", i + 1, scene->mNumMaterials);
        const aiMaterial *m = scene->mMaterials[i];
        ourScene.materialNames.push_back(m->GetName().C_Str());
        meshData.materials.push_back(convertAIMaterial(m, meshData.textureFiles, opacityMaps));
    }

    // texture processing, rescaling and packing
    convertAndDownscaleAllTextures(meshData.materials, basePath, meshData.textureFiles, opacityMaps);

    recalculateBoundingBoxes(meshData);

    // scene hierarchy conversion
    traverse(scene, ourScene, scene->mRootNode, -1, 0);
}
