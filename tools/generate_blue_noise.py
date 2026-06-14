"""Generate a 128x128x64 blue noise texture (64 slices tiled vertically into 128x8192 PNG)."""

import numpy as np
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    import subprocess, sys
    subprocess.check_call([sys.executable, "-m", "pip", "install", "Pillow"])
    from PIL import Image

from scipy.ndimage import gaussian_filter

SLICES = 64
SIZE = 128


def void_and_cluster(size, seed):
    rng = np.random.default_rng(seed)
    n = size * size
    initial_density = 0.1
    grid = rng.random((size, size)) < initial_density

    def energy(g):
        return gaussian_filter(g.astype(np.float64), sigma=1.5, mode='wrap')

    rank = np.zeros((size, size), dtype=np.int32)
    current_rank = 0

    temp_grid = grid.copy()
    removed = []
    while np.any(temp_grid):
        e = energy(temp_grid)
        e[~temp_grid] = -np.inf
        idx = np.unravel_index(np.argmax(e), e.shape)
        temp_grid[idx] = False
        removed.append(idx)

    for idx in reversed(removed):
        rank[idx] = current_rank
        current_rank += 1

    temp_grid = grid.copy()
    while current_rank < n:
        e = energy(temp_grid)
        e[temp_grid] = np.inf
        idx = np.unravel_index(np.argmin(e), e.shape)
        temp_grid[idx] = True
        rank[idx] = current_rank
        current_rank += 1

    return (rank.astype(np.float64) / (n - 1) * 255.0).astype(np.uint8)


def main():
    output_path = Path(__file__).parent.parent / "data" / "blue_noise_128x128x64.png"

    # 128 wide, 128*64=8192 tall, RGBA
    img = np.zeros((SIZE * SLICES, SIZE, 4), dtype=np.uint8)

    for z in range(SLICES):
        print(f"Generating slice {z+1}/{SLICES}...")
        ch_r = void_and_cluster(SIZE, seed=z * 2)
        ch_g = void_and_cluster(SIZE, seed=z * 2 + 1)
        y0 = z * SIZE
        img[y0:y0 + SIZE, :, 0] = ch_r
        img[y0:y0 + SIZE, :, 1] = ch_g
        img[y0:y0 + SIZE, :, 2] = 0
        img[y0:y0 + SIZE, :, 3] = 255

    Image.fromarray(img, 'RGBA').save(output_path)
    print(f"Saved: {output_path} ({SIZE}x{SIZE}x{SLICES} tiled as {SIZE}x{SIZE*SLICES})")


if __name__ == "__main__":
    main()
