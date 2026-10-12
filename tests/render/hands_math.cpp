#include "render/hands_math.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace witness::hands;
using witness::input::TrackedPose;

void check_impl(bool value, int line) {
    if (!value)
        throw std::runtime_error("Hand transform invariant failed at line " + std::to_string(line));
}

#define check(...) check_impl((__VA_ARGS__), __LINE__)

bool close(float a, float b, float tolerance = .0001f) {
    return std::abs(a - b) < tolerance;
}

bool close(const Mat4& a, const Mat4& b) {
    for (unsigned i = 0; i < a.size(); ++i)
        if (!close(a[i], b[i]))
            return false;
    return true;
}

std::array<float, 4> transform(const Mat4& matrix, std::array<float, 4> point) {
    std::array<float, 4> result{};
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned column = 0; column < 4; ++column)
            result[row] += matrix[row * 4 + column] * point[column];
    return result;
}

std::array<float, 3> project(const Mat4& matrix, float x, float y, float z) {
    const auto clip = transform(matrix, {x, y, z, 1});
    check(clip[3] > 0 && std::isfinite(clip[3]));
    return {clip[0] / clip[3], clip[1] / clip[3], clip[2] / clip[3]};
}

TrackedPose pose(const Mat4& matrix = identity()) {
    TrackedPose result{};
    result.valid = result.connected = true;
    result.tracking_result = 200;
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned column = 0; column < 4; ++column)
            result.matrix[row][column] = matrix[row * 4 + column];
    return result;
}

Mat4 translated(float x, float y, float z) {
    Mat4 result = identity();
    result[3] = x;
    result[7] = y;
    result[11] = z;
    return result;
}

int main() {
    try {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float infinity = std::numeric_limits<float>::infinity();
        const Mat4 projection = projection_raw(-1, 1, -1, 1);
        check(finite(projection));
        check(close(project(projection, 0, 0, -.02f)[2], 0));
        check(close(project(projection, 0, 0, -1000.f)[2], 1));
        check(close(project(projection, -1, 1, -1)[0], -1));
        check(close(project(projection, -1, 1, -1)[1], 1));
        check(close(project(projection, 1, -1, -1)[0], 1));
        check(close(project(projection, 1, -1, -1)[1], -1));

        // Raw OpenVR top is negative although the eye-space top edge has positive Y.
        const Mat4 asymmetric = projection_raw(-.8f, 1.2f, -.9f, 1.1f);
        for (float depth : {.02f, .2f, 3.f, 1000.f}) {
            const auto upper_left = project(asymmetric, -.8f * depth, .9f * depth, -depth);
            const auto lower_right = project(asymmetric, 1.2f * depth, -1.1f * depth, -depth);
            check(close(upper_left[0], -1) && close(upper_left[1], 1));
            check(close(lower_right[0], 1) && close(lower_right[1], -1));
        }
        const auto center = project(asymmetric, 0, 0, -1);
        check(close(center[0], -.2f) && close(center[1], .1f));
        check(projection_raw(1, -1, -1, 1) == Mat4{});
        check(projection_raw(-1, 1, 1, -1) == Mat4{});
        check(projection_raw(-1, 1, -1, 1, 0, 1000) == Mat4{});
        check(projection_raw(-1, 1, -1, 1, 2, 1) == Mat4{});
        check(projection_raw(nan, 1, -1, 1) == Mat4{});
        check(projection_raw(-1, infinity, -1, 1) == Mat4{});

        const TrackedPose head = pose(), hand = pose(translated(.2f, -.15f, -.5f));
        const TrackedPose left_eye = pose(translated(-.032f, 0, 0));
        const TrackedPose right_eye = pose(translated(.032f, 0, 0));
        const HandDraw left = make_draw(head, hand, left_eye.matrix, projection, identity());
        const HandDraw right = make_draw(head, hand, right_eye.matrix, projection, identity());
        check(left.valid && right.valid);
        const auto left_palm = project(left.clip_from_model, 0, 0, 0);
        const auto right_palm = project(right.clip_from_model, 0, 0, 0);
        check(close(left_palm[0], .464f) && close(right_palm[0], .336f));
        check(close(left_palm[0] - right_palm[0], .128f));
        check(close(left_palm[1], -.3f) && close(left_palm[1], right_palm[1]));
        check(close(left.normal_from_model, identity()));

        // Origin changes, player heading, and common translation cancel between HMD and hand.
        const Mat4 yaw{0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 0, 1};
        const Mat4 common = multiply(translated(16.4f, 2.f, -34.f), yaw);
        check(close(multiply(rigid_inverse(common), common), identity()));
        check(close(multiply(common, rigid_inverse(common)), identity()));
        const TrackedPose moved_head = pose(multiply(common, from_3x4(head.matrix)));
        const TrackedPose moved_hand = pose(multiply(common, from_3x4(hand.matrix)));
        const auto moved_left = make_draw(moved_head, moved_hand, left_eye.matrix, projection, identity());
        const auto moved_right = make_draw(moved_head, moved_hand, right_eye.matrix, projection, identity());
        check(moved_left.valid && moved_right.valid);
        check(close(moved_left.clip_from_model, left.clip_from_model));
        check(close(moved_right.clip_from_model, right.clip_from_model));
        check(close(moved_left.normal_from_model, left.normal_from_model));

        // Head rotation changes the view while a world-fixed hand retains its tracking position.
        const auto turned =
            make_draw(pose(yaw), pose(translated(-.5f, 0, 0)), head.matrix, projection, identity());
        check(turned.valid);
        const auto turned_palm = project(turned.clip_from_model, 0, 0, 0);
        check(close(turned_palm[0], 0) && close(turned_palm[1], 0));

        for (float distance : {.001f, .02f, .5f, 1000.f, 2000.f}) {
            const auto draw =
                make_draw(head, pose(translated(0, 0, -distance)), head.matrix, projection, identity());
            check(draw.valid && finite(draw.clip_from_model) && finite(draw.normal_from_model));
            const auto palm = project(draw.clip_from_model, 0, 0, 0);
            check(std::isfinite(palm[2])); // GPU clipping, not division here, handles near/far rejection.
        }

        // Per-hand art alignment remains configurable and never moves the palm origin itself.
        const auto left_finger = transform(alignment(true), {0, 1, 0, 0});
        const auto right_finger = transform(alignment(false), {0, 1, 0, 0});
        check(left_finger[0] < 0 && right_finger[0] > 0);
        check(close(left_finger[0], -right_finger[0]));
        check(left_finger[2] < -.98f && close(left_finger[2], right_finger[2]));
        const auto aligned = make_draw(head, hand, left_eye.matrix, projection, alignment(true));
        check(aligned.valid);
        const auto aligned_palm = project(aligned.clip_from_model, 0, 0, 0);
        check(close(aligned_palm[0], left_palm[0]) && close(aligned_palm[1], left_palm[1]));
        const auto offset = make_draw(head, hand, left_eye.matrix, projection, translated(.1f, 0, 0));
        check(offset.valid && close(project(offset.clip_from_model, 0, 0, 0)[0], .664f));
        check(close(offset.normal_from_model, left.normal_from_model));
        Mat4 scale = identity();
        scale[0] = 2;
        scale[5] = 3;
        scale[10] = 4;
        const auto scaled = make_draw(head, hand, left_eye.matrix, projection, multiply(yaw, scale));
        check(scaled.valid);
        const auto normal = transform(scaled.normal_from_model, {1, 1, 0, 0});
        const auto tangent = transform(multiply(yaw, scale), {1, -1, 0, 0});
        check(close(normal[0] * tangent[0] + normal[1] * tangent[1] + normal[2] * tangent[2], 0));
        check(scaled.normal_from_model[3] == 0 && scaled.normal_from_model[7] == 0 &&
              scaled.normal_from_model[11] == 0);

        // Semantic landmarks from the supplied mirrored rig, not raw-controller axes.
        // With a palm_ext frame facing forward, a handshake has thumbs up and palms inward.
        for (bool is_left : {false, true}) {
            const float thumb_side = is_left ? -1.f : 1.f;
            const auto fingers = transform(palm_basis(is_left), {0, 1, 0, 0});
            const auto thumb = transform(palm_basis(is_left), {thumb_side, 0, 0, 0});
            const auto palm_normal = transform(palm_basis(is_left), {0, 0, 1, 0});
            check(close(fingers[0], 0) && close(fingers[1], 0) && close(fingers[2], -1));
            check(close(thumb[0], 0) && close(thumb[1], 1) && close(thumb[2], 0));
            check(close(palm_normal[0], is_left ? 1.f : -1.f) && close(palm_normal[1], 0) &&
                  close(palm_normal[2], 0));
            check(witness::input::valid_pose(pose(palm_basis(is_left)))); // No handedness reflection.
            const auto fitted_fingers = transform(palm_alignment(is_left), {0, 1, 0, 0});
            const auto fitted_thumb = transform(palm_alignment(is_left), {thumb_side, 0, 0, 0});
            const auto fitted_palm = transform(palm_alignment(is_left), {0, 0, 1, 0});
            check(close(fitted_fingers[0], 0) && fitted_fingers[2] < -.98f);
            check(close(fitted_thumb[0], 0) && fitted_thumb[1] > .98f);
            check(fitted_palm == palm_normal); // The source fit never rolls the palm inside out.

            Mat4 device_from_glove{};
            check(component_alignment(head.matrix, is_left, device_from_glove));
            check(close(device_from_glove, palm_alignment(is_left)));
            // Component offsets are supplied by the runtime and are not negated or inverted.
            const auto component = pose(multiply(translated(.03f, -.02f, .07f), yaw));
            check(component_alignment(component.matrix, is_left, device_from_glove));
            const auto palm_origin = transform(device_from_glove, {0, 0, 0, 1});
            check(close(palm_origin[0], .03f) && close(palm_origin[1], -.02f) && close(palm_origin[2], .07f));
            const auto device_fingers = transform(device_from_glove, {0, 1, 0, 0});
            check(device_fingers[0] < -.98f && close(device_fingers[2], 0));
            const auto device_palm = transform(device_from_glove, {0, 0, 1, 0});
            check(close(device_palm[0], 0) && close(device_palm[1], 0) &&
                  close(device_palm[2], is_left ? -1.f : 1.f));
            const auto palm_draw = make_draw(head, hand, left_eye.matrix, projection, device_from_glove);
            check(palm_draw.valid);
            const auto projected_palm = project(palm_draw.clip_from_model, 0, 0, 0);
            check(close(projected_palm[0], (.2f + .03f + .032f) / (.5f - .07f)));
            check(close(projected_palm[1], (-.15f - .02f) / (.5f - .07f)));

            for (unsigned failure = 0; failure < 5; ++failure) {
                auto invalid_component = head;
                switch (failure) {
                case 0:
                    invalid_component.matrix[0][0] = -1;
                    break;
                case 1:
                    invalid_component.matrix[1][1] = 2;
                    break;
                case 2:
                    invalid_component.matrix[0][1] = .2f;
                    break;
                case 3:
                    invalid_component.matrix[2][3] = nan;
                    break;
                case 4:
                    invalid_component.matrix[0][3] = infinity;
                    break;
                }
                check(!component_alignment(invalid_component.matrix, is_left, device_from_glove));
                check(device_from_glove == Mat4{}); // Never reuse a prior controller's alignment.
            }
        }

        // Losing either device, non-rigid tracking data, or malformed eye/projection data hides the hand.
        for (unsigned failure = 0; failure < 8; ++failure) {
            auto invalid = hand;
            switch (failure) {
            case 0:
                invalid.valid = false;
                break;
            case 1:
                invalid.connected = false;
                break;
            case 2:
                invalid.tracking_result = 201;
                break;
            case 3:
                invalid.matrix[0][0] = 2;
                break;
            case 4:
                invalid.matrix[0][1] = .2f;
                break;
            case 5:
                invalid.matrix[0][0] = -1;
                break;
            case 6:
                invalid.matrix[0][3] = nan;
                break;
            case 7:
                invalid.matrix[2][3] = infinity;
                break;
            }
            check(!make_draw(head, invalid, left_eye.matrix, projection, identity()).valid);
            check(!make_draw(invalid, hand, left_eye.matrix, projection, identity()).valid);
        }
        auto invalid_eye = left_eye;
        invalid_eye.matrix[1][1] = 0;
        check(!make_draw(head, hand, invalid_eye.matrix, projection, identity()).valid);
        auto invalid_projection = projection;
        invalid_projection[6] = nan;
        check(!make_draw(head, hand, left_eye.matrix, invalid_projection, identity()).valid);
        check(!make_draw(head, hand, left_eye.matrix, Mat4{}, identity()).valid);
        Mat4 invalid_alignment = identity();
        invalid_alignment[0] = 0;
        check(!make_draw(head, hand, left_eye.matrix, projection, invalid_alignment).valid);
        invalid_alignment = identity();
        invalid_alignment[14] = .1f;
        check(!make_draw(head, hand, left_eye.matrix, projection, invalid_alignment).valid);
        invalid_alignment = identity();
        invalid_alignment[3] = infinity;
        const auto rejected = make_draw(head, hand, left_eye.matrix, projection, invalid_alignment);
        check(!rejected.valid && rejected.clip_from_model == Mat4{} && rejected.normal_from_model == Mat4{});

        std::cout << "Hand transform checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
