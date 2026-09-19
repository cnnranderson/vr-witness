#pragma once
#include "controller_input.hpp"
namespace witness::input {
// Native D-pad events: up/down/left/right, verified against the supported game's menu handlers.
enum class MenuDirection : int { none = 0, up = 0x13f, down, left, right };

// One direction per pulse; rearm at neutral and repeat after 350 ms, then every 150 ms.
class MenuStick {
    std::uint32_t device_{invalid_device};
    bool armed_{};
    MenuDirection held_{MenuDirection::none};
    std::uint64_t next_{};

public:
    void reset() { *this = {}; }
    MenuDirection update(const Hand& hand, bool allowed, std::uint64_t now) {
        if (!allowed || !hand.valid || hand.device >= 16 || hand.stick < 0 || hand.stick >= 5 ||
            !finite(hand.state.axes[hand.stick])) {
            reset();
            return MenuDirection::none;
        }
        if (device_ != hand.device) {
            reset();
            device_ = hand.device;
        }
        const auto axis = hand.state.axes[hand.stick];
        const float x = std::abs(axis.x), y = std::abs(axis.y);
        if (std::max(x, y) < .3f) {
            armed_ = true;
            held_ = MenuDirection::none;
            next_ = 0;
            return MenuDirection::none;
        }
        if (!armed_ || std::max(x, y) < .6f)
            return MenuDirection::none;
        const auto direction = x > y ? (axis.x < 0 ? MenuDirection::left : MenuDirection::right)
                                     : (axis.y > 0 ? MenuDirection::up : MenuDirection::down);
        if (direction != held_) {
            held_ = direction;
            next_ = now + 350;
            return direction;
        }
        if (now < next_)
            return MenuDirection::none;
        next_ = now + 150;
        return direction;
    }
};
} // namespace witness::input
