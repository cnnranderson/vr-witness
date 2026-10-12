#pragma once

#include "hand_mesh_data.hpp"
#include "hands_math.hpp"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace witness::hands::skin {

inline constexpr unsigned vertex_count = 1368;
inline constexpr unsigned bone_count = 26;
static_assert(std::size(mesh::left) == vertex_count && std::size(mesh::right) == vertex_count);
static_assert(std::size(mesh::left_bones) == bone_count && std::size(mesh::right_bones) == bone_count);
static_assert(std::size(mesh::left_order) == bone_count && std::size(mesh::right_order) == bone_count);

struct Pose {
    std::array<Mat4, bone_count> global{};
    std::array<Mat4, bone_count> deformation{};
};

inline bool normalize_grip(float input, float& output) {
    if (!std::isfinite(input))
        return false;
    output = std::clamp(input, 0.f, 1.f);
    return true;
}

// Exponentiating each joint's axis-angle preserves its translation and length at
// every intermediate grip. It does not interpolate already deformed vertices.
inline Mat4 rotation(const std::array<float, 3>& bend, float grip) {
    const float length = std::sqrt(bend[0] * bend[0] + bend[1] * bend[1] + bend[2] * bend[2]);
    if (length == 0 || grip == 0)
        return identity();
    const float x = bend[0] / length, y = bend[1] / length, z = bend[2] / length;
    const float c = std::cos(length * grip), s = std::sin(length * grip), t = 1 - c;
    return {t * x * x + c,
            t * x * y - s * z,
            t * x * z + s * y,
            0,
            t * x * y + s * z,
            t * y * y + c,
            t * y * z - s * x,
            0,
            t * x * z - s * y,
            t * y * z + s * x,
            t * z * z + c,
            0,
            0,
            0,
            0,
            1};
}

inline bool make_pose(bool left, float input_grip, Pose& output) {
    float grip;
    if (!normalize_grip(input_grip, grip))
        return false;
    const auto* bones = left ? mesh::left_bones : mesh::right_bones;
    const auto* order = left ? mesh::left_order : mesh::right_order;
    std::array<bool, bone_count> animated{};
    for (unsigned n = 0; n != bone_count; ++n) {
        const unsigned index = order[n];
        const auto& bone = bones[index];
        const auto local = multiply(bone.local, rotation(bone.bend, grip));
        output.global[index] = bone.parent < 0 ? local : multiply(output.global[bone.parent], local);
        animated[index] = grip != 0 && (bone.bend[0] != 0 || bone.bend[1] != 0 || bone.bend[2] != 0 ||
                                        (bone.parent >= 0 && animated[bone.parent]));
        // Preserve the palm/wrist exactly, including rest vertices with full
        // weight there. Bind matrices are verified against the exported rest.
        output.deformation[index] =
            animated[index] ? multiply(output.global[index], bone.inverse_bind) : identity();
    }
    return true;
}

inline std::array<float, 3> transform(const Mat4& matrix, const float (&value)[3], bool position) {
    std::array<float, 3> result{};
    for (unsigned row = 0; row != 3; ++row)
        result[row] = matrix[row * 4] * value[0] + matrix[row * 4 + 1] * value[1] +
                      matrix[row * 4 + 2] * value[2] + (position ? matrix[row * 4 + 3] : 0.f);
    return result;
}

// output owns space for vertex_count vertices. No allocation or persistent CPU
// mesh cache is used. GPU callers upload only when the clamped grip changes.
inline bool deform(bool left, float input_grip, mesh::Vertex* output) {
    float grip;
    if (!output || !normalize_grip(input_grip, grip))
        return false;
    const auto* source = left ? mesh::left : mesh::right;
    if (grip == 0) {
        for (unsigned i = 0; i != vertex_count; ++i)
            output[i] = source[i].rest;
        return true;
    }
    Pose pose;
    if (!make_pose(left, grip, pose))
        return false;
    for (unsigned i = 0; i != vertex_count; ++i) {
        const auto& vertex = source[i];
        mesh::Vertex skinned{};
        for (unsigned influence = 0; influence != 4; ++influence) {
            const float weight = vertex.weights[influence];
            if (weight == 0)
                continue;
            const auto& matrix = pose.deformation[vertex.joints[influence]];
            const auto position = transform(matrix, vertex.rest.position, true);
            const auto normal = transform(matrix, vertex.rest.normal, false);
            for (unsigned axis = 0; axis != 3; ++axis) {
                skinned.position[axis] += weight * position[axis];
                skinned.normal[axis] += weight * normal[axis];
            }
        }
        const float length_squared = skinned.normal[0] * skinned.normal[0] +
                                     skinned.normal[1] * skinned.normal[1] +
                                     skinned.normal[2] * skinned.normal[2];
        if (length_squared > 1e-12f) {
            const float inverse_length = 1.f / std::sqrt(length_squared);
            for (auto& value : skinned.normal)
                value *= inverse_length;
        } else {
            std::copy(std::begin(vertex.rest.normal), std::end(vertex.rest.normal), skinned.normal);
        }
        output[i] = skinned;
    }
    return true;
}

} // namespace witness::hands::skin
