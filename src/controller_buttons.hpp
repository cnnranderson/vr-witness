#pragma once
#include "controller_input.hpp"
namespace witness::input {
// Emit one B-button edge after release; context/device changes require a fresh release.
class BButton {
    std::uint32_t device_{invalid_device};
    bool armed_{};

public:
    void reset() {
        device_ = invalid_device;
        armed_ = false;
    }
    // Return true once per allowed press; invalid hands and disallowed contexts reset the latch.
    bool update(const Hand& hand, bool allowed) {
        if (!allowed || !hand.valid || hand.device >= 16) {
            reset();
            return false;
        }
        if (device_ != hand.device) {
            reset();
            device_ = hand.device;
        }
        // Both Knuckles B buttons were observed as bit 1 on the legacy binding.
        if (!(hand.state.pressed & (1ull << 1))) {
            armed_ = true;
            return false;
        }
        if (!armed_)
            return false;
        armed_ = false;
        return true;
    }
};
} // namespace witness::input
