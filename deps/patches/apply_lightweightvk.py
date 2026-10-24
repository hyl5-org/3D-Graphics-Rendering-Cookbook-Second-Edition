from pathlib import Path
import subprocess


patch_dir = Path(__file__).resolve().parent
repository = patch_dir.parent / "src" / "lightweightvk"
patch = patch_dir / "lightweightvk-renderer-900adc4.patch"

subprocess.run(["git", "-C", str(repository), "apply", "--check", str(patch)], check=True)
subprocess.run(["git", "-C", str(repository), "apply", str(patch)], check=True)
