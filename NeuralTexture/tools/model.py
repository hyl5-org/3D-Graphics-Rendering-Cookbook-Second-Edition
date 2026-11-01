"""Shared PyTorch model and stable FP32 Slang deployment format."""

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
    def __init__(self, materials, size, grid):
        super().__init__()
        self.config = dict(materials=materials, size=size, grid=grid)
        self.latent = nn.Parameter(torch.randn(materials, grid, grid, FEATURES) * 0.1)
        self.mlp = nn.Sequential(
            nn.Linear(FEATURES, HIDDEN),
            nn.ReLU(),
            nn.Linear(HIDDEN, CHANNELS),
            nn.Sigmoid(),
        )

    def forward(self, material, uv):
        grid = self.config["grid"]
        p = uv.remainder(1) * grid - 0.5
        xy = p.floor().long()
        f = p - xy
        feature = 0
        for dx, dy in ((0, 0), (1, 0), (0, 1), (1, 1)):
            weight = (f[:, 0] if dx else 1 - f[:, 0]) * (f[:, 1] if dy else 1 - f[:, 1])
            feature = (
                feature
                + self.latent[material, (xy[:, 1] + dy) % grid, (xy[:, 0] + dx) % grid]
                * weight[:, None]
            )
        return self.mlp(feature)

    def pixels(self, ids):
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
    c = model.config
    raw = np.fromfile(path, dtype="u1")
    if raw.size != c["materials"] * c["size"] ** 2 * CHANNELS:
        raise ValueError("Reference dimensions do not match checkpoint")
    return torch.from_numpy(raw.reshape(-1, CHANNELS).astype("f4") / 255).to(device)


def export(model, path):
    c = model.config
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as f:
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
        # nn.Linear uses [out,in]; Slang indexes [in,out].
        for tensor in (
            model.latent,
            model.mlp[0].weight.T,
            model.mlp[0].bias,
            model.mlp[2].weight.T,
            model.mlp[2].bias,
        ):
            f.write(tensor.detach().cpu().numpy().astype("<f4").tobytes())


def split(ids):
    # A fixed spatial holdout inside each material, not unseen-material generalization.
    bucket = ((ids * 1664525 + 1013904223) & 0xFFFFFFFF) % 10
    return bucket


@torch.inference_mode()
def evaluate(model, target, ids, batch=16384):
    error = torch.zeros(CHANNELS, dtype=torch.float64, device=target.device)
    for chunk in ids.split(batch):
        error += ((model.pixels(chunk) - target[chunk]).double() ** 2).sum(0)
    mse = error / ids.numel()
    return {
        "pixels": ids.numel(),
        "mse": mse.tolist(),
        "psnr_db": (-10 * torch.log10(mse.clamp_min(1e-12))).tolist(),
        "mean_psnr_db": (-10 * torch.log10(mse.mean().clamp_min(1e-12))).item(),
    }
