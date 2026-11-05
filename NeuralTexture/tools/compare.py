"""用相同数据与训练预算对比 latent 维数、MLP 宽度和深度。"""

import argparse
import json
from pathlib import Path
import subprocess
import sys


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("gltf", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--steps", type=int, default=5000)
    p.add_argument("--size", type=int, default=256)
    p.add_argument("--grid", type=int, default=32)
    p.add_argument("--batch", type=int, default=2048)
    p.add_argument("--seed", type=int, default=7)
    p.add_argument("--device", default="cpu")
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    tools = Path(__file__).resolve().parent
    variants = [
        ("baseline", 8, [32]),
        ("features16", 16, [32]),
        ("wide64", 8, [64]),
        ("deep32", 8, [32, 32]),
        ("features16_deep64", 16, [64, 64]),
    ]
    results = []
    for name, features, hidden in variants:
        # 每组从相同 seed 重新训练；固定目标分辨率、网格、像素划分和优化次数。
        checkpoint = a.output / (name + ".pt")
        commands = [
            [sys.executable, str(tools / "train.py"), str(a.gltf),
             "--output", str(checkpoint), "--features", str(features),
             "--hidden", *map(str, hidden), "--steps", str(a.steps),
             "--size", str(a.size), "--grid", str(a.grid),
             "--batch", str(a.batch), "--seed", str(a.seed), "--device", a.device],
            [sys.executable, str(tools / "test.py"), str(checkpoint), "--device", a.device],
        ]
        print(f"Training {name}: {' -> '.join(map(str, [features, *hidden, 9]))}", flush=True)
        with (a.output / (name + ".log")).open("w", encoding="utf-8") as log:
            for command in commands:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
        training = json.loads(checkpoint.with_suffix(".train.json").read_text())
        metrics = json.loads(checkpoint.with_suffix(".test.json").read_text())
        widths = [features, *hidden, 9]
        macs = sum(inputs * outputs for inputs, outputs in zip(widths, widths[1:]))
        mlp_parameters = macs + sum(widths[1:])
        latent_parameters = training["config"]["materials"] * a.grid ** 2 * features
        results.append(dict(
            name=name, config=training["config"], steps=a.steps, seed=a.seed,
            batch=a.batch, device=a.device, training_seconds=training["training_seconds"],
            latent_parameters=latent_parameters, mlp_parameters=mlp_parameters,
            fp32_parameter_bytes=4 * (latent_parameters + mlp_parameters),
            mlp_macs_per_query=macs, test=metrics["test"],
            full_reconstruction=metrics["full_reconstruction"],
        ))
        # 逐组保存结果，长实验中断时仍可查看已经完成的配置。
        (a.output / "comparison.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
        print(f"{name}: test PSNR {metrics['test']['mean_psnr_db']:.3f} dB", flush=True)


if __name__ == "__main__":
    main()
