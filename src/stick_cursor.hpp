#pragma once
#include "controller_input.hpp"

namespace witness::input {
// Manual cursor ownership lasts until recenter, context loss or device change.
class StickCursor {
    std::uint32_t device_{invalid_device};
    int axis_{-1};
    bool armed_{}, manual_{};
    std::uint64_t tick_{};

public:
    void reset() {
        device_ = invalid_device;
        axis_ = -1;
        armed_ = false;
        manual_ = false;
        tick_ = 0;
    }
    // Return manual ownership; delta uses view units, speed uses view widths/s, and now uses ms.
    bool update(const Hand& hand, bool allowed, bool recentered, std::uint64_t now, float width, float speed,
                Axis& delta) {
        delta = {};
        if (!allowed || !hand.valid || hand.device >= 16 || hand.stick < 0 || hand.stick >= 5 ||
            (hand.types[hand.stick] != 2 && !(hand.legacy_axis && hand.stick == 0 && hand.types[0] == 1)) ||
            !finite(hand.state.axes[hand.stick]) || !std::isfinite(width) || width <= 0 || width > 4 ||
            !std::isfinite(speed) || speed < .1f || speed > 3.f) {
            reset();
            return false;
        }
        if (recentered || device_ != hand.device || axis_ != hand.stick ||
            (tick_ && (now < tick_ || now - tick_ > 100))) {
            reset();
            device_ = hand.device;
            axis_ = hand.stick;
        }
        const float dt = tick_ ? static_cast<float>(now - tick_) / 1000.f : 0.f;
        tick_ = now;
        const auto value = hand.state.axes[hand.stick];
        if (!armed_) {
            armed_ = std::hypot(value.x, value.y) <= .15f;
            return false;
        }
        const auto stick = deadzone(value);
        if (stick.x || stick.y)
            manual_ = true;
        if (manual_)
            delta = {stick.x * speed * width * dt, stick.y * speed * width * dt};
        return manual_;
    }
};
} // namespace witness::input
