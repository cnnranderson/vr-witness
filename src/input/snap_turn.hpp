#pragma once
#include "input/controller_input.hpp"

namespace witness::input {
// Step counts 1, 2 and 4 select 22.5, 45 and 90 degrees.
inline bool valid_snap_steps(unsigned steps) {
    return steps == 1 || steps == 2 || steps == 4;
}

inline float snap_radians(unsigned steps) {
    return .39269908169872414f * steps;
}

// Require neutral between turns and reset on stale input or context/device changes.
class SnapTurn {
    std::uint32_t device_{invalid_device};
    int axis_{-1};
    bool armed_{};
    std::uint64_t tick_{}, last_turn_{};

public:
    void reset() {
        device_ = invalid_device;
        axis_ = -1;
        armed_ = false;
        tick_ = 0;
        last_turn_ = 0;
    }

    // Return -1 (left), 0 (none) or +1 (right); now is a monotonic millisecond tick.
    int update(const Hand& hand, bool allowed, std::uint64_t now) {
        if (!allowed || !hand.valid || hand.device >= 16 || hand.stick < 0 || hand.stick >= 5 ||
            (hand.types[hand.stick] != 2 && !(hand.legacy_axis && hand.stick == 0)) ||
            !finite(hand.state.axes[hand.stick])) {
            reset();
            return 0;
        }
        if (device_ != hand.device || axis_ != hand.stick || (tick_ && (now < tick_ || now - tick_ > 100))) {
            reset();
            device_ = hand.device;
            axis_ = hand.stick;
        }
        tick_ = now;
        const auto stick = hand.state.axes[hand.stick];
        const float radius = std::hypot(stick.x, stick.y);
        if (radius <= .2f) {
            armed_ = true;
            return 0;
        }
        if (!armed_ || radius < .7f)
            return 0;
        // Vertical deflection also consumes the edge until the stick returns to neutral.
        armed_ = false;
        if (std::abs(stick.x) < .7f || std::abs(stick.x) <= std::abs(stick.y) ||
            (last_turn_ && now - last_turn_ < 250))
            return 0;
        last_turn_ = now;
        return stick.x > 0 ? 1 : -1;
    }
};

// Apply the same yaw delta to native and VR headings; outputs are valid only on success.
inline bool snap_headings(float yaw, float vr_yaw, int direction, unsigned steps, float& next_yaw,
                          float& next_vr_yaw) {
    if (!valid_snap_steps(steps) || (direction != 1 && direction != -1) || !std::isfinite(yaw) ||
        !std::isfinite(vr_yaw) || std::abs(yaw) > 10000.f || std::abs(vr_yaw) > 10000.f)
        return false;
    const float delta = -direction * snap_radians(steps);
    next_yaw = yaw + delta;
    next_vr_yaw = vr_yaw + delta;
    return std::isfinite(next_yaw) && std::isfinite(next_vr_yaw);
}
} // namespace witness::input
