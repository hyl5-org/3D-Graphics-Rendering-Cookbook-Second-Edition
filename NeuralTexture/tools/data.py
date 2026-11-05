"""glTF reference material and static scene preparation."""

import json, struct
import numpy as np
from PIL import Image


def linear(x):
    return np.where(x <= 0.04045, x / 12.92, ((x + 0.055) / 1.055) ** 2.4)


def srgb(x):
    return np.where(
        x <= 0.0031308, x * 12.92, 1.055 * np.maximum(x, 0) ** (1 / 2.4) - 0.055
    )


def prepare(path, size):
    doc = json.loads(path.read_text())
    # 将输入贴图统一到 size×size；缺失的贴图由材质默认值填充。
    def image(info, default):
        if not info:
            return np.broadcast_to(
                np.array(default, np.float32), (size, size, 4)
            ).copy()
        if info.get("texCoord", 0) or info.get("extensions"):
            raise ValueError("Only TEXCOORD_0 without texture transforms is supported")
        tex = doc["textures"][info["index"]]
        if "sampler" in tex:
            sampler = doc["samplers"][tex["sampler"]]
            if (
                sampler.get("wrapS", 10497) != 10497
                or sampler.get("wrapT", 10497) != 10497
            ):
                raise ValueError("Minimal encoder supports repeat wrapping only")
        uri = doc["images"][tex["source"]]["uri"]
        return (
            np.asarray(
                Image.open(path.parent / uri)
                .convert("RGBA")
                .resize((size, size), Image.Resampling.LANCZOS),
                np.float32,
            )
            / 255
        )

    targets = []
    for m in doc["materials"]:
        p = m.get("pbrMetallicRoughness", {})
        b = image(p.get("baseColorTexture"), [1, 1, 1, 1])
        # Base Color 的 RGB 在转到线性空间后乘材质因子，训练目标再编码回 sRGB；alpha 保持线性。
        factor = np.array(p.get("baseColorFactor", [1] * 4))
        b[..., :3] = srgb(linear(b[..., :3]) * factor[:3])
        b[..., 3] *= factor[3]
        # 切线空间法线应用 scale 并归一化；只保存 XY，运行时重建正向 Z。
        n = image(m.get("normalTexture"), [0.5, 0.5, 1, 1])[..., :2] * 2 - 1
        n *= m.get("normalTexture", {}).get("scale", 1)
        z = np.sqrt(np.maximum(1 - (n * n).sum(-1, keepdims=True), 0))
        xyz = np.concatenate([n, z], -1)
        xyz /= np.maximum(np.linalg.norm(xyz, axis=-1, keepdims=True), 1e-6)
        # glTF 的 G 通道存粗糙度，B 通道存金属度，AO 从独立贴图的 R 通道读取。
        mr = image(p.get("metallicRoughnessTexture"), [1, 1, 1, 1])
        r = mr[..., 1:2] * p.get("roughnessFactor", 1)
        metal = mr[..., 2:3] * p.get("metallicFactor", 1)
        ao = image(m.get("occlusionTexture"), [1, 1, 1, 1])[..., :1]
        ao = 1 - m.get("occlusionTexture", {}).get("strength", 1) * (1 - ao)
        # 每个材质合并为九通道：RGB、alpha、编码到 [0, 1] 的法线 XY、粗糙度、金属度、AO。
        #print(b.shape, xyz.shape, r.shape, metal.shape, ao.shape)
        targets.append(np.concatenate([b, xyz[..., :2] * 0.5 + 0.5, r, metal, ao], -1))
    #print(targets[0].shape) # 256x256x9
    # 输出形状为 [材质数, size, size, 9]，每组材质贴图对应一个训练目标。
    return doc, np.clip(np.stack(targets), 0, 1).astype(np.float32)


def geometry(doc, path, out):
    # 导出静态场景供 C++ 渲染使用；这一步与纹理网络的训练无关。
    buffers = [(path.parent / b["uri"]).read_bytes() for b in doc["buffers"]]

    def accessor(i):
        # 根据 glTF 的类型、偏移和步长读取缓冲区，兼容交错存储的顶点属性。
        a = doc["accessors"][i]
        if "sparse" in a:
            raise ValueError("Sparse accessors not supported")
        v = doc["bufferViews"][a["bufferView"]]
        dt = np.dtype(
            {5126: "<f4", 5125: "<u4", 5123: "<u2", 5121: "u1"}[a["componentType"]]
        )
        width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
        return np.ndarray(
            (a["count"], width),
            dt,
            buffers[v["buffer"]],
            v.get("byteOffset", 0) + a.get("byteOffset", 0),
            strides=(v.get("byteStride", dt.itemsize * width), dt.itemsize),
        ).copy()

    vertices = []
    indices = []
    draws = []
    nv = ni = 0

    def visit(i, parent):
        nonlocal nv, ni
        node = doc["nodes"][i]
        if "matrix" in node:
            local = np.array(node["matrix"]).reshape(4, 4).T
        else:
            x, y, z, w = node.get("rotation", [0, 0, 0, 1])
            local = np.eye(4)
            local[:3, :3] = np.array(
                [
                    [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                    [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                    [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
                ]
            ) @ np.diag(node.get("scale", [1] * 3))
            local[:3, 3] = node.get("translation", [0] * 3)
        # 累积节点变换，将实例烘焙到世界空间，运行时无需再遍历场景树。
        world = parent @ local
        if "mesh" in node:
            for p in doc["meshes"][node["mesh"]]["primitives"]:
                if p.get("mode", 4) != 4:
                    raise ValueError("Triangles only")
                a = p["attributes"]
                pos = accessor(a["POSITION"])
                n = accessor(a["NORMAL"])
                uv = accessor(a["TEXCOORD_0"])
                if (
                    "TANGENT" not in a
                    and "normalTexture" in doc["materials"][p["material"]]
                ):
                    raise ValueError("Normal-mapped geometry requires tangents")
                t = (
                    accessor(a["TANGENT"])
                    if "TANGENT" in a
                    else np.tile(np.array([1, 0, 0, 1], np.float32), (len(pos), 1))
                )
                pos = pos @ world[:3, :3].T + world[:3, 3]
                # 法线使用逆转置变换；这里按行向量计算，因此右乘逆矩阵。
                n = n @ np.linalg.inv(world[:3, :3])
                n /= np.linalg.norm(n, axis=1, keepdims=True)
                t[:, :3] = t[:, :3] @ world[:3, :3].T
                t[:, :3] /= np.linalg.norm(t[:, :3], axis=1, keepdims=True)
                t[:, 3] *= np.sign(np.linalg.det(world[:3, :3]))
                v = np.concatenate([pos, n, t, uv], 1).astype("<f4")
                idx = accessor(p["indices"]).reshape(-1).astype("<u4") + nv
                m = doc["materials"][p["material"]]
                if m.get("alphaMode", "OPAQUE") == "BLEND":
                    raise ValueError("Blended materials not supported")
                # 每个 draw 保存索引范围、材质编号及 alpha cutoff，关联对应的 latent 网格。
                draws.append(
                    struct.pack(
                        "<IIIf",
                        ni,
                        len(idx),
                        p["material"],
                        m.get("alphaCutoff", 0.5)
                        if m.get("alphaMode") == "MASK"
                        else -1,
                    )
                )
                vertices.append(v)
                indices.append(idx)
                nv += len(v)
                ni += len(idx)
        for child in node.get("children", []):
            visit(child, world)

    for node in doc["scenes"][doc.get("scene", 0)]["nodes"]:
        visit(node, np.eye(4))
    with out.open("wb") as f:
        # .scene 顺序：文件头、draw 表、顶点数据、全局索引；必须与 C++ 加载格式一致。
        f.write(struct.pack("<4I", 0x3143534E, len(draws), nv, ni))
        f.write(b"".join(draws))
        for v in vertices:
            f.write(v.tobytes())
        for i in indices:
            f.write(i.tobytes())
    print(
        "Scene:",
        nv,
        "vertices,",
        ni // 3,
        "triangles,",
        len(draws),
        "draws",
        flush=True,
    )
