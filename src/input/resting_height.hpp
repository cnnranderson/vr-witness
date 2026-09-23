#pragma once
#include <cmath>

namespace witness::input {
// The native OpenVR conversion preserves meters and changes the up axis to Z.
struct HeightFrame {
    void* player{};
    float tracked_height{}, standing_height{};
    bool native_vr{}, can_calibrate{};
    bool key_down{}, reset{};
};

class RestingHeight {
    bool armed_{}, calibrated_{};
    float height_{}, offset_{};

public:
    void disarm() { armed_ = false; }

    bool calibrated() const { return calibrated_; }

    float height() const { return height_; }

    float offset() const { return offset_; }

    // A fresh F7 press captures once; held keys and ineligible contexts never recalibrate.
    float update(const HeightFrame& frame) {
        if (!frame.native_vr || !frame.can_calibrate) {
            disarm();
            return offset_;
        }
        if (!frame.key_down) {
            armed_ = true;
            return offset_;
        }
        if (!armed_)
            return offset_;
        disarm();
        if (frame.reset) {
            calibrated_ = false;
            height_ = offset_ = 0;
        } else if (std::isfinite(frame.tracked_height) && std::isfinite(frame.standing_height) &&
                   frame.standing_height > 0 && frame.tracked_height >= 0 &&
                   frame.tracked_height <= frame.standing_height * 2) {
            height_ = frame.standing_height;
            offset_ = height_ - frame.tracked_height;
            calibrated_ = true;
        }
        return offset_;
    }
};
} // namespace witness::input
