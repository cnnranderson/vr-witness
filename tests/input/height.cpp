#include "targets.hpp"
#include "common/protocol.hpp"
#include "common/fixes_toggle.hpp"
#include "common/win_util.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace witness;
using namespace witness::input;

int check_height_hook() {
    const auto root = std::filesystem::path(module_path()).parent_path();
    const auto file = root / (L"input-settings-test-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    using Control = DWORD(WINAPI*)(InputSessionRequest*);
    auto library = LoadLibraryW((root / L"witness-input-probe.dll").c_str());
    auto control = library ? reinterpret_cast<Control>(reinterpret_cast<void*>(
                                 GetProcAddress(library, "WitnessInputSessionControl")))
                           : nullptr;
    if (!control)
        return 1;
    const auto command = [&](SessionCommand action) {
        InputSessionRequest request{};
        request.size = sizeof(request);
        request.version = kInputProtocolVersion;
        request.command = action;
        request.aim_speed_percent = 60;
        request.aim_smoothing_ms = 80;
        if (control(&request))
            throw std::runtime_error("Height fixture session command failed");
        return request;
    };
    const auto check = [&](void* entity, float expected) {
        Vec3 result{};
        auto* returned = TestInputEye(&result, entity);
        if (returned != &result || result.x != 10 || result.y != 20 || std::abs(result.z - expected) > .0001f)
            throw std::runtime_error("Height fixture eye position mismatch");
    };
    const auto status = [&](bool calibrated, float height, float offset) {
        const auto result = command(SessionCommand::status);
        if (bool(result.height_calibrated) != calibrated || std::abs(result.height_m - height) > .0001f ||
            std::abs(result.height_offset_m - offset) > .0001f)
            throw std::runtime_error("Height fixture status mismatch");
    };
    const auto calibrate = [&](float before, float after, bool reset = false) {
        fixture_height.reset = reset;
        fixture_height.key_down = false;
        check(&fixture_player, before);
        fixture_height.key_down = true;
        check(&fixture_player, after);
    };
    int code = 0;
    try {
        std::uint32_t old_header[]{2192, 6};
        if (!control(reinterpret_cast<InputSessionRequest*>(old_header)))
            throw std::runtime_error("Legacy input protocol was accepted");
        if (!WritePrivateProfileStringW(L"Input", L"RestingHeight", L"2", file.c_str()))
            throw win_error("Write legacy height setting");
        fixture_height = {&fixture_player, 1.f, 1.8f, true, true, true, false};
        command(SessionCommand::start);
        check(&fixture_player, 101.f); // Old settings and a held F7 cannot calibrate on launch.
        status(false, 0, 0);
        calibrate(101.f, 101.8f);
        status(true, 1.8f, .8f);
        check(&fixture_player, 101.8f);
        int other{};
        check(&other, 101.f);
        fixture_height.tracked_height = .75f;
        check(&fixture_player, 101.55f); // Preserve a 0.25 m crouch while F7 stays held.
        fixture_height.can_calibrate = false;
        check(&fixture_player, 101.55f);
        fixture_height.native_vr = false;
        check(&fixture_player, 100.75f);
        fixture_height.native_vr = true;
        fixture_height.can_calibrate = true;
        check(&fixture_player, 101.55f);
        command(SessionCommand::disable);
        check(&fixture_player, 100.75f);
        command(SessionCommand::enable);
        check(&fixture_player, 101.55f);
        FixesToggle toggle;
        Sleep(30);
        toggle.publish(false);
        Sleep(40);
        check(&fixture_player, 100.75f);
        toggle.publish(true);
        Sleep(40);
        check(&fixture_player, 101.55f);
        command(SessionCommand::stop);
        check(&fixture_player, 100.75f);
        command(SessionCommand::start);
        check(&fixture_player, 101.55f); // Settings restarts retain this process's calibration.
        calibrate(101.55f, 101.8f);
        status(true, 1.8f, 1.05f);
        calibrate(101.8f, 100.75f, true);
        status(false, 0, 0);
        command(SessionCommand::stop);
        check(&fixture_player, 100.75f);
        if (native_eye_calls < 20)
            throw std::runtime_error("Height fixture bypassed the original function");
        std::cout << "Height hotkey, status, native routing, combined toggle and stop passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        code = 1;
        try {
            command(SessionCommand::stop);
        } catch (...) {
        }
    }
    std::filesystem::remove(file);
    return code;
}
