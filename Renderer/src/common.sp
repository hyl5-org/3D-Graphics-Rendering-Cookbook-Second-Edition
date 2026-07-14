//

#include <data/shaders/gltf/common_material.sp>
#include <Renderer/shaders/common_oit.sp>

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
  vec4 lightColorIntensity;
  uint shadowTexture;
  uint shadowSampler;
  uint frameIndex;
  float iblIntensity;
};

struct OIT {
  AtomicCounter atomicCounter;
  TransparencyListsBuffer oitLists;
  uint texHeadsOIT;
  uint maxOITFragments;
};

layout(push_constant) uniform PerFrameData {
  mat4 viewProj[2];
  vec4 cameraPos[2];
  TransformBuffer transforms;
  DrawDataBuffer drawData;
  MaterialBuffer materials;
  OIT oit;
  LightBuffer light; // one directional light
  uint texSkybox;
  uint texSkyboxIrradiance;
} pc;
