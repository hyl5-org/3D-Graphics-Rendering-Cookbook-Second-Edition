# 使用说明

基于https://github.com/PacktPublishing/3D-Graphics-Rendering-Cookbook-Second-Edition 开发，原仓库依赖meta lightweightvk
https://vulkan.org/user/pages/09.events/vulkanised-2025/T7-Roman-Kuznetsov-Meta.pdf


## Feature

- Deferred Shading (GBuffer: albedo + octahedron normal + metallic/roughness + depth)
- RT Shadow/AO
- Shadow Map (directional light)
- CPU/GPU(deprecated) 视锥剔除
- HDR / Bloom / Eye Adaptation / OIT
- 全 Bindless / Indirect Draw
- meshoptimizer / 纹理压缩 / 场景管理 / 相机
- Profiler (Tracy CPU/GPU)
- 框架封装了barrier/descriptor等语义，降低渲染开发的难度

## 渲染管线流程

```
CPU Update (camera, light, culling)
    ↓
GPU Frustum Culling (compute)
    ↓
Shadow Map (render)
    ↓
GBuffer Pass (render, MRT x4)
    ↓
RT Shadow/AO (render)
    ↓
RT Denoise (compute, temporal + spatial)
    ↓
Lighting Pass (render)
    ↓
Skybox (render)
    ↓
Transparent / OIT (render + combine)
    ↓
HDR Post Process (bloom + adaptation + tonemap)
    ↓
ImGui
```

## 依赖

需要本机已安装：

- Python 3
- Vulkan SDK https://vulkan.lunarg.com/sdk/home
- Visual Studio C++ 工具链
- Android SDK / NDK / adb / JDK

首次拉依赖：

```powershell
python deploy_deps.py
```

Android NDK 路径读取 `ANDROID_NDK`，没有的话读取 `NDK_ROOT`。

## 源码编译

Windows 和 Android 都统一使用仓库根目录下的 `build`。

## Windows

配置：

```powershell
cmake -S . -B build
```

进入 `build`，用 Visual Studio 打开 `vrgraphics.slnx`，编译/运行 `Renderer`

## Android 打包部署

配置并生成 Android Gradle 工程：

```powershell
cmake -S . -B build
```
打包：

```powershell
cd build\android\Renderer
.\gradlew.bat :app:assembleDebug
```

`assembleDebug` 使用 debug key，APK 保持 `debuggable` / `jniDebuggable`，native 代码按 `RelWithDebInfo` 构建并通过 `keepDebugSymbols` 保留调试符号，便于 profile 和 crash 栈解析。

安卓渲染使用ASTC纹理格式，建议在 Windows 上预生成 Android 可用的纹理压缩格式，图片会默认处理成 ASTC 6x6 + mipmap + KTX，供 Android Vulkan 直接加载。

```
cmake --build build --target Renderer_GenerateAndroidTextureCache --config Release
```

安装apk，部署运行资源到设备：

```powershell
adb install -r app\build\outputs\apk\debug\app-debug.apk
python Renderer\deploy_android_content.py
```


检查设备上资源是否正常推送

```text
adb shell ls /sdcard/vrgraphics/ -a
// 缓存数据
/sdcard/vrgraphics/.cache/ch11_bistro_android.meshes
/sdcard/vrgraphics/.cache/ch11_bistro_android.materials
/sdcard/vrgraphics/.cache/ch11_bistro_android.scene
/sdcard/vrgraphics/.cache/out_textures_11_astc/
/sdcard/vrgraphics/deps/src/bistro/Exterior/exterior.obj
...
其他资源/shader等
```

## Android 调试

- 安装AGDE https://developer.android.com/games/agde
- 再VS项目中添加 Android 平台，参考 https://developer.android.com/games/agde/adapt-existing-project?hl=zh-cn “添加 Android 平台”
- 拷贝调试符号 'build\android\Renderer\app\build\intermediates\cxx\Debug\4k3s2u4q\obj\arm64-v8a'到'build/Debug/Android-arm64-v8a/arm64-v8a'里
- 启动app，attach

> 原生android构建需要维护三方依赖，和cmake冲突，只能attach，参考 “此外，由于所有 C/C++ 代码现在都由 MSBuild 构建，因此请移除 Gradle 构建脚本中的 externalNativeBuild 部分。这些部分过去用于调用 CMake 或 ndk-build 以编译 C/C++ 代码，但不再需要。”

## Profiler

Profiler的编译选项由LVK_WITH_TRACY/LVK_WITH_TRACY_GPU配置，默认开

下载 https://github.com/wolfpld/tracy/releases/tag/v0.13.1

### Windows

启动app，打开tracy-profiler.exe，连接127.0.0.1

### Android

usb连接安卓设备，启动adb，并执行指令做端口转发：

```powershell
adb forward tcp:8086 tcp:8086
```

启动 Android app 后，在 PC 上打开 Tracy Profiler，连接：127.0.0.1:8086

安卓端暂时只支持 CPU Tracy Profile

## 开发

- 修改cmake/android配置，需要重新用cmake生成项目，编译，推包
- 修改c++代码，编译推包即可
- 修改shader/资源，直接推资源即可

常用修改

## 常用调参代码路径

- 纹理分辨率：`Renderer/src/DemoConfig.h`，`Renderer/src/scene/SceneUtils.h`
- Android ASTC block 大小：`Renderer/src/DemoConfig.h`，`Renderer/src/scene/SceneUtils.h`
- 窗口/渲染分辨率：`shared/VulkanApp.cpp`，`Renderer/src/SceneResources.cpp`
- 渲染开关：`Renderer/src/DemoConfig.h`，`Renderer/src/DemoUI.cpp`
- Bistro 场景 interior / exterior：`Renderer/src/scene/Bistro.h`
- Vulkan Validation Layer 开关：`shared/VulkanApp.cpp`，`Renderer/android/app/build.gradle.in`




TODO: 相机/安卓触控
自动化相机轨道路径

## Vulkan Validation Layer

Windows 推荐使用 Vulkan SDK 自带的 Vulkan Configurator 配置 validation layer。

Android 可以通过代码里的 `enableValidation` 开关启用；如果设备没有自动发现 APK 内置的 validation layer，需要用 adb 开启 GPU debug layer 发现机制，具体命令参考 Google Android Vulkan validation layer 文档。

## Malioc Shader 分析

`python .\tools\analyze_mali_shaders.py`

## Perfetto 数据统计

e.g.

~~~
SELECT AVG(dur) / 1e6 AS avg_ms
FROM slice
WHERE name GLOB '*main*'
AND EXTRACT_ARG(arg_set_id, 'Labels') GLOB '*AO';
~~~
