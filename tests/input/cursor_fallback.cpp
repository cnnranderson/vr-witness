#include "targets.hpp"
#include "common/protocol.hpp"
#include "common/win_util.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace witness;
using namespace witness::input;

int check_cursor_fallback_hook() {
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
            throw std::runtime_error("Cursor fallback fixture session command failed");
    };
    const auto frame = [&] {
        Sleep(20);
        TestInputPoll();
        return game_cursor();
    };
    const auto native = [&](const char* label) {
        const auto delta = frame();
        if (delta.x != .001f || delta.y != -.002f)
            throw std::runtime_error(label);
    };
    const auto manual = [&] {
        fixture_right_axis = {0, 1};
        const auto delta = frame();
        if (delta.x != 0 || delta.y <= 0 || delta.y > .3f)
            throw std::runtime_error("Manual cursor failed while controller pose was unavailable");
        Axis decoy;
        TestInputCursor(&decoy, false);
        if (decoy.x != .001f || decoy.y != -.002f)
            throw std::runtime_error("An unrelated cursor caller was changed");
    };
    const auto center = [&] {
        fixture_right_axis = {};
        native("Centering unexpectedly replaced native cursor input");
    };
    try {
        fixture_trigger_scenario = true;
        fixture_trigger_context.mode = 0;
        command(SessionCommand::start);
        for (const bool legacy : {false, true}) {
            TestInputSetLegacy(legacy);
            for (const bool failed_read : {false, true}) {
                command(SessionCommand::disable);
                command(SessionCommand::enable);
                fixture_right_pose_valid = failed_read;
                fixture_pose_read_success = !failed_read;
                fixture_right_axis = {0, 1};
                native("Held stick activated before neutral after attachment");
                center();
                manual();
                manual();

                fixture_trigger_context.tracking = false;
                native("Manual cursor leaked without native headset tracking");
                fixture_trigger_context.tracking = true;
                native("Held stick rearmed when native tracking returned");
                center();
                manual();

                fixture_hmd_connected = false;
                native("Manual cursor leaked while the headset was disconnected");
                fixture_hmd_connected = true;
                native("Held stick rearmed when the headset reconnected");
                center();
                manual();

                fixture_trigger_context.focused = false;
                native("Manual cursor leaked without game focus");
                fixture_trigger_context.focused = true;
                native("Held stick rearmed when game focus returned");
                center();
                manual();

                fixture_captured = true;
                native("Manual cursor leaked while the runtime captured input");
                fixture_captured = false;
                native("Held stick rearmed when runtime capture ended");
                center();
                manual();

                fixture_right_pose_valid = true;
                fixture_pose_read_success = true;
                manual();
                center(); // Pose recovery preserves manual ownership until recenter.
                TestInputRecenter(true);
                native("Recenter applied pointing before its release latch armed");
                TestInputRecenter(false);
                const auto pointing = frame();
                if (std::abs(pointing.x) > .00001f || std::abs(pointing.y) > .00001f)
                    throw std::runtime_error("Recovered pointing did not resume after recenter");
            }
        }
        command(SessionCommand::stop);
        native("Stopping did not restore native cursor input");
        std::cout
            << "Manual cursor pose fallback, tracking gates, neutral rearm and pointing recovery passed.\n";
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
