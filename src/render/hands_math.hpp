#pragma once
#include "input/puzzle_aim.hpp"
#include <array>

namespace witness::hands {
// Row-major storage, column vectors: clip = clip_from_model * float4(position, 1).
// Tracking, eye transforms, and asset coordinates are all in meters.
using Mat4 = std::array<float, 16>;

struct HandDraw {
    bool valid{};
    Mat4 clip_from_model{}, normal_from_model{};
};

inline Mat4 identity() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

inline bool finite(const Mat4& matrix) {
    return std::all_of(matrix.begin(), matrix.end(), [](float value) { return std::isfinite(value); });
}

inline Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 result{};
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned column = 0; column < 4; ++column)
            for (unsigned k = 0; k < 4; ++k)
                result[row * 4 + column] += a[row * 4 + k] * b[k * 4 + column];
    return result;
}

inline Mat4 from_3x4(const float (&matrix)[3][4]) {
    Mat4 result = identity();
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned column = 0; column < 4; ++column)
            result[row * 4 + column] = matrix[row][column];
    return result;
}

// Caller must provide a validated rigid transform. make_draw validates before inversion.
inline Mat4 rigid_inverse(const Mat4& matrix) {
    Mat4 result = identity();
    for (unsigned row = 0; row < 3; ++row) {
        for (unsigned column = 0; column < 3; ++column) {
            result[row * 4 + column] = matrix[column * 4 + row];
            result[row * 4 + 3] -= matrix[column * 4 + row] * matrix[column * 4 + 3];
        }
    }
    return result;
}

// Historical OpenVR raw tangents use negative top / positive bottom. Eye space is
// right-handed, +Y up, forward -Z. DirectX clip depth is [0, 1]. Invalid input yields zero.
inline Mat4 projection_raw(float left, float right, float top, float bottom, float near_z = .02f,
                           float far_z = 1000.f) {
    if (!std::isfinite(left) || !std::isfinite(right) || !std::isfinite(top) || !std::isfinite(bottom) ||
        !std::isfinite(near_z) || !std::isfinite(far_z) || right - left < .000001f ||
        bottom - top < .000001f || near_z <= 0 || far_z <= near_z)
        return {};
    Mat4 result{};
    result[0] = 2.f / (right - left);
    result[2] = (right + left) / (right - left);
    result[5] = 2.f / (bottom - top);
    result[6] = -(bottom + top) / (bottom - top);
    result[10] = -far_z / (far_z - near_z);
    result[11] = -far_z * near_z / (far_z - near_z);
    result[14] = -1.f;
    return finite(result) ? result : Mat4{};
}

// The supplied Godot wrappers retain a mirrored eight-degree fit about model Z.
inline Mat4 model_spread(bool left) {
    constexpr float radians = 8.f * 3.14159265358979323846f / 180.f;
    const float c = std::cos(radians), s = std::sin(left ? radians : -radians);
    return {c, -s, 0, 0, s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

// Legacy raw-controller alignment retained for fixture comparison. Production must
// use the runtime palm component: controller device axes do not describe a hand.
inline Mat4 alignment(bool left) {
    const Mat4 forward{1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1};
    return multiply(forward, model_spread(left));
}

// Valve's openxr_handmodel component follows OpenXR palm_ext, not the hand-joint
// palm frame. palm_ext -Z follows a straight index finger; +X is palm-facing for
// left and back-facing for right. The source mesh has +Y fingers, +Z palm-facing,
// right thumb +X / left thumb -X. This proper rotation maps both thumbs to +Y.
// https://github.com/ValveSoftware/openvr/wiki/Render-Model-Reference
// https://registry.khronos.org/OpenXR/specs/1.0/man/html/XR_EXT_palm_pose.html
inline Mat4 palm_basis(bool left) {
    const float side = left ? 1.f : -1.f;
    return {0, 0, side, 0, -side, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, 1};
}

inline Mat4 palm_alignment(bool left) {
    return multiply(palm_basis(left), model_spread(left));
}

// component_local is mTrackingToComponentLocal for openxr_handmodel in meters.
// Do not substitute handgrip/grip/tip: their orientation contracts differ.
// The returned matrix maps glove coordinates into raw tracked-device coordinates.
// Invalid/non-rigid components clear the output and hide that hand at the caller.
inline bool component_alignment(const float (&component_local)[3][4], bool left, Mat4& result) {
    result = {};
    input::TrackedPose component{};
    component.valid = component.connected = true;
    component.tracking_result = 200;
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned column = 0; column < 4; ++column)
            component.matrix[row][column] = component_local[row][column];
    if (!input::valid_pose(component))
        return false;
    result = multiply(from_3x4(component_local), palm_alignment(left));
    return true;
}

inline bool affine(const Mat4& matrix) {
    return finite(matrix) && matrix[12] == 0 && matrix[13] == 0 && matrix[14] == 0 && matrix[15] == 1;
}

// Normal transform excludes translation and supports nonuniform model alignment scale.
inline bool normal_matrix(const Mat4& matrix, Mat4& normal) {
    if (!affine(matrix))
        return false;
    const float a = matrix[0], b = matrix[1], c = matrix[2], d = matrix[4], e = matrix[5], f = matrix[6],
                g = matrix[8], h = matrix[9], i = matrix[10];
    const float determinant = a * (e * i - f * h) + b * (f * g - d * i) + c * (d * h - e * g);
    if (!std::isfinite(determinant) || std::abs(determinant) < .00000001f)
        return false;
    normal = {(e * i - f * h) / determinant,
              (f * g - d * i) / determinant,
              (d * h - e * g) / determinant,
              0,
              (c * h - b * i) / determinant,
              (a * i - c * g) / determinant,
              (b * g - a * h) / determinant,
              0,
              (b * f - c * e) / determinant,
              (c * d - a * f) / determinant,
              (a * e - b * d) / determinant,
              0,
              0,
              0,
              0,
              1};
    return finite(normal);
}

// head and hand must come from the same compositor pose sample. Device-role and
// frame freshness validation belongs to the caller. Invalid data hides only this hand.
inline HandDraw make_draw(const input::TrackedPose& head, const input::TrackedPose& hand,
                          const float (&eye_to_head)[3][4], const Mat4& projection,
                          const Mat4& model_alignment) {
    input::TrackedPose eye{};
    eye.valid = eye.connected = true;
    eye.tracking_result = 200;
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned column = 0; column < 4; ++column)
            eye.matrix[row][column] = eye_to_head[row][column];
    if (!input::valid_pose(head) || !input::valid_pose(hand) || !input::valid_pose(eye) ||
        !affine(model_alignment) || !finite(projection) || projection[0] <= 0 || projection[5] <= 0 ||
        projection[10] > -1 || projection[11] >= 0 || projection[14] != -1 || projection[15] != 0)
        return {};
    const Mat4 eye_from_tracking =
        multiply(rigid_inverse(from_3x4(eye_to_head)), rigid_inverse(from_3x4(head.matrix)));
    const Mat4 eye_from_model = multiply(multiply(eye_from_tracking, from_3x4(hand.matrix)), model_alignment);
    HandDraw result{};
    if (!normal_matrix(eye_from_model, result.normal_from_model))
        return {};
    result.clip_from_model = multiply(projection, eye_from_model);
    if (!finite(result.clip_from_model))
        return {};
    result.valid = true;
    return result;
}
} // namespace witness::hands
