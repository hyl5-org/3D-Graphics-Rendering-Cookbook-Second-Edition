"""Offline inference, decoded channel previews and optional Slang model export."""

import argparse, json
from pathlib import Path
import numpy as np
import torch
from PIL import Image
from model import load, export, CHANNEL_NAMES


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("checkpoint", type=Path)
    p.add_argument("--device", default="cpu")
    p.add_argument("--material", type=int, default=0)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--export", type=Path)
    a = p.parse_args()
    torch.set_num_threads(4)
    model, _ = load(a.checkpoint, a.device)
    c = model.config
    if not 0 <= a.material < c["materials"]:
        p.error("Invalid material index")
    a.output.mkdir(parents=True, exist_ok=True)
    with torch.inference_mode():
        ids = (
            torch.arange(c["size"] ** 2, device=a.device) + a.material * c["size"] ** 2
        )
        image = (
            torch.cat([model.pixels(chunk) for chunk in ids.split(16384)])
            .cpu()
            .numpy()
            .reshape(c["size"], c["size"], 9)
        )
    pixels = np.rint(image * 255).clip(0, 255).astype("u1")
    Image.fromarray(pixels[..., :3]).save(a.output / "base_color.png")
    for i, name in enumerate(CHANNEL_NAMES[3:], 3):
        Image.fromarray(pixels[..., i]).save(a.output / (name + ".png"))
    np.save(a.output / "channels.npy", image)
    if a.export:
        # Companion scene/reference files must use the same destination stem.
        import shutil

        if a.export.resolve() == a.checkpoint.resolve():
            p.error("Export must not overwrite the checkpoint")
        export(model, a.export)
        for suffix in (".ref", ".scene"):
            src = a.checkpoint.with_suffix(suffix)
            dst = a.export.with_suffix(suffix)
            if src.resolve() != dst.resolve():
                shutil.copyfile(src, dst)
        raw = c["materials"] * c["size"] ** 2 * 9
        compressed = a.export.stat().st_size
        print(
            json.dumps(
                {
                    "raw_bytes": raw,
                    "compressed_bytes": compressed,
                    "ratio": raw / compressed,
                },
                indent=2,
            )
        )


if __name__ == "__main__":
    main()
