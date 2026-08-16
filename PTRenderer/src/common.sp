//

#include <data/shaders/gltf/common_material.sp>

struct DrawData {
  uint transformId;
  uint materialId;
};

layout(std430, buffer_reference) readonly buffer TransformBuffer {
  mat4 model[];
};

layout(std430, buffer_reference) readonly buffer DrawDataBuffer {
  DrawData dd[];
};

layout(std430, buffer_reference) readonly buffer MaterialBuffer {
  MetallicRoughnessDataGPU material[];
};

layout(std430, buffer_reference) buffer AtomicCounter {
  uint numFragments;
};

layout(std430, buffer_reference) readonly buffer LightBuffer {
  mat4 viewProjBias;
  vec4 lightDir;
  uint frameIndex;
};

layout(push_constant) uniform PerFrameData {
  mat4 viewProj;
  vec4 cameraPos;
  TransformBuffer transforms;
  DrawDataBuffer drawData;
  MaterialBuffer materials;
  LightBuffer light; // one directional light
  uint texSkybox;
  uint texSkyboxIrradiance;
} pc;
