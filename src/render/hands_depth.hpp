#pragma once
#include <cmath>

namespace witness::hands {
namespace detail {
inline bool depth_candidate(const float (&matrix)[16], bool transpose, bool reversed, float& a,
                            float& b) noexcept {
    const auto at = [&](unsigned row, unsigned column) {
        return static_cast<double>(matrix[transpose ? column * 4 + row : row * 4 + column]);
    };
    const double w[3]{at(3, 0), at(3, 1), at(3, 2)};
    const double z[3]{at(2, 0), at(2, 1), at(2, 2)};
    double w_squared = 0, zw = 0, z_squared = 0;
    for (unsigned axis = 0; axis < 3; ++axis) {
        w_squared += w[axis] * w[axis];
        zw += z[axis] * w[axis];
        z_squared += z[axis] * z[axis];
    }
    const double length = std::sqrt(w_squared);
    if (length < .95 || length > 1.05 || std::abs(at(3, 3)) > .00001)
        return false;
    const double candidate_a = zw / w_squared;
    const double candidate_b = at(2, 3) / length;
    double residual_squared = 0;
    for (unsigned axis = 0; axis < 3; ++axis) {
        const double residual = z[axis] - candidate_a * w[axis];
        residual_squared += residual * residual;
    }
    if (residual_squared > .00000001 * z_squared || candidate_a < -.1 || candidate_a > 1.1 ||
        candidate_b == 0 || std::abs(candidate_b) >= 10 || (reversed ? candidate_b < 0 : candidate_b > 0))
        return false;

    // depth = A + B / distance. A zero far denominator describes infinite far,
    // which is legitimate for both ordinary and reversed perspective depth.
    const double near_denominator = (reversed ? 1.0 : 0.0) - candidate_a;
    if (near_denominator == 0)
        return false;
    const double near_distance = candidate_b / near_denominator;
    if (near_distance < .001 || near_distance > 10)
        return false;
    const double far_denominator = (reversed ? 0.0 : 1.0) - candidate_a;
    if (far_denominator != 0) {
        const double far_distance = candidate_b / far_denominator;
        if (far_distance <= near_distance)
            return false;
    }
    a = static_cast<float>(candidate_a);
    b = static_cast<float>(candidate_b);
    return true;
}
} // namespace detail

// Extract DirectX [0,1] perspective depth as A + B / positive eye-forward meters.
// Native camera axes may be rotated; only the depth/forward row relationship is
// assumed. Try row-major column-vector storage first, then its transpose.
// A matrix that fits both interpretations uses the first. Invalid data clears both
// outputs; callers must hide/skip depth testing rather than guess coefficients.
inline bool depth_projection(const float (&matrix)[16], bool reversed, float& a, float& b) noexcept {
    a = b = 0;
    for (float value : matrix)
        if (!std::isfinite(value))
            return false;
    return detail::depth_candidate(matrix, false, reversed, a, b) ||
           detail::depth_candidate(matrix, true, reversed, a, b);
}
} // namespace witness::hands
