#include "targets.hpp"
#include "common/protocol.hpp"
#include "common/win_util.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace witness;

int check_smooth_turn_hook() {
    const auto root = std::filesystem::path(module_path()).parent_path();
    using Control = DWORD(WINAPI*)(InputSessionRequest*);
    const auto library = LoadLibraryW((root / L"witness-input-probe.dll").c_str());
    const auto control = library ? reinterpret_cast<Control>(reinterpret_cast<void*>(
                                       GetProcAddress(library, "WitnessInputSessionControl")))
                                 : nullptr;
    if (!control)
        return 1;
    bool continuous = false;
    const auto verify_rotation = [&] {
        constexpr float step = 3.14159265358979323846f / 8.f;
        const auto angle = continuous ? TestInputVrYaw : std::nearbyint(TestInputVrYaw / step) * step;
        for (const auto& rotation : fixture_rotations)
            if (rotation[0] != 0 || rotation[1] != 0 ||
                std::abs(rotation[2] - std::sin(angle * .5f)) > .000001f ||
                std::abs(rotation[3] - std::cos(angle * .5f)) > .000001f)
                throw std::runtime_error(
                    "VR rotation consumer rounded a smooth heading or changed native output");
    };
    const auto command = [&](SessionCommand action, bool smooth = true) {
        InputSessionRequest request{};
        request.size = sizeof(request);
        request.version = kInputProtocolVersion;
        request.command = action;
        request.legacy_axis0 = 1;
        request.pointing = 1;
        request.aim_speed_percent = 60;
        request.aim_smoothing_ms = 80;
        request.smooth_turn_speed = smooth ? 60 : 0;
        request.snap_steps = smooth ? 0 : 1;
        if (control(&request) || request.smooth_turn_speed != (smooth ? 60u : 0u) ||
            request.snap_steps != (smooth ? 0u : 1u))
            throw std::runtime_error("Smooth turn fixture session command failed");
        continuous = smooth && (action == SessionCommand::start || action == SessionCommand::enable);
    };
    const auto stationary = [&] {
        const auto before = TestInputYaw;
        TestInputPoll();
        game_vr_update();
        if (TestInputYaw != before)
            throw std::runtime_error("Smooth turning leaked into a blocked context");
        verify_rotation();
    };
    const auto center = [&] {
        fixture_right_axis = {};
        stationary();
    };
    const auto turn = [&](float axis) {
        fixture_right_axis = {axis, 0};
        Sleep(20);
        const auto before = TestInputYaw, before_vr = TestInputVrYaw;
        TestInputPoll();
        game_vr_update();
        const auto delta = TestInputYaw - before;
        if ((axis > 0 ? delta >= 0 : delta <= 0) ||
            std::abs(delta - (TestInputVrYaw - before_vr)) > .00001f || fixture_seen_yaw != TestInputYaw ||
            fixture_seen_vr_yaw != TestInputVrYaw)
            throw std::runtime_error("Smooth heading direction, eye agreement or update order failed");
        verify_rotation();
        const auto after = TestInputYaw;
        game_vr_update();
        TestInputVrUpdate(); // An unrelated caller must not consume or add another turn.
        if (TestInputYaw != after)
            throw std::runtime_error("Smooth turn was applied more than once");
    };
    try {
        InputSessionRequest legacy{};
        legacy.size = sizeof(legacy);
        legacy.version = 7;
        if (!control(&legacy))
            throw std::runtime_error("Legacy input protocol was accepted");
        fixture_trigger_scenario = true;
        TestInputSetLegacy(true);
        fixture_right_axis = {1, 0};
        command(SessionCommand::start);
        stationary();
        center();
        turn(1);
        turn(1); // Holding continues to turn.
        turn(-1);
        float unrelated[4]{};
        TestInputRotation(unrelated, 1, 0, 0, .75f);
        if (std::abs(unrelated[0] - std::sin(.375f)) > .000001f || unrelated[1] != 0 || unrelated[2] != 0 ||
            std::abs(unrelated[3] - std::cos(.375f)) > .000001f)
            throw std::runtime_error("Unrelated rotation caller or axis was changed");
        fixture_trigger_context.mode = 0;
        stationary();
        fixture_trigger_context.mode = 1;
        stationary();
        fixture_trigger_context.mode = 2;
        stationary(); // Center after leaving a puzzle.
        center();
        turn(1);
        fixture_trigger_context.menu = true;
        fixture_trigger_context.fade = 1;
        stationary();
        fixture_trigger_context.menu = false;
        fixture_trigger_context.fade = 0;
        stationary();
        center();
        turn(1);
        fixture_trigger_context.focused = false;
        stationary();
        fixture_trigger_context.focused = true;
        stationary();
        center();
        turn(1);
        fixture_controllers_connected = false;
        stationary();
        fixture_controllers_connected = true;
        stationary();
        center();
        turn(1);
        fixture_swap_roles = true;
        stationary();
        center();
        turn(-1);
        Sleep(110);
        stationary(); // A timing gap cannot create a large catch-up turn.
        center();
        turn(1);
        command(SessionCommand::disable);
        stationary();
        command(SessionCommand::enable);
        stationary();
        center();
        turn(1);
        command(SessionCommand::stop);
        stationary();
        command(SessionCommand::start, false);
        center(); // Snap mode keeps the native rounded rotations after a smooth session.
        command(SessionCommand::stop, false);
        command(SessionCommand::start);
        center();
        turn(-1);
        command(SessionCommand::stop);
        stationary();
        std::cout
            << "Smooth turning native headings, continuous VR rotations, timing, context gates and lifecycle passed.\n";
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
