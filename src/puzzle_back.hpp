#pragma once
#include "controller_input.hpp"
namespace witness::input {
// Emit one left-B edge after release; context/device changes require a fresh release.
class PuzzleBack {
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
        // Left-B-only capture: bit 1 on the current Knuckles legacy binding.
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
