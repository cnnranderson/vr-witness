#include "render/hands_depth.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
void check_impl(bool value, int line) {
    if (!value)
        throw std::runtime_error("Hand depth projection invariant failed at line " + std::to_string(line));
}

#define check(...) check_impl((__VA_ARGS__), __LINE__)

struct Matrix {
    float m[16]{};
};

bool close(float a, float b, float tolerance = .000001f) {
    return std::abs(a - b) < tolerance;
}

Matrix projection(float near_z, float far_z, bool reversed) {
    const float a = reversed ? -near_z / (far_z - near_z) : far_z / (far_z - near_z);
    const float b = (reversed ? 1.f : -1.f) * near_z * far_z / (far_z - near_z);
    return {{1.1f, 0, 0, 0, 0, 1.2f, 0, 0, 0, 0, -a, b, 0, 0, -1, 0}};
}

Matrix transposed(const Matrix& original) {
    Matrix result{};
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned column = 0; column < 4; ++column)
            result.m[row * 4 + column] = original.m[column * 4 + row];
    return result;
}

void rejected(const Matrix& matrix, bool reversed = false) {
    float a = 9, b = 9;
    check(!witness::hands::depth_projection(matrix.m, reversed, a, b));
    check(a == 0 && b == 0);
}
} // namespace

int main() {
    try {
        for (bool reversed : {false, true}) {
            const auto native = projection(.02f, 1000.f, reversed);
            for (const auto& matrix : {native, transposed(native)}) {
                float a{}, b{};
                check(witness::hands::depth_projection(matrix.m, reversed, a, b));
                check(close(a + b / .02f, reversed ? 1.f : 0.f));
                check(close(a + b / 1000.f, reversed ? 0.f : 1.f));
                const float near_sample = a + b / .2f, far_sample = a + b / 2.f;
                check(reversed ? near_sample > far_sample : near_sample < far_sample);
                rejected(matrix, !reversed);
            }

            float expected_a{}, expected_b{};
            check(witness::hands::depth_projection(native.m, reversed, expected_a, expected_b));
            // Rotate camera forward onto an arbitrary unit axis, not merely native -Z.
            Matrix rotated{{.8f, -.6f, 0, 0, .48f, .64f, -.6f, 0, .36f * expected_a, .48f * expected_a,
                            .8f * expected_a, expected_b, .36f, .48f, .8f, 0}};
            for (const auto& matrix : {rotated, transposed(rotated)}) {
                float a{}, b{};
                check(witness::hands::depth_projection(matrix.m, reversed, a, b));
                check(close(a, expected_a) && close(b, expected_b));
            }
            // Small normalization error is accepted while B remains expressed in meters.
            for (unsigned column = 0; column < 4; ++column) {
                rotated.m[8 + column] *= 1.02f;
                rotated.m[12 + column] *= 1.02f;
            }
            float a{}, b{};
            check(witness::hands::depth_projection(rotated.m, reversed, a, b));
            check(close(a, expected_a) && close(b, expected_b));

            Matrix infinite = native;
            infinite.m[10] = reversed ? 0.f : -1.f;
            infinite.m[11] = reversed ? .02f : -.02f;
            for (const auto& matrix : {infinite, transposed(infinite)}) {
                check(witness::hands::depth_projection(matrix.m, reversed, a, b));
                check(close(a + b / .02f, reversed ? 1.f : 0.f));
                check(close(a, reversed ? 0.f : 1.f));
            }
            // Very distant finite far planes are not rejected by an artificial range cap.
            const auto distant = projection(.1f, 10000000.f, reversed);
            check(witness::hands::depth_projection(distant.m, reversed, a, b));
            check(close(a + b / .1f, reversed ? 1.f : 0.f));

            rejected(projection(.0001f, 1000.f, reversed), reversed);
            rejected(projection(11.f, 1000.f, reversed), reversed);
            for (unsigned failure = 0; failure < 9; ++failure) {
                auto invalid = native;
                switch (failure) {
                case 0:
                    invalid.m[0] = std::numeric_limits<float>::quiet_NaN();
                    break;
                case 1:
                    invalid.m[11] = std::numeric_limits<float>::infinity();
                    break;
                case 2:
                    invalid.m[15] = .2f;
                    break; // View translation in homogeneous W.
                case 3:
                    invalid.m[8] = .03f;
                    break; // Depth depends on a sideways coordinate.
                case 4:
                    invalid.m[14] = -2.f;
                    break; // Unknown world scale.
                case 5:
                    invalid.m[11] = 0;
                    break; // No perspective depth range.
                case 6:
                    invalid.m[10] = -1.2f;
                    break;
                case 7:
                    invalid.m[11] = reversed ? 10.f : -10.f;
                    break;
                case 8:
                    invalid.m[14] = -.5f;
                    break;
                }
                rejected(invalid, reversed);
                rejected(transposed(invalid), reversed);
            }
            // A far endpoint behind the eye is not a usable finite or infinite frustum.
            auto behind = native;
            behind.m[10] = reversed ? -.01f : -.99f;
            rejected(behind, reversed);
        }
        rejected(Matrix{});
        Matrix orthographic{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, .001f, 0, 0, 0, 0, 1}};
        rejected(orthographic);
        std::cout << "Hand depth extraction: standard/reversed, transpose, rotated axes, infinite far, "
                     "meter scaling, and invalid projection checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
