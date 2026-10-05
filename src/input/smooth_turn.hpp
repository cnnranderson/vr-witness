#pragma once
#include "input/controller_input.hpp"

namespace witness::input {
inline bool valid_turn_speed(unsigned speed) {
    return speed >= 30 && speed <= 180;
}

// Integrate right-stick yaw in radians; neutral is required after invalid input or a timing gap.
class SmoothTurn {
    std::uint32_t device_{invalid_device};
    int axis_{-1};
    double tick_{};
    bool armed_{};

public:
    void reset() {
        device_ = invalid_device;
        axis_ = -1;
        tick_ = 0;
        armed_ = false;
    }

    // now is a high-resolution monotonic timestamp in milliseconds, including fractional milliseconds.
    float update(const Hand& hand, bool allowed, double now, unsigned degrees_per_second) {
        if (!allowed || !std::isfinite(now) || now <= 0 || !valid_turn_speed(degrees_per_second) ||
            !hand.valid || hand.device >= 16 || hand.stick < 0 || hand.stick >= 5 ||
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
        const auto elapsed = tick_ ? now - tick_ : 0;
        tick_ = now;
        const auto stick = hand.state.axes[hand.stick];
        if (std::hypot(stick.x, stick.y) <= .2f) {
            armed_ = true;
            return 0;
        }
        if (!armed_ || std::abs(stick.x) <= .2f)
            return 0;
        const float fraction = std::copysign(std::min(1.f, (std::abs(stick.x) - .2f) / .8f), stick.x);
        return -fraction * degrees_per_second * .017453292519943295f * static_cast<float>(elapsed / 1000.0);
    }
};
} // namespace witness::input
