"""Configurable PyTorch model and fixed-architecture FP32 Slang export."""

import struct
from pathlib import Path
import numpy as np
import torch
from torch import nn

FEATURES, HIDDEN, CHANNELS = 8, 32, 9
CHANNEL_NAMES = [
    "R",
    "G",
    "B",
    "alpha",
    "normalX",
    "normalY",
    "roughness",
    "metallic",
    "AO",
]


class NeuralTexture(nn.Module):
    def __init__(self, materials, size, grid, features=FEATURES, hidden=(HIDDEN,)):
        super().__init__()
        if features < 1 or not hidden or any(width < 1 for width in hidden):
            raise ValueError("Positive features and at least one positive hidden width required")
        self.config = dict(
            materials=materials, size=size, grid=grid, features=features, hidden=list(hidden)
        )
        # 每个材质的一整组 PBR 贴图共用一张 latent 网格；每个格点存 features 维可训练特征。
        self.latent = nn.Parameter(torch.randn(materials, grid, grid, features) * 0.1)
        # 所有材质共享 MLP；隐藏层使用 ReLU，最后用 Sigmoid 输出九个 [0, 1] 通道。
        widths = [features, *hidden, CHANNELS]
        layers = []
        for index, (inputs, outputs) in enumerate(zip(widths, widths[1:])):
            layers.append(nn.Linear(inputs, outputs))
            layers.append(nn.Sigmoid() if index == len(widths) - 2 else nn.ReLU())
        self.mlp = nn.Sequential(*layers)

    def forward(self, material, uv):
        grid = self.config["grid"]
        # repeat 寻址，并以格点中心为采样位置；与运行时 Slang 的坐标约定一致。
        p = uv.remainder(1) * grid - 0.5
        xy = p.floor().long()
        f = p - xy
        feature = 0
        # 双线性插值四个相邻格点；梯度会回传到这些 latent 特征及后续 MLP。
        for dx, dy in ((0, 0), (1, 0), (0, 1), (1, 1)):
            weight = (f[:, 0] if dx else 1 - f[:, 0]) * (f[:, 1] if dy else 1 - f[:, 1])
            feature = (
                feature
                + self.latent[material, (xy[:, 1] + dy) % grid, (xy[:, 0] + dx) % grid]
                * weight[:, None]
            )
        # 插值后的形状为 [batch, features]。
        #print(feature.shape)
        return self.mlp(feature)

    def pixels(self, ids):
        # 将展平的像素编号还原为材质编号和纹素中心 UV，避免存储完整的坐标表。
        size = self.config["size"]
        material = ids // (size * size)
        uv = torch.stack(
            ((ids % size + 0.5) / size, (ids // size % size + 0.5) / size), -1
        )
        return self(material, uv)


def load(path, device):
    checkpoint = torch.load(path, map_location=device, weights_only=True)
    model = NeuralTexture(**checkpoint["config"]).to(device)
    model.load_state_dict(checkpoint["model"])
    model.eval()
    return model, checkpoint


def reference(path, model, device):
    # .ref 保存量化后的九通道目标，训练和质量评估统一使用同一份数据。
    c = model.config
    raw = np.fromfile(path, dtype="u1")
    if raw.size != c["materials"] * c["size"] ** 2 * CHANNELS:
        raise ValueError("Reference dimensions do not match checkpoint")
    return torch.from_numpy(raw.reshape(-1, CHANNELS).astype("f4") / 255).to(device)


def export(model, path):
    c = model.config
    # 目前 Slang / C++ 仍使用固定架构；实验网络仅通过 checkpoint 做离线推理与评估。
    if c["features"] != FEATURES or c["hidden"] != [HIDDEN]:
        raise ValueError("Runtime .ntc export only supports 8->32->9; use checkpoint inference for experimental architectures")
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as f:
        # .ntc：32 字节文件头，随后依次写入 latent、W1、b1、W2、b2，均为 FP32。
        f.write(
            struct.pack(
                "<8I",
                0x3143544E,
                1,
                c["materials"],
                c["size"],
                c["grid"],
                FEATURES,
                HIDDEN,
                CHANNELS,
            )
        )
        # PyTorch 权重布局为 [输出, 输入]，转置为 Slang 使用的 [输入, 输出]。
        for tensor in (
            model.latent,
            model.mlp[0].weight.T,
            model.mlp[0].bias,
            model.mlp[2].weight.T,
            model.mlp[2].bias,
        ):
            f.write(tensor.detach().cpu().numpy().astype("<f4").tobytes())


def split(ids):
    # 固定哈希划分像素：桶 0 测试、桶 1 验证、其余训练；评估同一材质内的未训练像素。
    bucket = ((ids * 1664525 + 1013904223) & 0xFFFFFFFF) % 10
    return bucket


@torch.inference_mode()
def evaluate(model, target, ids, batch=16384):
    error = torch.zeros(CHANNELS, dtype=torch.float64, device=target.device)
    for chunk in ids.split(batch):
        error += ((model.pixels(chunk) - target[chunk]).double() ** 2).sum(0)
    mse = error / ids.numel()
    # 通道范围为 [0, 1]；总体 PSNR 由九通道平均 MSE 计算，并非各通道 PSNR 的均值。
    return {
        "pixels": ids.numel(),
        "mse": mse.tolist(),
        "psnr_db": (-10 * torch.log10(mse.clamp_min(1e-12))).tolist(),
        "mean_psnr_db": (-10 * torch.log10(mse.mean().clamp_min(1e-12))).item(),
    }
