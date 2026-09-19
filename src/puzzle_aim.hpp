#pragma once
#include "controller_input.hpp"

namespace witness::input {
// Windows x64 OpenVR v0.9.19 TrackedDevicePose_t, IVRCompositor_013 slot 3.
struct TrackedPose {
    float matrix[3][4]{};
    Vec3 velocity{}, angular_velocity{};
    int tracking_result{};
    bool valid{}, connected{};
};
static_assert(sizeof(TrackedPose) == 80 && offsetof(TrackedPose, valid) == 76);
// Native cursor coordinates/bounds and the eye-origin projection plane for one frame.
struct AimFrame {
    Axis cursor{}, bounds{};
    float scale{};
    Vec3 origin{}, plane{}, u{}, v{};
    bool valid{};
};
inline Vec3 add(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vec3 subtract(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 multiply(Vec3 a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}
inline float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline bool valid_pose(const TrackedPose& p) {
    if (!p.valid || !p.connected || p.tracking_result != 200)
        return false;
    for (const auto& row : p.matrix)
        for (float f : row)
            if (!std::isfinite(f))
                return false;
    Vec3 cols[3];
    for (int i = 0; i < 3; ++i)
        cols[i] = {p.matrix[0][i], p.matrix[1][i], p.matrix[2][i]};
    for (auto c : cols)
        if (std::abs(dot(c, c) - 1.f) > .03f)
            return false;
    return std::abs(dot(cols[0], cols[1])) < .02f && std::abs(dot(cols[0], cols[2])) < .02f &&
           std::abs(dot(cols[1], cols[2])) < .02f && dot(cross(cols[0], cols[1]), cols[2]) > .97f;
}
inline bool puzzle(const Context& c) {
    return (c.mode == 0 || c.mode == 1) && c.focused && c.tracking && !c.suppressed && c.fade == 0.f;
}
struct AimSettings {
    float speed{.6f}; // Cursor view widths per second.
    std::uint32_t smoothing_ms{80};
};
inline bool valid_settings(const AimSettings& s) {
    return std::isfinite(s.speed) && s.speed >= .1f && s.speed <= 3.f && s.smoothing_ms <= 250;
}
inline Vec3 pose_forward(const TrackedPose& pose) {
    return {-pose.matrix[0][2], -pose.matrix[1][2], -pose.matrix[2][2]};
}
// Store the rotation aligning hand aim with head direction when A is pressed.
class AimCalibration {
    Vec3 rotation_{};
    float w_{1.f};
    std::uint32_t device_{invalid_device};
    bool button_armed_{};

public:
    // Require another button release without discarding the current rotation.
    void disarm() { button_armed_ = false; }
    void reset() {
        rotation_ = {};
        w_ = 1.f;
        device_ = invalid_device;
        button_armed_ = false;
    }
    // Return true only when a fresh, non-drawing button press changes calibration.
    bool update(std::uint32_t device, bool allowed, bool button, bool drawing, const TrackedPose& head,
                const TrackedPose& hand) {
        if (device != device_) {
            reset();
            device_ = device;
        }
        if (!allowed || device >= 16 || !valid_pose(head) || !valid_pose(hand)) {
            button_armed_ = false;
            return false;
        }
        if (!button) {
            button_armed_ = true;
            return false;
        }
        if (!button_armed_)
            return false;
        button_armed_ = false;
        if (drawing)
            return false;
        const auto from = pose_forward(hand), to = pose_forward(head);
        const auto xyz = cross(from, to);
        const float w = 1.f + dot(from, to), norm = std::sqrt(dot(xyz, xyz) + w * w);
        if (w < .01f || !std::isfinite(norm) || norm < .01f)
            return false;
        rotation_ = multiply(xyz, 1.f / norm);
        w_ = w / norm;
        return true;
    }
    Vec3 direction(const TrackedPose& hand) const {
        const auto forward = pose_forward(hand), t = multiply(cross(rotation_, forward), 2.f);
        return add(add(forward, multiply(t, w_)), cross(rotation_, t));
    }
};
// Project calibrated hand direction from the eye origin; target is usable only on success.
inline bool aim_target(const TrackedPose& head, const TrackedPose& hand, const AimFrame& f, Axis& target,
                       const AimCalibration& calibration = AimCalibration{}) {
    if (!valid_pose(head) || !valid_pose(hand) || !f.valid || !finite(f.origin) || !finite(f.plane) ||
        !finite(f.u) || !finite(f.v) || !std::isfinite(f.scale) || f.scale < .001f || f.scale > 10.f ||
        !std::isfinite(f.bounds.x) || !std::isfinite(f.bounds.y) || f.bounds.x <= 0 || f.bounds.y <= 0 ||
        f.bounds.x > 4 || f.bounds.y > 4)
        return false;
    if (std::abs(dot(f.u, f.u) - 1.f) > .03f || std::abs(dot(f.v, f.v) - 1.f) > .03f ||
        std::abs(dot(f.u, f.v)) > .02f)
        return false;
    const Vec3 hand_forward = calibration.direction(hand);
    Vec3 relative;
    relative.x = dot({head.matrix[0][0], head.matrix[1][0], head.matrix[2][0]}, hand_forward);
    relative.y = dot({head.matrix[0][1], head.matrix[1][1], head.matrix[2][1]}, hand_forward);
    relative.z = dot({head.matrix[0][2], head.matrix[1][2], head.matrix[2][2]}, hand_forward);
    const Vec3 forward = cross(f.v, f.u);
    const Vec3 direction =
        add(add(multiply(f.u, relative.x), multiply(f.v, relative.y)), multiply(forward, -relative.z));
    const float denominator = dot(direction, forward);
    if (denominator < .15f)
        return false;
    const float distance = dot(subtract(f.plane, f.origin), forward) / denominator;
    if (!std::isfinite(distance) || distance <= 0)
        return false;
    const Vec3 offset = subtract(add(f.origin, multiply(direction, distance)), f.plane);
    target = {dot(offset, f.u) / (2 * f.scale), dot(offset, f.v) / (2 * f.scale)};
    return std::isfinite(target.x) && std::isfinite(target.y) && target.x >= 0 && target.y >= 0 &&
           target.x <= f.bounds.x && target.y <= f.bounds.y;
}
// Smooth angular cursor motion after trigger release, with a view-width-based speed limit.
class Pointing {
    std::uint32_t device_{invalid_device};
    bool armed_{};
    std::uint64_t tick_{};

public:
    void reset() {
        device_ = invalid_device;
        armed_ = false;
        tick_ = 0;
    }
    // Return whether delta should replace native input; tick is monotonic milliseconds.
    bool update(std::uint32_t device, bool allowed, bool trigger, const Axis& target, const Axis& current,
                Axis& delta, std::uint64_t tick, float view_width, const AimSettings& settings) {
        if (!allowed || device >= 16 || !valid_settings(settings) || !std::isfinite(view_width) ||
            view_width <= 0 || view_width > 4 || !std::isfinite(current.x) || !std::isfinite(current.y) ||
            !std::isfinite(target.x) || !std::isfinite(target.y)) {
            reset();
            return false;
        }
        if (device_ != device) {
            reset();
            device_ = device;
        }
        // No cursor jump on enabling/reacquiring while the draw trigger is held.
        if (!armed_) {
            armed_ = !trigger;
            tick_ = tick;
            return false;
        }
        if (tick <= tick_) {
            delta = {};
            return true;
        }
        const float dt = static_cast<float>(tick - tick_) / 1000.f;
        tick_ = tick;
        if (dt > .1f) {
            reset();
            return false;
        }
        delta = {target.x - current.x, target.y - current.y};
        const float n = std::hypot(delta.x, delta.y);
        if (!std::isfinite(n)) {
            reset();
            return false;
        }
        if (n < .0004f)
            delta = {};
        else {
            const float alpha =
                settings.smoothing_ms ? -std::expm1(-dt * 1000.f / settings.smoothing_ms) : 1.f;
            const float scale = std::min(alpha, settings.speed * view_width * dt / n);
            delta.x *= scale;
            delta.y *= scale;
        }
        return true;
    }
};
} // namespace witness::input
