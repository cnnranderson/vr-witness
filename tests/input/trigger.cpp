#include "targets.hpp"
#include "common/protocol.hpp"
#include "common/win_util.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace witness;
using namespace witness::input;

int check_trigger_hook() {
    const auto root = std::filesystem::path(module_path()).parent_path();
    using Control = DWORD(WINAPI*)(InputSessionRequest*);
    const auto library = LoadLibraryW((root / L"witness-input-probe.dll").c_str());
    const auto control = library ? reinterpret_cast<Control>(reinterpret_cast<void*>(
                                       GetProcAddress(library, "WitnessInputSessionControl")))
                                 : nullptr;
    if (!control)
        return 1;
    const auto command = [&](SessionCommand action) {
        InputSessionRequest request{};
        request.size = sizeof(request);
        request.version = kInputProtocolVersion;
        request.command = action;
        request.pointing = 1;
        request.legacy_axis0 = 1;
        request.aim_speed_percent = 60;
        request.aim_smoothing_ms = 80;
        if (control(&request))
            throw std::runtime_error("Trigger fixture session command failed");
    };
    const auto poll = [&](bool expected, const char* label) {
        TestInputPoll();
        if (fixture_trigger_pressed != expected)
            throw std::runtime_error(label);
    };
    const auto release = [&] {
        fixture_right_trigger = false;
        poll(false, "Right trigger release was lost");
    };
    const auto press = [&] {
        fixture_right_trigger = true;
        poll(true, "Right trigger press was lost");
    };
    try {
        fixture_trigger_scenario = true;
        fixture_swap_roles = true; // The native game picks the left hand first.
        fixture_right_trigger = true;
        command(SessionCommand::start);
        poll(false, "Held trigger activated on attachment");
        release();
        fixture_left_trigger = true;
        poll(false, "Left trigger activated puzzle input");
        const auto before = fixture_trigger_presses;
        press();
        fixture_trigger_context.mode = 0;
        poll(true, "Trigger hold was lost on puzzle entry");
        fixture_trigger_context.mode = 1;
        poll(true, "Trigger hold was lost while drawing");
        if (fixture_trigger_presses != before + 1)
            throw std::runtime_error("Held trigger repeated its press edge");
        release();
        fixture_trigger_context.mode = 2;
        press();
        fixture_trigger_context.focused = false;
        poll(false, "Trigger leaked without game focus");
        fixture_trigger_context.focused = true;
        poll(false, "Held trigger rearmed on focus return");
        release();
        press();
        fixture_captured = true;
        poll(false, "Trigger leaked while SteamVR captured input");
        fixture_captured = false;
        poll(false, "Held trigger rearmed after runtime capture");
        release();
        press();
        fixture_controller_state_valid = false;
        poll(false, "Failed controller read retained trigger");
        fixture_controller_state_valid = true;
        poll(false, "Held trigger rearmed after failed controller read");
        release();
        press();
        fixture_controllers_connected = false;
        poll(false, "Disconnected controller retained trigger");
        fixture_controllers_connected = true;
        poll(false, "Held trigger rearmed after reconnection");
        release();
        press();
        fixture_trigger_context.tracking = false;
        poll(false, "Trigger leaked without tracking");
        fixture_trigger_context.tracking = true;
        poll(false, "Held trigger rearmed after tracking returned");
        release();
        press();
        fixture_swap_roles = false; // Roles now resolve to different device indices.
        poll(false, "Role reassignment carried a held trigger");
        release();
        press();
        fixture_trigger_context.menu = true;
        fixture_trigger_context.fade = 1;
        poll(false, "Held trigger selected a menu item on entry");
        release();
        press();
        poll(true, "Menu trigger hold was lost");
        release();
        fixture_trigger_context.menu = false;
        fixture_trigger_context.fade = 0;
        poll(false, "Menu exit emitted a trigger press");
        TestInputKey(nullptr, 0x135, true);
        if (!fixture_trigger_pressed)
            throw std::runtime_error("An unrelated key caller was changed");
        poll(false, "Native trigger did not clear an unrelated held value");
        fixture_swap_roles = true;
        fixture_left_trigger = false;
        release();
        press();
        command(SessionCommand::disable);
        poll(false, "Disabled fix still used the right-hand trigger");
        fixture_left_trigger = true;
        poll(true, "Disabled fix did not preserve the native trigger");
        command(SessionCommand::enable);
        poll(false, "Enable reused a held trigger");
        release();
        press();
        command(SessionCommand::stop);
        fixture_left_trigger = false;
        poll(false, "Stop did not restore native trigger release");
        fixture_left_trigger = true;
        fixture_right_trigger = false;
        poll(true, "Stop did not restore native trigger press");
        std::cout
            << "Right-hand trigger routing, press/hold/release, rearming and native restoration passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        try {
            command(SessionCommand::stop);
        } catch (...) {
        }
        return 1;
    }
}
