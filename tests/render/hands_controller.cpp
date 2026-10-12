#include "render/hands_controller.hpp"
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace witness::hands;

namespace {
void check_impl(bool value, int line) {
    if (!value)
        throw std::runtime_error("Hand controller invariant failed at line " + std::to_string(line));
}

#define check(...) check_impl((__VA_ARGS__), __LINE__)

struct Fixture {
    std::string name{"fixture_right_controller"};
    int property_error{};
    unsigned length_override{std::numeric_limits<unsigned>::max()};
    bool missing_terminator{}, state_ok{true}, component_ok{true}, args_ok{true};
    std::uint32_t expected_device{5};
    unsigned property_calls{}, state_calls{}, component_calls{};
    witness::input::ControllerState controller{};
    ComponentState component{};
};

Fixture fixture;
int system_object{}, models_object{};

std::uint32_t fake_property(void* system, std::uint32_t device, int property, char* output,
                            std::uint32_t capacity, int* error) {
    ++fixture.property_calls;
    fixture.args_ok &= system == &system_object && device == fixture.expected_device && property == 1003 &&
                       capacity == 256 && error != nullptr;
    check(capacity == 256 && output && error);
    const auto count = std::min<std::size_t>(fixture.name.size(), capacity - 1);
    std::memcpy(output, fixture.name.data(), count);
    output[count] = '\0';
    if (fixture.missing_terminator)
        std::fill(output, output + capacity, 'x');
    *error = fixture.property_error;
    return fixture.length_override != std::numeric_limits<unsigned>::max()
               ? fixture.length_override
               : static_cast<std::uint32_t>(fixture.name.size() + 1);
}

bool fake_controller(void* system, std::uint32_t device, witness::input::ControllerState* output) {
    ++fixture.state_calls;
    fixture.args_ok &= system == &system_object && device == fixture.expected_device;
    check(output);
    *output = fixture.controller;
    return fixture.state_ok;
}

bool fake_component(void* models, const char* name, const char* component,
                    const witness::input::ControllerState* state, const ControllerMode* mode,
                    ComponentState* output) {
    ++fixture.component_calls;
    fixture.args_ok &=
        models == &models_object && name == fixture.name && std::strcmp(component, "openxr_handmodel") == 0;
    check(state && mode && output);
    fixture.args_ok &= !mode->scroll_wheel_visible && state->packet == fixture.controller.packet &&
                       state->pressed == fixture.controller.pressed &&
                       state->touched == fixture.controller.touched &&
                       state->axes[2].x == fixture.controller.axes[2].x;
    *output = fixture.component;
    return fixture.component_ok;
}

ControllerHand sample(bool is_left = false) {
    return sample_hand(&system_object, &models_object, fixture.expected_device, is_left, fake_property,
                       fake_controller, fake_component);
}

void reset() {
    fixture = {};
    fixture.controller.packet = 77;
    fixture.controller.axes[2].x = .7f;
    for (unsigned axis = 0; axis < 3; ++axis)
        fixture.component.local.m[axis][axis] = 1;
    // The render-model transform is intentionally unrelated to local attachment.
    fixture.component.render_model.m[0][0] = std::numeric_limits<float>::quiet_NaN();
    fixture.component.local.m[0][3] = .03f;
    fixture.component.local.m[1][3] = -.02f;
    fixture.component.local.m[2][3] = .07f;
}

void invalid(const ControllerHand& result) {
    check(!result.valid && !result.grip_pressed && result.device_from_glove == Mat4{});
}
} // namespace

int main() {
    try {
        reset();
        auto right = sample();
        check(right.valid && !right.grip_pressed && fixture.args_ok);
        check(fixture.property_calls == 1 && fixture.state_calls == 1 && fixture.component_calls == 1);
        check(right.device_from_glove[3] == .03f && right.device_from_glove[7] == -.02f &&
              right.device_from_glove[11] == .07f);
        check(right.device_from_glove[2] == -1); // Right palm faces inward in palm_ext.
        fixture.expected_device = 12;
        fixture.name = "fixture_left_controller";
        fixture.controller.pressed = 1ull << 2;
        const auto left = sample(true);
        check(left.valid && left.grip_pressed && fixture.args_ok);
        check(left.device_from_glove[2] == 1); // Mirrored role selects opposite palm normal.
        fixture.controller.pressed = (1ull << 1) | (1ull << 33);
        fixture.controller.touched = 1ull << 2;
        check(sample(true).valid && !sample(true).grip_pressed); // Touch, B, and trigger do not close.
        fixture.controller.pressed |= 1ull << 2;
        check(sample(true).grip_pressed);
        fixture.controller.pressed = 0;
        check(!sample(true).grip_pressed); // Button release opens without a retained latch.

        // Runtime state failure represents an unavailable/disconnected controller.
        reset();
        fixture.state_ok = false;
        fixture.controller.pressed = 1ull << 2;
        invalid(sample());
        check(fixture.property_calls == 1 && fixture.state_calls == 1 && fixture.component_calls == 0);
        reset();
        fixture.component_ok = false;
        invalid(sample());
        check(fixture.component_calls == 1); // Missing palm component has no guessed-axis fallback.

        for (unsigned failure = 0; failure < 7; ++failure) {
            reset();
            switch (failure) {
            case 0:
                fixture.property_error = 4;
                break;
            case 1:
                fixture.name = "";
                break;
            case 2:
                fixture.length_override = 257;
                break;
            case 3:
                fixture.missing_terminator = true;
                break;
            case 4:
                fixture.name = std::string("ab\0cd", 5);
                break;
            case 5:
                fixture.length_override = 2;
                break;
            case 6:
                fixture.length_override = 0;
                break;
            }
            invalid(sample());
            check(fixture.state_calls == 0 && fixture.component_calls == 0);
        }
        reset();
        fixture.name.assign(255, 'x');
        check(sample().valid && fixture.args_ok); // Maximum bounded name includes final NUL.

        for (unsigned failure = 0; failure < 5; ++failure) {
            reset();
            switch (failure) {
            case 0:
                fixture.component.local.m[0][0] = -1;
                break;
            case 1:
                fixture.component.local.m[2][2] = 2;
                break;
            case 2:
                fixture.component.local.m[0][1] = .1f;
                break;
            case 3:
                fixture.component.local.m[0][3] = std::numeric_limits<float>::infinity();
                break;
            case 4:
                fixture.component.local.m[1][1] = std::numeric_limits<float>::quiet_NaN();
                break;
            }
            invalid(sample());
            check(fixture.component_calls == 1);
        }
        reset();
        for (std::uint32_t device : {0u, 16u, witness::input::invalid_device})
            invalid(sample_hand(&system_object, &models_object, device, false, fake_property, fake_controller,
                                fake_component));
        invalid(
            sample_hand(nullptr, &models_object, 5, false, fake_property, fake_controller, fake_component));
        invalid(
            sample_hand(&system_object, nullptr, 5, false, fake_property, fake_controller, fake_component));
        invalid(
            sample_hand(&system_object, &models_object, 5, false, nullptr, fake_controller, fake_component));
        invalid(
            sample_hand(&system_object, &models_object, 5, false, fake_property, nullptr, fake_component));
        invalid(
            sample_hand(&system_object, &models_object, 5, false, fake_property, fake_controller, nullptr));
        check(fixture.property_calls == 0 && fixture.state_calls == 0 && fixture.component_calls == 0);
        std::cout << "Hand controller ABI, palm lookup, validation, roles, and legacy grip checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
