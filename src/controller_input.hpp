#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace witness::input {
// Windows x64 OpenVR v0.9.19 IVRSystem_012 ABI; slots include every virtual method.
// Source: https://github.com/ValveSoftware/openvr/blob/v0.9.19/headers/openvr.h
struct Axis { float x{}, y{}; };
struct ControllerState {
    std::uint32_t packet{};
    std::uint64_t pressed{}, touched{};
    Axis axes[5]{};
};
static_assert(sizeof(ControllerState) == 64 && offsetof(ControllerState, axes) == 24);
inline constexpr unsigned role_slot = 17, connected_slot = 20, property_slot = 23,
    state_slot = 32, focus_slot = 39;
inline constexpr std::uint32_t invalid_device = 0xffffffff;
// Role-selected state; stick == -1 means no supported axis was found.
struct Hand {
    std::uint32_t device{invalid_device};
    bool valid{};
    int types[5]{};
    int stick{-1};
    bool legacy_axis{};
    ControllerState state{};
};
struct Vec3 { float x{}, y{}, z{}; };
// Native mode: 0 = puzzle idle, 1 = drawing, 2 = walking.
struct Context {
    int mode{};
    bool focused{}, tracking{}, suppressed{};
    float fade{};
    Vec3 forward{}, left{};
};
inline bool finite(Axis a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::abs(a.x) <= 1.01f && std::abs(a.y) <= 1.01f; }
inline bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
inline float length(Vec3 v) { return std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z); }
// Rescale a radial 0.2 deadzone to unit range; invalid axes produce zero movement.
inline Axis deadzone(Axis a) {
    if (!finite(a)) return {};
    const float n = std::hypot(a.x, a.y);
    if (n <= .2f) return {};
    const float scale = std::min(1.f, (n - .2f) / .8f) / n;
    return {a.x * scale, a.y * scale};
}
// Select one joystick, or the observed compatibility axis when explicitly enabled.
inline void select_axis(Hand& hand, bool legacy) {
    hand.stick=-1; hand.legacy_axis=false;
    int sticks=0;
    for (int i=0;i<5;++i) if(hand.types[i]==2) {hand.stick=i;++sticks;}
    if(sticks!=1)hand.stick=-1;
    // Explicit opt-in for the exact compatibility layout observed in this game.
    if(sticks==0 && legacy && hand.types[0]==1 && hand.types[1]==3 && hand.types[2]==3 && hand.types[3]==-1 && hand.types[4]==-1) {
        hand.stick=0; hand.legacy_axis=true;
    }
}
inline bool recenter_pressed(const Hand& hand) {
    // Right A is bit 2 on the observed legacy layout and OpenVR A (bit 7) otherwise.
    const auto mask=1ull<<(hand.legacy_axis ? 2 : 7);
    return hand.valid && (hand.state.pressed & mask)!=0;
}
inline bool walking(const Context& c) {
    return c.mode == 2 && c.focused && c.tracking && !c.suppressed && c.fade == 0.f;
}
// Movement stays disarmed until neutral after a device, axis or context change.
class Movement {
    std::uint32_t device_{invalid_device};
    int axis_{-1};
    bool armed_{};
public:
    void reset() { device_ = invalid_device; axis_ = -1; armed_ = false; }
    Axis update(const Hand& hand, bool allowed) {
        if (!allowed || !hand.valid || hand.stick < 0 || hand.stick >= 5 || (hand.types[hand.stick] != 2 && !(hand.legacy_axis && hand.stick==0 && hand.types[0]==1)) || !finite(hand.state.axes[hand.stick])) {
            reset(); return {};
        }
        if (device_ != hand.device || axis_ != hand.stick) {
            device_ = hand.device; axis_ = hand.stick; armed_ = false;
        }
        const auto a = hand.state.axes[hand.stick];
        if (!armed_) { armed_ = std::hypot(a.x, a.y) <= .15f; return {}; }
        return deadzone(a);
    }
};
// Add camera-relative stick motion without reducing the native movement magnitude cap.
inline bool blend(Vec3& native, Axis stick, const Context& c) {
    if (!finite(native) || !finite(c.forward) || !finite(c.left) || !finite(stick)) return false;
    const float f = length(c.forward), l = length(c.left);
    if (f < .9f || f > 1.1f || l < .9f || l > 1.1f) return false;
    // Native +strafe is the A/left direction in this verified build.
    Vec3 add{c.forward.x*stick.y-c.left.x*stick.x,
             c.forward.y*stick.y-c.left.y*stick.x,
             c.forward.z*stick.y-c.left.z*stick.x};
    if (length(add) < .0001f) return false;
    Vec3 result{native.x+add.x, native.y+add.y, native.z+add.z};
    const float n = length(result), cap = std::max(1.f, length(native));
    if (!std::isfinite(n) || !std::isfinite(cap)) return false;
    if (n > cap) { result.x *= cap/n; result.y *= cap/n; result.z *= cap/n; }
    native = result;
    return true;
}
} // namespace witness::input
