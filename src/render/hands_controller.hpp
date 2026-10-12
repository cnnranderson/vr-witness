#pragma once
#include "render/hands_math.hpp"

namespace witness::hands {
// OpenVR v0.9.19 Windows x64 ABI, IVRSystem_012 and IVRRenderModels_005.
struct Matrix34 {
    float m[3][4]{};
};

struct ComponentState {
    Matrix34 render_model{}, local{};
    std::uint32_t properties{};
};

struct ControllerMode {
    bool scroll_wheel_visible{};
};

static_assert(sizeof(Matrix34) == 48);
static_assert(sizeof(ComponentState) == 100 && offsetof(ComponentState, local) == 48 &&
              offsetof(ComponentState, properties) == 96);
static_assert(sizeof(ControllerMode) == 1);
static_assert(sizeof(input::ControllerState) == 64);

using StringProperty = std::uint32_t (*)(void*, std::uint32_t, int, char*, std::uint32_t, int*);
using ControllerStateFn = bool (*)(void*, std::uint32_t, input::ControllerState*);
using ComponentStateFn = bool (*)(void*, const char*, const char*, const input::ControllerState*,
                                  const ControllerMode*, ComponentState*);

struct ControllerHand {
    bool valid{};
    Mat4 device_from_glove{};
    bool grip_pressed{};
};

// The caller resolves roles, verifies connection/current poses, and validates the
// runtime methods before passing them here. These functions borrow the game's VR
// session; no initialization, shutdown, event consumption, or input writes occur.
// Legacy grip bit 2 follows the active SteamVR binding. Default Index bindings feed
// it from both a firm physical squeeze and A, so it is not an isolated squeeze axis.
inline ControllerHand sample_hand(void* system, void* models, std::uint32_t device, bool is_left,
                                  StringProperty string_property, ControllerStateFn controller_state,
                                  ComponentStateFn component_state) {
    if (!system || !models || !string_property || !controller_state || !component_state || device == 0 ||
        device >= 16)
        return {};
    constexpr int render_model_property = 1003;
    std::array<char, 256> model{};
    int property_error = -1;
    const auto length = string_property(system, device, render_model_property, model.data(),
                                        static_cast<std::uint32_t>(model.size()), &property_error);
    // The historical API returns byte length including the terminator. Never pass
    // a partial/unterminated model name into the runtime component lookup.
    if (property_error != 0 || length < 2 || length > model.size() ||
        std::find(model.begin(), model.begin() + length, '\0') != model.begin() + length - 1)
        return {};
    input::ControllerState state{};
    if (!controller_state(system, device, &state))
        return {};
    const ControllerMode mode{};
    ComponentState component{};
    if (!component_state(models, model.data(), "openxr_handmodel", &state, &mode, &component))
        return {};
    ControllerHand result{};
    if (!component_alignment(component.local.m, is_left, result.device_from_glove))
        return {};
    result.grip_pressed = (state.pressed & (1ull << 2)) != 0;
    result.valid = true;
    return result;
}
} // namespace witness::hands
