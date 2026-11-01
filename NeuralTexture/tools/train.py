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
    p.add_argument("--steps", type=int, default=2000)
    p.add_argument("--batch", type=int, default=2048)
    p.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    p.add_argument("--resume", type=Path)
    p.add_argument("--seed", type=int, default=7)
    a = p.parse_args()
    if (
        min(a.size, a.grid, a.steps, a.batch) < 1
        or a.grid > a.size
        or a.size < 4
        or a.size & (a.size - 1)
    ):
        p.error(
            "Power-of-two size >=4, positive grid/steps/batch and grid <= size required"
        )
    torch.set_num_threads(4)
    torch.manual_seed(a.seed)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    doc, images = prepare(a.gltf, a.size)
    np.rint(images * 255).astype("u1").tofile(a.output.with_suffix(".ref"))
    geometry(doc, a.gltf, a.output.with_suffix(".scene"))
    model = NeuralTexture(len(images), a.size, a.grid).to(a.device)
    checkpoint = None
    if a.resume:
        model, checkpoint = load(a.resume, a.device)
        if model.config != dict(materials=len(images), size=a.size, grid=a.grid):
            raise ValueError("Resume configuration mismatch")
    target = reference(a.output.with_suffix(".ref"), model, a.device)
    all_ids = torch.arange(len(target))
    buckets = split(all_ids)
    train_ids = all_ids[buckets >= 2]
    validation_ids = all_ids[buckets == 1].to(a.device)
    loader = DataLoader(TensorDataset(train_ids), batch_size=a.batch, shuffle=True)
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
            },
            indent=2,
        )
    )
    print("Checkpoint:", a.output)


if __name__ == "__main__":
    main()
