"""Build the skinned C++ mesh payload from the validated, local glove GLBs.

The runtime needs no asset parser. Refuse changed topology or nonidentity mesh
ancestors instead of silently baking a different coordinate system. --check is
read-only and verifies that the committed header matches both GLBs exactly.
"""

import argparse
import json
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "src/render/hand_mesh_data.hpp"


def identity():
    return [1.0 if i // 4 == i % 4 else 0.0 for i in range(16)]


def multiply(a, b):
    return [
        sum(a[row * 4 + k] * b[k * 4 + column] for k in range(4)) for row in range(4) for column in range(4)
    ]


def local_matrix(node):
    if "matrix" in node or node.get("scale", [1, 1, 1]) != [1, 1, 1]:
        raise ValueError("Expected an unscaled TRS joint")
    x, y, z, w = node.get("rotation", [0, 0, 0, 1])
    if abs(x * x + y * y + z * z + w * w - 1) > 0.00001:
        raise ValueError("Joint rotation is not a unit quaternion")
    tx, ty, tz = node.get("translation", [0, 0, 0])
    return [
        1 - 2 * (y * y + z * z),
        2 * (x * y - z * w),
        2 * (x * z + y * w),
        tx,
        2 * (x * y + z * w),
        1 - 2 * (x * x + z * z),
        2 * (y * z - x * w),
        ty,
        2 * (x * z - y * w),
        2 * (y * z + x * w),
        1 - 2 * (x * x + y * y),
        tz,
        0,
        0,
        0,
        1,
    ]


def grip_bend(name, side):
    """Authored axis-angle vectors in local joint space, not measured fingers.

    Local +X curls +Y finger bones toward the +Z palm. The thumb also opposes
    inward with a mirrored local Z rotation. All local translations are kept.
    """
    if name == "ThumbMetacarpal":
        return [0.5, 0, -0.5 if side == "left" else 0.5]
    if name in ("ThumbProximal", "ThumbDistal"):
        return [math.radians(35), 0, 0]
    for finger in ("Index", "Middle", "Ring", "Little"):
        angles = (55, 75, 40) if finger == "Little" else (60, 80, 45)
        for suffix, angle in zip(("Proximal", "Intermediate", "Distal"), angles):
            if name == finger + suffix:
                return [math.radians(angle), 0, 0]
    return [0, 0, 0]


def read_mesh(path):
    raw = path.read_bytes()
    if struct.unpack_from("<III", raw) != (0x46546C67, 2, len(raw)):
        raise ValueError("Invalid GLB header")
    json_size, json_type = struct.unpack_from("<II", raw, 12)
    if json_type != 0x4E4F534A:
        raise ValueError("Expected GLB JSON")
    document = json.loads(raw[20 : 20 + json_size])
    bin_size, bin_type = struct.unpack_from("<II", raw, 20 + json_size)
    blob = raw[28 + json_size :]
    if bin_type != 0x004E4942 or len(blob) != bin_size:
        raise ValueError("Expected single GLB binary buffer")
    if len(document["meshes"]) != 1 or len(document["meshes"][0]["primitives"]) != 1:
        raise ValueError("Expected one glove primitive")
    primitive = document["meshes"][0]["primitives"][0]
    if primitive.get("mode", 4) != 4 or primitive.get("targets"):
        raise ValueError("Expected fixed triangle mesh")

    nodes = document["nodes"]
    parents = {child: i for i, node in enumerate(nodes) for child in node.get("children", [])}
    mesh_nodes = [i for i, node in enumerate(nodes) if "mesh" in node]
    if len(mesh_nodes) != 1:
        raise ValueError("Expected one mesh instance")
    node_index = mesh_nodes[0]
    while True:
        node = nodes[node_index]
        if any(key in node for key in ("matrix", "translation", "rotation", "scale")):
            raise ValueError("Mesh ancestors must keep the prepared identity transform")
        if node_index not in parents:
            break
        node_index = parents[node_index]

    def accessor(index, component, kind):
        entry = document["accessors"][index]
        if (entry["componentType"], entry["type"]) != (component, kind):
            raise ValueError("Unexpected accessor format")
        if entry.get("sparse") or entry.get("normalized"):
            raise ValueError("Unexpected accessor encoding")
        view = document["bufferViews"][entry["bufferView"]]
        if view.get("buffer", 0) != 0:
            raise ValueError("External buffer")
        count = {"VEC3": 3, "VEC4": 4, "SCALAR": 1, "MAT4": 16}[kind]
        element = struct.Struct("<" + {5126: "f", 5123: "H", 5121: "B"}[component] * count)
        stride = view.get("byteStride", element.size)
        start = view.get("byteOffset", 0) + entry.get("byteOffset", 0)
        end = start + (entry["count"] - 1) * stride + element.size
        if stride < element.size or end > view.get("byteOffset", 0) + view["byteLength"]:
            raise ValueError("Invalid accessor bounds")
        if start < 0 or end > len(blob):
            raise ValueError("Accessor outside binary buffer")
        return [element.unpack_from(blob, start + i * stride) for i in range(entry["count"])]

    positions = accessor(primitive["attributes"]["POSITION"], 5126, "VEC3")
    normals = accessor(primitive["attributes"]["NORMAL"], 5126, "VEC3")
    vertex_joints = accessor(primitive["attributes"]["JOINTS_0"], 5121, "VEC4")
    weights = accessor(primitive["attributes"]["WEIGHTS_0"], 5126, "VEC4")
    indices = [item[0] for item in accessor(primitive["indices"], 5123, "SCALAR")]
    if any(len(values) != 1368 for values in (positions, normals, vertex_joints, weights, indices)):
        raise ValueError("Prepared glove geometry count changed")
    if any(index >= len(positions) for index in indices):
        raise ValueError("Invalid vertex index")
    for position, normal in zip(positions, normals):
        if not all(math.isfinite(value) for value in (*position, *normal)):
            raise ValueError("Nonfinite vertex")
        if max(abs(value) for value in position) > 0.25:
            raise ValueError("Prepared meter-scale glove bounds changed")
        if not 0.98 < sum(value * value for value in normal) < 1.02:
            raise ValueError("Invalid unit normal")
    skin = document["skins"][nodes[mesh_nodes[0]]["skin"]]
    joints = skin["joints"]
    if len(joints) != 26 or len(set(joints)) != 26:
        raise ValueError("Expected 26 distinct skin joints")
    for influences, values in zip(vertex_joints, weights):
        if any(index >= len(joints) for index in influences):
            raise ValueError("Invalid skin joint index")
        if any(not math.isfinite(weight) or weight < 0 or weight > 1 for weight in values):
            raise ValueError("Invalid skin weight")
        if abs(sum(values) - 1) > 0.00001:
            raise ValueError("Skin weights must sum to one")
    binds = accessor(skin["inverseBindMatrices"], 5126, "MAT4")
    if len(binds) != 26:
        raise ValueError("Expected one inverse bind per joint")
    # glTF MAT4 is column-major; every generated/runtime transform is row-major.
    binds = [[matrix[column * 4 + row] for row in range(4) for column in range(4)] for matrix in binds]
    order = []
    joint_parents = []
    locals_ = []
    for index in joints:
        parent = parents.get(index)
        joint_parents.append(joints.index(parent) if parent in joints else -1)
        locals_.append(local_matrix(nodes[index]))
        while parent is not None and parent not in joints:
            if local_matrix(nodes[parent]) != identity():
                raise ValueError("Skin ancestors must preserve the mesh coordinate system")
            parent = parents.get(parent)
    globals_ = {}
    while len(order) < len(joints):
        remaining = [
            i for i, parent in enumerate(joint_parents) if i not in order and (parent < 0 or parent in order)
        ]
        if not remaining:
            raise ValueError("Cycle in joint hierarchy")
        for i in remaining:
            parent = joint_parents[i]
            globals_[i] = multiply(globals_[parent], locals_[i]) if parent >= 0 else locals_[i]
            order.append(i)
            if max(abs(a - b) for a, b in zip(multiply(globals_[i], binds[i]), identity())) > 0.000002:
                raise ValueError("Inverse bind disagrees with rest hierarchy")
    side = "left" if path.stem.startswith("left") else "right"
    prefix = side.title()
    names = [nodes[index]["name"] for index in joints]
    if any(not name.startswith(prefix) for name in names):
        raise ValueError("Unexpected glove joint names")
    bends = [grip_bend(name[len(prefix) :], side) for name in names]
    # Both mirrored exports must have outward CCW winding, not mirrored indices.
    for i in range(0, len(indices), 3):
        a, b, c = (positions[index] for index in indices[i : i + 3])
        u = [b[j] - a[j] for j in range(3)]
        v = [c[j] - a[j] for j in range(3)]
        cross = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        if sum(cross[j] * normals[indices[i]][j] for j in range(3)) <= 0:
            raise ValueError("Glove winding disagrees with outward normals")
    return {
        "vertices": [
            (positions[index], normals[index], vertex_joints[index], weights[index]) for index in indices
        ],
        "bones": list(zip(names, joint_parents, locals_, binds, bends)),
        "order": order,
    }


def float_literal(value):
    text = format(value, ".9g")
    if "." not in text and "e" not in text:
        text += ".0"
    return text + "f"


def generate():
    lines = [
        "// Generated by scripts/embed-hand-meshes.py. Do not edit by hand.",
        "// Quaternius glove (CC0); see assets/hands/source/LICENSE.md.",
        "#pragma once",
        "#include <array>",
        "#include <cstdint>",
        "",
        "namespace witness::hands::mesh {",
        "struct Vertex {",
        "    float position[3];",
        "    float normal[3];",
        "};",
        "",
        "struct SkinVertex {",
        "    Vertex rest;",
        "    std::uint8_t joints[4];",
        "    float weights[4];",
        "};",
        "",
        "struct Bone {",
        "    int parent;",
        "    std::array<float, 16> local;",
        "    std::array<float, 16> inverse_bind;",
        "    std::array<float, 3> bend; // Local axis-angle vector in radians at full grip.",
        "};",
        "",
        "// clang-format off",
    ]
    for side in ("left", "right"):
        mesh = read_mesh(ROOT / f"assets/hands/{side}_hand.glb")
        lines.extend(
            [f"// Source: assets/hands/{side}_hand.glb", f"inline constexpr SkinVertex {side}[] = {{"]
        )
        for position, normal, joints, weights in mesh["vertices"]:
            fields = [
                ", ".join(float_literal(value) for value in values) for values in (position, normal, weights)
            ]
            joint_values = ", ".join(str(joint) for joint in joints)
            lines.append(
                "    {{{"
                + fields[0]
                + "}, {"
                + fields[1]
                + "}}, {"
                + joint_values
                + "}, {"
                + fields[2]
                + "}},"
            )
        lines.append("};")
        lines.append(f"inline constexpr Bone {side}_bones[] = {{")
        for name, parent, local, inverse_bind, bend in mesh["bones"]:
            lines.append(f"    // {name}")
            fields = [
                ", ".join(float_literal(value) for value in values) for values in (local, inverse_bind, bend)
            ]
            lines.append(
                "    {" + str(parent) + ", {" + fields[0] + "}, {" + fields[1] + "}, {" + fields[2] + "}},"
            )
        lines.append("};")
        lines.append(
            f"inline constexpr std::uint8_t {side}_order[] = {{"
            + ", ".join(str(index) for index in mesh["order"])
            + "};"
        )
    lines.extend(["// clang-format on", "} // namespace witness::hands::mesh", ""])
    return "\n".join(lines)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    options = parser.parse_args()
    generated = generate()
    if options.check:
        if not OUTPUT.exists() or OUTPUT.read_text(encoding="ascii") != generated:
            raise SystemExit("Embedded glove meshes differ; run scripts/embed-hand-meshes.py")
        print(
            "Embedded gloves match both GLBs: 456 outward-wound triangles, 26 joints, normalized weights and inverse binds per hand"
        )
    else:
        OUTPUT.write_text(generated, encoding="ascii", newline="\n")
        print(f"Generated {OUTPUT.relative_to(ROOT)}")
