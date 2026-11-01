# Minimal Neural Texture Compression

这个 sample 用完整 Sponza 场景演示一条最小的神经纹理压缩流程：准备 PBR 纹理数据，用 PyTorch 训练，导出压缩表示，再用 Slang 在 fragment shader 中逐像素解码。

先记住三个对象：

- **目标纹理**：希望还原的颜色、法线、粗糙度等数据，只在训练和质量对比时读取。
- **Latent 网格**：每个材质各有一张低分辨率的、可训练的特征网格，保存这个材质的局部信息。
- **MLP**：所有材质共用一个小网络，把查询到的特征转换成 PBR 通道。

训练后的 latent 和 MLP 参数一起构成压缩数据。当前实现仍把它们保存为 FP32，压缩主要来自空间分辨率的降低；不是通过 PNG/JPEG、BC 格式或熵编码来压缩。

## 1. 我们到底在压缩什么？

[`data.py`](tools/data.py) 读取 glTF 材质，把各张纹理缩放到相同尺寸，并合并成每个像素的 9 个通道：

| 下标 | 内容 | 数值含义 |
| --- | --- | --- |
| 0–2 | Base color RGB | sRGB 编码的颜色 |
| 3 | Alpha | 不透明度 |
| 4–5 | Tangent-space normal XY | 从法线的 `[-1, 1]` 编码到 `[0, 1]` |
| 6 | Roughness | 粗糙度 |
| 7 | Metallic | 金属度 |
| 8 | Ambient occlusion | AO |

材质的颜色、roughness、metallic、normal scale 和 AO strength 等 glTF factor 在准备数据时烘焙进去。没有贴图的通道使用默认值。Base color factor 在转换到线性空间后应用，再编码回 sRGB；法线 Z 在准备数据和渲染时重建。

以默认参数为例，目标数据的形状是：

```text
[材质数 M, 纹理高度 256, 纹理宽度 256, 通道数 9]
```

这些值都在 `[0, 1]` 内。训练前会将目标量化成 8-bit，写成 `.ref`；训练时再除以 255，得到浮点目标值。`.ref` 也作为运行时的未压缩对照数据。

训练只拟合材质数据，不读取相机、光源或渲染结果。它不会学习阴影、光照或最终的 PBR 图像。

## 2. Latent 网格和神经网络如何表示纹理？

### 每个材质一张 latent 网格

[`NeuralTexture.latent`](tools/model.py) 是一个 `nn.Parameter`，默认形状为：

```text
[M, 32, 32, 8]
```

每个网格点保存 8 个可训练浮点数。这 8 个数没有预先指定的物理含义：它们不是 RGB、法线或 roughness，而是训练得到的特征。

所有材质共用 MLP 权重，但通过各自的 latent 网格保存不同的纹理信息。`materialId` 用于选择网格，并不直接作为网络输入。

### 一次 UV 查询

给定材质和 UV，先在 latent 网格中做周期性双线性插值：

```text
position = frac(uv) * gridSize - 0.5
cell     = floor(position)
fraction = position - cell

feature = 对 cell 周围的四个网格点做双线性插值
```

这里的 `feature` 是一个 **8 维向量**。`-0.5` 用于按 texel center 对齐采样；越过边界的坐标按 repeat 方式环绕。

例如，`gridSize = 32`、`uv = (0.25, 0.5)` 时：

```text
position = (7.5, 15.5)
四个邻居 = (7,15), (8,15), (7,16), (8,16)
四个权重 = 0.25, 0.25, 0.25, 0.25
```

把四个 8 维向量平均后得到输入特征。注意，这里插值的是 latent 特征；之后仍要经过 MLP 才能得到材质通道。

### 共享的 8 → 32 → 9 MLP

[`NeuralTexture.mlp`](tools/model.py) 的网络只有两层 Linear：

```text
材质 ID + UV
    ↓ 选择 latent 网格，双线性插值
8 维 feature
    ↓ Linear(8, 32) + ReLU
32 维 hidden
    ↓ Linear(32, 9) + Sigmoid
9 个 PBR 通道
```

采用列向量记法：

```text
z = bilinear(latent[materialId], uv)       # [8]
h = max(W1 * z + b1, 0)                   # W1: [32,8], b1: [32]
y = sigmoid(W2 * h + b2)                  # W2: [9,32], b2: [9]
sigmoid(x) = 1 / (1 + exp(-x))
```

ReLU 提供非线性，使网络可以表达比单纯线性插值更复杂的变化；Sigmoid 把 9 个输出限制到 `(0, 1)`，与训练目标的编码范围对应。

MLP 参数共 **585 个**：`8*32 + 32 + 32*9 + 9`。真正占据大部分压缩空间的是每个材质的 latent 网格，而不是这个小网络。

## 3. 训练：同时学习 latent 和 MLP

训练是在为指定的这批材质拟合一个压缩表示。增加另一批材质时，需要为新数据进行训练；当前 sample 没有实现直接编码任意新图片的通用 encoder。

[`train.py`](tools/train.py) 的流程是：

1. 准备 9 通道目标纹理，输出 `.ref` 和场景几何 `.scene`。
2. 初始化 latent 网格和 MLP。
3. 从训练像素中随机取一个 batch。
4. 用像素编号计算材质 ID 和像素中心 UV。
5. 查询 latent、运行 MLP，预测该像素的 9 个通道。
6. 计算预测值与目标值的 MSE，反向传播，再用 Adam 更新参数。

一个 batch 的张量尺寸是：

```text
像素编号 [B]
    → 材质 ID [B]、UV [B,2]
    → latent 插值 [B,8]
    → 第一层 [B,32]
    → 第二层 [B,9]
    → 与目标 [B,9] 比较，得到一个标量 loss
```

对应的核心训练循环如下，和实际代码使用相同的调用方式：

```python
optimizer = torch.optim.Adam(model.parameters(), lr=0.01)

optimizer.zero_grad(set_to_none=True)
predicted = model.pixels(ids)
loss = torch.nn.functional.mse_loss(predicted, target[ids])
loss.backward()
optimizer.step()
```

`model.parameters()` 包含 **latent 和两层 Linear 的参数**，因此两者一起更新。一个 UV 查询会将梯度传给它插值使用的四个 latent 点，同时也更新共享的网络权重。

MSE 是 batch 内所有像素、所有 9 个通道的平方误差平均值。当前各通道等权，没有法线角度损失或基于渲染结果的感知损失。

默认 batch 为 2048，训练 2000 个 **optimizer step**；step 不是 epoch。每步取一个 batch，遍历完训练集后 DataLoader 会重新打乱并继续。训练结束保存的是最后一步的 checkpoint，代码没有按最佳 validation 结果选择权重。

### 训练、验证和测试的区别

像素编号经过固定哈希后，约 80% 用于训练、10% 用于 validation、10% 用于 test：

| 数据 | 用途 | 是否参与梯度更新 |
| --- | --- | --- |
| Train | 拟合 latent 和 MLP | 是 |
| Validation | 每 200 步观察重建质量 | 否 |
| Test | 训练后独立评估保留像素 | 否 |

三者来自同一批材质，空间上也共享同一张 latent 网格。这衡量的是同一材质内部未参与训练的像素重建能力，不是对未知材质的泛化能力。

## 4. 离线推理、测试和导出

完整的数据流是：

```text
Sponza glTF + 原始纹理
    │ data.py：合并通道、应用 factor、缩放
    ├── .ref：8-bit 目标通道
    ├── .scene：静态几何和材质分配
    │
    │ train.py：优化 latent + MLP
    └── .pt：PyTorch checkpoint
            │
            ├── test.py：保留测试像素与全图重建指标
            │
            └── infer.py：离线推理、预览和导出
                    ├── PNG / channels.npy
                    └── .ntc：运行时使用的 latent + MLP 参数
```

`infer.py` 重新加载 `.pt`，调用 `eval()` 和 `torch.inference_mode()`，查询整张纹理的像素中心 UV。这个过程只做前向计算，不更新参数。生成的 PNG 用于观察误差，运行时并不依赖这些解码后的图片。

`test.py` 分别报告 test 像素和 full reconstruction 的 MSE/PSNR。因为通道范围是 `[0, 1]`，整体 PSNR 为：

```text
PSNR = -10 * log10(所有像素、所有通道的平均 MSE)
```

Full reconstruction 包含训练像素，不能与 test 指标混为一谈。这些指标在通道编码空间计算，不是线性辐射误差，也不是最终渲染图像的 PSNR。

### 为什么 `.pt` 和 `.ntc` 都存在？

| 文件 | 保存什么 | 谁读取 |
| --- | --- | --- |
| `.pt` | 模型配置、latent、MLP、Adam 状态、step、seed | PyTorch，支持继续训练 |
| `.ntc` | 32-byte header + FP32 latent + FP32 MLP 参数 | C++ / Slang |
| `.ref` | 9 通道 uint8 目标数据 | 训练、评估、运行时 A/B 对照 |
| `.scene` | 顶点、索引、材质 ID、alpha cutoff | C++ 场景加载 |

`.ntc` 不含优化器状态，也不需要 Python。布局为：

```text
header | latent[M,G,G,8] | weight1[8,32] | bias1[32] | weight2[32,9] | bias2[9]
```

PyTorch 的 Linear 权重是 `[out, in]`，导出时转置为 `[in, out]`，方便标量 shader 按输入特征访问：

```text
PyTorch: W1[neuron, feature]
Shader:  model[weight1Offset + feature * 32 + neuron]
```

`test.py --deployment ...` 会用独立的 NumPy 解码实现检查这个文件布局与 PyTorch 输出的一致性。这个检查用于发现导出错误，与模型对原纹理的重建质量是两回事。

## 5. 运行时：fragment shader 如何使用它？

运行时在 C++ 中加载 `.ntc`，把 latent 和网络参数上传为 storage buffer，然后绘制 `.scene`。每个 draw 通过 push constants 提供材质 ID。

[`neural_texture_decode.slang`](shaders/neural_texture_decode.slang) 的调用关系是：

```text
fragment 的 UV + draw 的 materialId
    → getBilinearFootprint()：计算四个 latent 索引和插值权重
    → sampleLatent()：读取 4 × 8 个 FP32 值，插值得到 8 维特征
    → decodeNeural()：两层 MLP + 激活函数
    → 9 个材质通道
```

[`neural_texture.slang`](shaders/neural_texture.slang) 随后处理 alpha test、sRGB 到线性颜色的转换、法线 Z 重建和切线空间变换，最后调用 `evaluateLighting()` 计算 Lambert + GGX/Schlick 直接光照和简单环境项。

这里的 GPU **推理**只是查询已经训练好的参数，不会训练或更新网络。每次 fragment 查询直接得到当前 UV 的材质值，没有先把整张纹理解码到另一张纹理中的步骤。

### 三条解码路径

三条路径使用同一份训练结果、同一套 latent 插值和通道定义：

| 路径 | 权重布局与精度 | MLP 矩阵运算 |
| --- | --- | --- |
| Scalar FP32 | 原始 FP32 参数 | 普通 FP32 标量循环，作为精度基线 |
| Scalar FP16 | 加载时生成的紧凑 FP16 权重 | 原生 FP16 乘法与累加 |
| Cooperative Vector FP16 | 驱动转换的 FP16 inference-optimal 布局 | 两次 `coopVecMatMul()` |

两条 FP16 路径中，网络输入和 hidden 值使用 FP16；latent 插值、bias 加法和 sigmoid 仍使用 FP32。CoopVec 的内部累加方式由实现决定，不能因为输入输出都是 FP16，就假设它和标量 FP16 逐位相同。

[`CooperativeDecoder.cpp`](src/CooperativeDecoder.cpp) 负责生成标量 FP16 权重、检查 CoopVec 能力，并调用 `vkConvertCooperativeVectorMatrixNV()` 转换矩阵布局。两层 CoopVec 矩阵地址按 64 字节对齐。这个布局与设备有关，在加载时生成，不写回通用 `.ntc` 文件。LVK 在设备支持时启用可选的 `VK_NV_cooperative_vector` 推理功能。

默认使用支持的 CoopVec 路径，否则回退到 Scalar FP16；FP32 保留用于精度比较。UI 的 **Decoder** 可以随时切换。减少矩阵运算成本不等于整个 pass 按相同比例加速：latent 读取、sigmoid、几何处理和 PBR 光照仍然存在。

### 三种不同的误差检查

| 检查 | 比较什么 | 能发现什么 |
| --- | --- | --- |
| `test.py` | PyTorch 重建 vs 原纹理目标 | 压缩本身的有损误差 |
| `test.py --deployment` | 导出的 FP32 参数解码 vs PyTorch | 序列化、转置或索引错误 |
| `--validate-decode` | 三条 GPU 路径两两比较 | FP16 精度变化与 GPU 解码差异 |

`--validate-decode` 对每个材质查询 64×64 个 UV，报告最大通道误差和 RMSE；出现非有限值或最大误差超过 0.01 时失败。这是 sample 的数值容差检查，不代表所有材质都能满足特定的视觉质量要求。

## 6. 压缩率和成本从哪里来？

以 25 个材质、256×256 目标、32×32×8 FP32 latent 为例：

```text
原始对照数据 = 25 × 256 × 256 × 9 bytes           = 14,745,600 bytes
latent       = 25 × 32 × 32 × 8 × 4 bytes         =    819,200 bytes
MLP          = 585 × 4 bytes                     =      2,340 bytes
header       = 32 bytes
.ntc 合计                                           821,572 bytes
压缩比       = 14,745,600 / 821,572                ≈ 17.95×
```

这个比例对比的是缩放后的 9 通道 uint8 数据，不是原始 PNG/JPEG 文件，也不是 BC 纹理。`.scene`、`.ref`、checkpoint、GPU 分配开销和为切换 decoder 保留的额外 FP16 权重缓冲都不计入压缩包大小。当前 A/B 示例启动仍需 `.ref`，但神经解码本身不读取它。

网络每次查询包含 `8×32 + 32×9 = 544` 次乘加，另有 latent 插值和激活函数。压缩节省存储空间，同时把传统纹理采样的一部分成本转成 shader 计算；GPU 收益需要实测。

可以从三个参数理解质量与体积的权衡：

- `--grid`：增大网格可保存更多局部信息，但 latent 体积按网格边长的平方增长。
- `--steps`：增加优化次数不会改变压缩包大小，但训练更久不保证每次都改善 validation 质量。
- 网络宽度：增大 hidden 层可提高表达能力，也增加每次解码计算。当前 8→32→9 是固定架构，修改时需同步训练、导出、加载和 shader。

这是一版有损、单分辨率的最小实现。细纹理和 alpha 边缘可能丢失；没有 mipmaps、流送、emissive 通道、熵编码或量化部署格式。场景只支持静态三角形、UV0、repeat wrapping、opaque/masked 材质；不支持 texture transform、sparse accessor 或透明混合。存在 normal map 时要求提供 tangent。

## 7. 实际运行一遍

从 Cookbook 仓库根目录执行：

```powershell
python -m venv .venv-ntc
./.venv-ntc/Scripts/python -m pip install -r NeuralTexture/tools/requirements.txt
$python = './.venv-ntc/Scripts/python.exe'

# 训练：同时优化 latent 和 MLP，输出 .pt / .ref / .scene / .train.json。
& $python NeuralTexture/tools/train.py D:/Codes/models/Cauldron-Media/Sponza/glTF/Sponza.gltf --output .cache/ntc-torch/sponza.pt --steps 2000

# 离线推理与导出：生成材质 0 的预览，以及运行时 .ntc。
& $python NeuralTexture/tools/infer.py .cache/ntc-torch/sponza.pt --output .cache/ntc-torch/preview --material 0 --export .cache/ntc-torch/sponza.ntc

# 测试重建质量，并核对导出的 FP32 参数布局。
& $python NeuralTexture/tools/test.py .cache/ntc-torch/sponza.pt --deployment .cache/ntc-torch/sponza.ntc

cmake -S . -B build
cmake --build build --config RelWithDebInfo --target NeuralTexture
./build/NeuralTexture/RelWithDebInfo/NeuralTexture_ReleaseDebInfo.exe --ntc "$PWD/.cache/ntc-torch/sponza.ntc"
```

训练默认在可用时选择 CUDA，否则使用 CPU；`--device cpu` 或 `--device cuda` 可指定设备，需要安装对应的 PyTorch。GPU 渲染和解码不依赖 Python 或 PyTorch。

继续训练可传入 `--resume .cache/ntc-torch/sponza.pt`，并将 `--steps` 设为新的总 step 数；配置必须匹配。当前恢复优化器与 step，但不保证随机数和 DataLoader 顺序的精确延续。

运行时用 WASD + 鼠标移动，Space 重置。UI 可选择 reference/neural/split、PBR/单通道和 decoder。也可以从命令行指定：

```powershell
# --source 0：原始对照；1：神经解码；2：左右分屏。
./build/NeuralTexture/RelWithDebInfo/NeuralTexture_ReleaseDebInfo.exe --ntc "$PWD/.cache/ntc-torch/sponza.ntc" --source 1 --decoder fp16

# --decoder 支持 auto / fp32 / fp16 / coop；scalar 是 fp16 的别名。
# 比较三条 GPU 解码路径的数值输出。
./build/NeuralTexture/RelWithDebInfo/NeuralTexture_ReleaseDebInfo.exe --ntc "$PWD/.cache/ntc-torch/sponza.ntc" --validate-decode
```

Slang 在构建时离线编译，运行时需要保留 build 目录中的 SPIR-V。当前程序会读取同名 `.ntc`、`.scene`、`.ref`，不再需要原始 Sponza 文件。生成的模型、checkpoint 和预览留在 `.cache` 等工作目录，不纳入源码提交。

## 8. 按什么顺序读代码？

1. [`tools/model.py`](tools/model.py)：先看 `__init__()`、`forward()`、`pixels()`，理解 latent、网络和 UV 的关系。
2. [`tools/train.py`](tools/train.py)：看 target、DataLoader、loss 和 optimizer，确认哪些参数被更新。
3. [`tools/data.py`](tools/data.py)：看 `prepare()` 如何定义这 9 个训练通道；`geometry()` 是独立的场景导出。
4. [`tools/infer.py`](tools/infer.py) 与 [`tools/model.py`](tools/model.py) 的 `export()`：看训练 checkpoint 如何变成预览和 `.ntc`。
5. [`shaders/neural_texture_decode.slang`](shaders/neural_texture_decode.slang)：先读 FP32 分支，再读两个 FP16 分支。
6. [`src/CooperativeDecoder.cpp`](src/CooperativeDecoder.cpp) 与 [`src/main.cpp`](src/main.cpp)：看权重上传、布局转换、pipeline 切换和 GPU 验证。
7. [`shaders/neural_texture.slang`](shaders/neural_texture.slang)：最后看解码出的材质如何参与 PBR。

学习时可以先固定 `--decoder fp32`，用 UI 的 Base color / Roughness 等单通道视图比较 reference 与 neural，理解压缩误差；再切换 FP16，区分模型重建误差和推理精度误差。
