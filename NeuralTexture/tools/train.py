"""Train latent grids and MLP with PyTorch autograd, DataLoader and Adam."""

import argparse, json, time
from pathlib import Path
import numpy as np
import torch
from torch.utils.data import DataLoader, TensorDataset
from data import prepare, geometry
from model import NeuralTexture, load, reference, split, evaluate


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("gltf", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--size", type=int, default=256)
    p.add_argument("--grid", type=int, default=32)
    p.add_argument("--features", type=int, default=8)
    p.add_argument(
        "--hidden", type=int, nargs="+", default=[32],
        help="Hidden layer widths, e.g. --hidden 64 64",
    )
    p.add_argument("--steps", type=int, default=2000)
    p.add_argument("--batch", type=int, default=2048)
    p.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    p.add_argument("--resume", type=Path)
    p.add_argument("--seed", type=int, default=7)
    a = p.parse_args()
    if (
        min(a.size, a.grid, a.steps, a.batch, a.features, *a.hidden) < 1
        or a.grid > a.size
        or a.size < 4
        or a.size & (a.size - 1)
    ):
        p.error(
            "Power-of-two size >=4, positive grid/steps/batch/features/hidden and grid <= size required"
        )
    torch.set_num_threads(4)
    torch.manual_seed(a.seed)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    doc, images = prepare(a.gltf, a.size)
    print(images.shape)
    # 保存 uint8 参考纹理，再读回作为训练目标，保证训练与运行时对比使用相同精度。
    np.rint(images * 255).astype("u1").tofile(a.output.with_suffix(".ref"))
    # 顺便导出 C++ sample 的场景资产；几何数据不参与网络训练或 loss 计算。
    geometry(doc, a.gltf, a.output.with_suffix(".scene"))
    model = NeuralTexture(len(images), a.size, a.grid, a.features, a.hidden).to(a.device)
    requested_config = model.config
    checkpoint = None
    if a.resume:
        model, checkpoint = load(a.resume, a.device)
        if model.config != requested_config:
            raise ValueError("Resume configuration mismatch")
    target = reference(a.output.with_suffix(".ref"), model, a.device)
    all_ids = torch.arange(len(target))
    buckets = split(all_ids)
    # 测试像素留给 test.py；训练期间只用验证集观察重建质量。
    train_ids = all_ids[buckets >= 2]
    validation_ids = all_ids[buckets == 1].to(a.device)
    # 独立随机数生成器保证不同网络结构看到相同顺序的训练样本。
    loader = DataLoader(
        TensorDataset(train_ids), batch_size=a.batch, shuffle=True,
        generator=torch.Generator().manual_seed(a.seed),
    )
    # model.parameters() 同时包含每个材质的 latent 网格和共享 MLP 参数。
    optimizer = torch.optim.Adam(model.parameters(), lr=0.01)
    step = checkpoint["step"] if checkpoint else 0
    if checkpoint:
        optimizer.load_state_dict(checkpoint["optimizer"])
    start = time.perf_counter()
    model.train()
    while step < a.steps:
        for (ids,) in loader:
            ids = ids.to(a.device)
            optimizer.zero_grad(set_to_none=True)
            # 用九个编码通道的等权 MSE 训练；反向传播同时更新 latent 和 MLP。
            loss = torch.nn.functional.mse_loss(model.pixels(ids), target[ids])
            loss.backward()
            optimizer.step()
            step += 1
            if step % 200 == 0 or step == a.steps:
                model.eval()
                metrics = evaluate(model, target, validation_ids)
                print(
                    f"{step}/{a.steps} train batch MSE={loss.item():.6f} validation PSNR={metrics['mean_psnr_db']:.2f} dB",
                    flush=True,
                )
                model.train()
            if step >= a.steps:
                break
    # checkpoint 保留模型配置、参数和优化器状态，用于继续训练及离线推理。
    torch.save(
        {
            "config": model.config,
            "model": model.state_dict(),
            "optimizer": optimizer.state_dict(),
            "step": step,
            "seed": a.seed,
        },
        a.output,
    )
    a.output.with_suffix(".train.json").write_text(
        json.dumps(
            {
                "device": a.device,
                "torch": torch.__version__,
                "steps": step,
                "training_seconds": time.perf_counter() - start,
                "train_pixels": len(train_ids),
                "validation_pixels": len(validation_ids),
                "seed": a.seed,
                "config": model.config,
            },
            indent=2,
        )
    )
    print("Checkpoint:", a.output)


if __name__ == "__main__":
    main()
