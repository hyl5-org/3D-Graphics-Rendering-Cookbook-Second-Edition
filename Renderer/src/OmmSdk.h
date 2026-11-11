#pragma once

#include <lvk/LVK.h>

#ifdef COOKBOOK_WITH_RTX_OMM
#include <omm.h>
#include "shared/Scene/VtxData.h"
#include <memory>
#include <unordered_map>
#endif

namespace FinalDemo
{

struct OmmGeometry
{
    lvk::Holder<lvk::AccelStructHandle> micromap;
    lvk::Holder<lvk::BufferHandle> indices;
};

#ifdef COOKBOOK_WITH_RTX_OMM
class OmmBaker
{
  public:
    explicit OmmBaker(const std::unique_ptr<lvk::IContext> &ctx);
    ~OmmBaker();
    OmmBaker(const OmmBaker &) = delete;
    OmmBaker &operator=(const OmmBaker &) = delete;

    OmmGeometry bake(const MeshData &meshData, const Mesh &mesh, uint32_t indexCount);

  private:
    ommCpuTexture getTexture(const MeshData &meshData, uint32_t materialId);

    const std::unique_ptr<lvk::IContext> &ctx_;
    ommBaker baker_ = nullptr;
    lvk::Holder<lvk::ShaderModuleHandle> readAlphaShader_;
    lvk::Holder<lvk::ComputePipelineHandle> readAlpha_;
    std::unordered_map<uint32_t, ommCpuTexture> textures_;
    uint8_t maxSubdivisionLevel_ = 0;
    uint32_t maxMicromapTriangles_ = 0;
};
#endif

} // namespace FinalDemo
