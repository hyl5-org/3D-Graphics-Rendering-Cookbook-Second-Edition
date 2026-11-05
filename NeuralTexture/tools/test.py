"""Evaluate a saved checkpoint on held-out pixels and on the full compression target."""

import argparse, json, struct
import numpy as np
from pathlib import Path
import torch
from model import load, reference, split, evaluate, CHANNEL_NAMES


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("checkpoint", type=Path)
    p.add_argument("--device", default="cpu")
    p.add_argument("--reference", type=Path)
    p.add_argument("--deployment", type=Path)
    a = p.parse_args()
    torch.set_num_threads(4)
    model, _ = load(a.checkpoint, a.device)
    target = reference(a.reference or a.checkpoint.with_suffix(".ref"), model, a.device)
    ids = torch.arange(len(target), device=a.device)
    # test 只评估未参与训练的测试像素；full_reconstruction 包含全部像素。
    metrics = {
        "channels": CHANNEL_NAMES,
        "test": evaluate(model, target, ids[split(ids) == 0]),
        "full_reconstruction": evaluate(model, target, ids),
        "raw_bytes": target.numel(),
    }
    if a.deployment:
        if model.config["features"] != 8 or model.config["hidden"] != [32]:
            p.error("Runtime deployment validation only supports 8->32->9")
        # 独立解析 .ntc，并用 NumPy 重建解码流程，检查导出布局与 PyTorch 是否一致。
        data = a.deployment.read_bytes()
        header = struct.unpack_from("<8I", data)
        c = model.config
        if header != (0x3143544E, 1, c["materials"], c["size"], c["grid"], 8, 32, 9):
            raise ValueError("Deployment header mismatch")
        values = np.frombuffer(data, dtype="<f4", offset=32)
        n = c["materials"] * c["grid"] ** 2 * 8
        if len(values) != n + 585:
            raise ValueError("Deployment payload size mismatch")
        latent = values[:n].reshape(c["materials"], c["grid"], c["grid"], 8)
        w1 = values[n : n + 256].reshape(8, 32)
        b1 = values[n + 256 : n + 288]
        w2 = values[n + 288 : n + 576].reshape(32, 9)
        b2 = values[n + 576 : n + 585]
        # 固定随机种子抽样，复现运行时的 repeat 双线性采样和两层 MLP。
        sample = np.random.default_rng(19).integers(len(target), size=1024)
        size = c["size"]
        grid = c["grid"]
        x = (sample % size + 0.5) * grid / size - 0.5
        y = (sample // size % size + 0.5) * grid / size - 0.5
        ix = np.floor(x).astype(int)
        iy = np.floor(y).astype(int)
        fx = x - ix
        fy = y - iy
        material = sample // (size * size)
        feature = sum(
            latent[material, (iy + dy) % grid, (ix + dx) % grid] * w[:, None]
            for (dx, dy), w in zip(
                ((0, 0), (1, 0), (0, 1), (1, 1)),
                ((1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy),
            )
        )
        decoded = 1 / (
            1 + np.exp(-np.clip(np.maximum(feature @ w1 + b1, 0) @ w2 + b2, -20, 20))
        )
        with torch.inference_mode():
            expected = model.pixels(torch.tensor(sample, device=a.device)).cpu().numpy()
        # 这里检查导出解码的一致性；重建纹理本身的误差由上面的 MSE / PSNR 衡量。
        max_error = float(np.max(np.abs(decoded - expected)))
        if max_error > 1e-5:
            raise AssertionError(f"Export/decode mismatch: {max_error}")
        metrics["deployment_max_abs_error"] = max_error
        metrics["compressed_bytes"] = len(data)
        metrics["ratio"] = target.numel() / len(data)
    output = a.checkpoint.with_suffix(".test.json")
    output.write_text(json.dumps(metrics, indent=2))
    print(json.dumps(metrics, indent=2))


if __name__ == "__main__":
    main()
