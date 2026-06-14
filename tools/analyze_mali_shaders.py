#!/usr/bin/env python3
"""
Compile the Renderer shaders the same way LVK sees them, then run Mali
Offline Compiler and save reports under build/shader-analysis.
"""

from __future__ import annotations

import argparse
import csv
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MALIOC = Path(r"D:\hylgfx\mali_offline_compiler\malioc.exe")


STAGE_BY_SUFFIX = {
    ".vert": ("vert", "--vertex"),
    ".frag": ("frag", "--fragment"),
    ".comp": ("comp", "--compute"),
    ".geom": ("geom", "--geometry"),
    ".tesc": ("tesc", "--tessellation_control"),
    ".tese": ("tese", "--tessellation_evaluation"),
    ".rgen": ("rgen", "--ray_generation"),
    ".rint": ("rint", "--ray_intersection"),
    ".rahit": ("rahit", "--ray_any_hit"),
    ".rchit": ("rchit", "--ray_closest_hit"),
    ".rmiss": ("rmiss", "--ray_miss"),
    ".rcall": ("rcall", "--ray_callable"),
}


INJECT_VERTEX_OR_COMPUTE = """#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require
#extension GL_EXT_debug_printf : enable
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_samplerless_texture_functions : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
"""


INJECT_FRAGMENT_BASE = """#version 460
#extension GL_EXT_buffer_reference_uvec2 : require
#extension GL_EXT_debug_printf : enable
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_samplerless_texture_functions : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
"""


INJECT_FRAGMENT_RAY_QUERY = """#extension GL_EXT_buffer_reference : require
#extension GL_EXT_ray_query : require
layout(set = 0, binding = 4) uniform accelerationStructureEXT kTLAS[];
"""


INJECT_FRAGMENT_BINDLESS = """layout (set = 0, binding = 0) uniform texture2D   kTextures2D[];
layout (set = 1, binding = 0) uniform texture3D   kTextures3D[];
layout (set = 2, binding = 0) uniform textureCube kTexturesCube[];
layout (set = 3, binding = 0) uniform texture2D   kTextures2DShadow[];
layout (set = 0, binding = 1) uniform sampler       kSamplers[];
layout (set = 3, binding = 1) uniform samplerShadow kSamplersShadow[];
layout (set = 0, binding = 3) uniform sampler2D     kSamplersYUV[];
"""


FRAGMENT_HELPERS = {
    "textureBindless2D(": """vec4 textureBindless2D(uint textureid, uint samplerid, vec2 uv) {
  return texture(nonuniformEXT(sampler2D(kTextures2D[textureid], kSamplers[samplerid])), uv);
}
""",
    "textureBindless2DLod(": """vec4 textureBindless2DLod(uint textureid, uint samplerid, vec2 uv, float lod) {
  return textureLod(nonuniformEXT(sampler2D(kTextures2D[textureid], kSamplers[samplerid])), uv, lod);
}
""",
    "textureBindless2DShadow(": """float textureBindless2DShadow(uint textureid, uint samplerid, vec3 uvw) {
  return texture(nonuniformEXT(sampler2DShadow(kTextures2DShadow[textureid], kSamplersShadow[samplerid])), uvw);
}
""",
    "textureBindlessSize2D(": """ivec2 textureBindlessSize2D(uint textureid) {
  return textureSize(nonuniformEXT(kTextures2D[textureid]), 0);
}
""",
    "textureBindlessCube(": """vec4 textureBindlessCube(uint textureid, uint samplerid, vec3 uvw) {
  return texture(nonuniformEXT(samplerCube(kTexturesCube[textureid], kSamplers[samplerid])), uvw);
}
""",
    "textureBindlessCubeLod(": """vec4 textureBindlessCubeLod(uint textureid, uint samplerid, vec3 uvw, float lod) {
  return textureLod(nonuniformEXT(samplerCube(kTexturesCube[textureid], kSamplers[samplerid])), uvw, lod);
}
""",
    "textureBindless3D(": """vec4 textureBindless3D(uint textureid, uint samplerid, vec3 uvw) {
  return texture(nonuniformEXT(sampler3D(kTextures3D[textureid], kSamplers[samplerid])), uvw);
}
""",
    "textureBindless3DLod(": """vec4 textureBindless3DLod(uint textureid, uint samplerid, vec3 uvw, float lod) {
  return textureLod(nonuniformEXT(sampler3D(kTextures3D[textureid], kSamplers[samplerid])), uvw, lod);
}
""",
    "textureBindlessQueryLevels2D(": """int textureBindlessQueryLevels2D(uint textureid) {
  return textureQueryLevels(nonuniformEXT(kTextures2D[textureid]));
}
""",
    "textureBindlessQueryLevelsCube(": """int textureBindlessQueryLevelsCube(uint textureid) {
  return textureQueryLevels(nonuniformEXT(kTexturesCube[textureid]));
}
""",
}


LOAD_SHADER_RE = re.compile(r'loadShaderModule\s*\([^;]*?"([^"]+)"', re.DOTALL)
INCLUDE_RE = re.compile(r"#include\s+<([^>]+)>")
BOOL_SPEC_RE = re.compile(
    r"layout\s*\(\s*constant_id\s*=\s*(\d+)\s*\)\s*const\s+bool\s+(\w+)\s*=\s*(true|false)\s*;"
)


@dataclass
class ShaderJob:
    source_path: Path
    stage: str
    mali_stage_flag: str
    variant: str
    patched_source: str

    @property
    def stem(self) -> str:
        rel = self.source_path.as_posix()
        name = re.sub(r"[^A-Za-z0-9_.-]+", "_", rel)
        name = name.replace("/", "_")
        suffix = f"_{self.variant}" if self.variant else ""
        return f"{name}{suffix}"


def run(cmd: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def read_shader_file(path: Path, seen: set[Path] | None = None) -> str:
    seen = seen or set()
    resolved = (REPO_ROOT / path).resolve() if not path.is_absolute() else path.resolve()
    if resolved in seen:
        raise RuntimeError(f"Recursive include detected: {resolved}")
    seen.add(resolved)

    text = resolved.read_text(encoding="utf-8-sig")

    def replace_include(match: re.Match[str]) -> str:
        include_path = REPO_ROOT / match.group(1)
        return read_shader_file(include_path, seen)

    return INCLUDE_RE.sub(replace_include, text)


def stage_for_path(path: Path) -> tuple[str, str]:
    suffix = path.suffix.lower()
    if suffix not in STAGE_BY_SUFFIX:
        raise ValueError(f"Unsupported shader suffix: {path}")
    return STAGE_BY_SUFFIX[suffix]


def inject_lvk_preamble(source: str, stage: str) -> str:
    if "#version " in source:
        return source

    if stage == "frag":
        preamble = INJECT_FRAGMENT_BASE
        if "kTLAS[" in source:
            preamble += INJECT_FRAGMENT_RAY_QUERY
        preamble += INJECT_FRAGMENT_BINDLESS
        for marker, helper in FRAGMENT_HELPERS.items():
            if marker in source:
                preamble += helper
        return f"{preamble}\n{source}"

    if stage in {"vert", "comp", "tesc", "tese"}:
        return f"{INJECT_VERTEX_OR_COMPUTE}\n{source}"

    return f"#version 460\n{source}"


def discover_shader_paths(scan_roots: list[Path]) -> list[Path]:
    found: dict[str, Path] = {}
    for root in scan_roots:
        if root.is_file():
            text = root.read_text(encoding="utf-8", errors="ignore")
            for shader in LOAD_SHADER_RE.findall(text):
                found[shader] = REPO_ROOT / shader
            continue
        for file in root.rglob("*"):
            if file.suffix.lower() not in {".cpp", ".h", ".hpp", ".cc"}:
                continue
            text = file.read_text(encoding="utf-8", errors="ignore")
            for shader in LOAD_SHADER_RE.findall(text):
                found[shader] = REPO_ROOT / shader
    return [found[key] for key in sorted(found)]


def make_jobs(shader_paths: list[Path]) -> list[ShaderJob]:
    jobs: list[ShaderJob] = []
    for source_path in shader_paths:
        stage, mali_stage_flag = stage_for_path(source_path)
        source = read_shader_file(source_path)
        patched = inject_lvk_preamble(source, stage)
        bool_specs = list(BOOL_SPEC_RE.finditer(patched))

        if bool_specs:
            for match in bool_specs:
                spec_id, name, _default = match.groups()
                for value in ("true", "false"):
                    fixed = BOOL_SPEC_RE.sub(
                        lambda m, v=value: f"const bool {m.group(2)} = {v};" if m.group(1) == spec_id else m.group(0),
                        patched,
                    )
                    jobs.append(
                        ShaderJob(
                            source_path=source_path.relative_to(REPO_ROOT),
                            stage=stage,
                            mali_stage_flag=mali_stage_flag,
                            variant=f"spec{spec_id}_{name}_{value}",
                            patched_source=fixed,
                        )
                    )
        else:
            jobs.append(
                ShaderJob(
                    source_path=source_path.relative_to(REPO_ROOT),
                    stage=stage,
                    mali_stage_flag=mali_stage_flag,
                    variant="",
                    patched_source=patched,
                )
            )
    return jobs


def parse_report(path: Path) -> dict[str, str]:
    text = path.read_text(encoding="utf-8", errors="ignore")
    result = {
        "work_registers": "",
        "occupancy": "",
        "uniform_registers": "",
        "stack_use": "",
        "arithmetic_16bit": "",
        "bound": "",
        "cycles_A": "",
        "cycles_FMA": "",
        "cycles_CVT": "",
        "cycles_SFU": "",
        "cycles_LS": "",
        "cycles_V": "",
        "cycles_T": "",
    }

    if match := re.search(r"Work registers:\s+(\d+).*?at\s+(\d+)% occupancy", text):
        result["work_registers"] = match.group(1)
        result["occupancy"] = f"{match.group(2)}%"
    if match := re.search(r"Uniform registers:\s+(\d+)", text):
        result["uniform_registers"] = match.group(1)
    if match := re.search(r"Stack use:\s+(\w+)", text):
        result["stack_use"] = match.group(1)
    if match := re.search(r"16-bit arithmetic:\s+([^\n]+)", text):
        result["arithmetic_16bit"] = match.group(1).strip()

    header_match = re.search(r"^\s*A\s+FMA\s+CVT\s+SFU\s+LS\s+(?:V\s+)?T\s+Bound\s*$", text, re.MULTILINE)
    row_match = re.search(
        r"Total instruction cycles:\s+"
        r"([0-9.]+)\s+([0-9.]+)\s+([0-9.]+)\s+([0-9.]+)\s+([0-9.]+)\s+"
        r"(?:(?:([0-9.]+)\s+))?"
        r"([0-9.]+)\s+([A-Z, ]+)",
        text,
    )
    if header_match and row_match:
        result["cycles_A"] = row_match.group(1)
        result["cycles_FMA"] = row_match.group(2)
        result["cycles_CVT"] = row_match.group(3)
        result["cycles_SFU"] = row_match.group(4)
        result["cycles_LS"] = row_match.group(5)
        result["cycles_V"] = row_match.group(6) or ""
        result["cycles_T"] = row_match.group(7)
        result["bound"] = row_match.group(8).strip()

    return result


def write_summary(rows: list[dict[str, str]], out_dir: Path) -> None:
    fieldnames = [
        "shader",
        "variant",
        "stage",
        "status",
        "work_registers",
        "occupancy",
        "uniform_registers",
        "stack_use",
        "arithmetic_16bit",
        "bound",
        "cycles_A",
        "cycles_FMA",
        "cycles_CVT",
        "cycles_SFU",
        "cycles_LS",
        "cycles_V",
        "cycles_T",
        "report",
        "message",
    ]
    csv_path = out_dir / "summary.csv"
    with csv_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    md_path = out_dir / "summary.md"
    with md_path.open("w", encoding="utf-8") as f:
        f.write("| Shader | Variant | Stage | Status | Work Regs | Occupancy | Bound | LS | T | A | Report |\n")
        f.write("|---|---|---:|---|---:|---:|---|---:|---:|---:|---|\n")
        for row in rows:
            f.write(
                f"| `{row['shader']}` | `{row['variant']}` | {row['stage']} | {row['status']} | "
                f"{row['work_registers']} | {row['occupancy']} | {row['bound']} | "
                f"{row['cycles_LS']} | {row['cycles_T']} | {row['cycles_A']} | `{row['report']}` |\n"
            )


def main() -> int:
    parser = argparse.ArgumentParser(description="Run Mali Offline Compiler on Renderer shaders.")
    parser.add_argument("--malioc", default=str(DEFAULT_MALIOC), help="Path to malioc.exe")
    parser.add_argument("--core", default="Mali-G1", help="Mali core name accepted by malioc")
    parser.add_argument(
        "--scan-root",
        action="append",
        default=None,
        help="Source tree/file to scan. Defaults to Renderer and shared.",
    )
    parser.add_argument("--out-dir", default=None, help="Output directory; default build/shader-analysis/<core>")
    parser.add_argument("--format", choices=["text", "json"], default="text", help="MaliOC report format")
    parser.add_argument("--keep-going", action="store_true", default=True, help="Continue after shader failures")
    args = parser.parse_args()

    malioc = Path(args.malioc)
    if not malioc.exists():
        print(f"malioc not found: {malioc}", file=sys.stderr)
        return 2

    glslang = shutil.which("glslangValidator")
    if not glslang:
        print("glslangValidator not found in PATH", file=sys.stderr)
        return 2

    spirv_dis = shutil.which("spirv-dis")
    if not spirv_dis:
        print("spirv-dis not found in PATH", file=sys.stderr)
        return 2

    safe_core = re.sub(r"[^A-Za-z0-9_.-]+", "-", args.core)
    out_dir = Path(args.out_dir) if args.out_dir else REPO_ROOT / "build" / "shader-analysis" / safe_core
    out_dir.mkdir(parents=True, exist_ok=True)
    intermediate_dir = out_dir / "intermediate"
    intermediate_dir.mkdir(parents=True, exist_ok=True)

    scan_args = args.scan_root or ["Renderer", "shared"]
    scan_roots = [(REPO_ROOT / p).resolve() for p in scan_args]
    shader_paths = discover_shader_paths(scan_roots)
    jobs = make_jobs(shader_paths)

    print(f"Discovered {len(shader_paths)} shader files, {len(jobs)} MaliOC jobs")
    rows: list[dict[str, str]] = []

    for job in jobs:
        ext = f".{job.stage}"
        patched_path = intermediate_dir / f"{job.stem}.patched{ext}"
        spv_path = intermediate_dir / f"{job.stem}.spv"
        asm_path = intermediate_dir / f"{job.stem}.spvasm"
        report_suffix = "json" if args.format == "json" else "txt"
        report_path = out_dir / f"{job.stem}.{report_suffix}"

        patched_path.write_text(job.patched_source, encoding="utf-8")
        row = {
            "shader": job.source_path.as_posix(),
            "variant": job.variant,
            "stage": job.stage,
            "status": "ok",
            "work_registers": "",
            "occupancy": "",
            "uniform_registers": "",
            "stack_use": "",
            "arithmetic_16bit": "",
            "bound": "",
            "cycles_A": "",
            "cycles_FMA": "",
            "cycles_CVT": "",
            "cycles_SFU": "",
            "cycles_LS": "",
            "cycles_V": "",
            "cycles_T": "",
            "report": report_path.name,
            "message": "",
        }

        compile_cmd = [
            glslang,
            "-V",
            "--target-env",
            "vulkan1.3",
            "-S",
            job.stage,
            str(patched_path),
            "-o",
            str(spv_path),
        ]
        compiled = run(compile_cmd, REPO_ROOT)
        if compiled.returncode != 0:
            row["status"] = "glslang_failed"
            row["message"] = compiled.stdout.strip().replace("\n", " | ")
            rows.append(row)
            print(f"[glslang failed] {job.source_path} {job.variant}")
            continue

        disassembled = run([spirv_dis, str(spv_path), "-o", str(asm_path)], REPO_ROOT)
        if disassembled.returncode != 0:
            row["status"] = "spirv_dis_failed"
            row["message"] = disassembled.stdout.strip().replace("\n", " | ")
            rows.append(row)
            print(f"[spirv-dis failed] {job.source_path} {job.variant}")
            continue

        mali_cmd = [
            str(malioc),
            "--vulkan",
            "--spirv",
            job.mali_stage_flag,
            "--core",
            args.core,
            "--detailed",
            "--format",
            args.format,
            "-o",
            str(report_path),
            str(spv_path),
        ]
        mali = run(mali_cmd, REPO_ROOT)
        if mali.returncode != 0:
            row["status"] = "malioc_failed"
            row["message"] = mali.stdout.strip().replace("\n", " | ")
            rows.append(row)
            print(f"[malioc failed] {job.source_path} {job.variant}")
            continue

        if args.format == "text":
            row.update(parse_report(report_path))
        rows.append(row)
        print(f"[ok] {job.source_path} {job.variant or 'default'}")

    write_summary(rows, out_dir)
    failures = [row for row in rows if row["status"] != "ok"]
    print(f"Reports: {out_dir}")
    print(f"Summary: {out_dir / 'summary.md'}")
    if failures:
        print(f"Failures: {len(failures)}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
