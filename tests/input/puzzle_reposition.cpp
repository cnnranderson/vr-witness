#include "targets.hpp"
#include "common/protocol.hpp"
#include "common/win_util.hpp"
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace witness;
using namespace witness::input;

int check_puzzle_reposition_hook() {
    const auto root = std::filesystem::path(module_path()).parent_path();
    using Control = DWORD(WINAPI*)(InputSessionRequest*);
    const auto library = LoadLibraryW((root / L"witness-input-probe.dll").c_str());
    const auto control = library ? reinterpret_cast<Control>(reinterpret_cast<void*>(
                                       GetProcAddress(library, "WitnessInputSessionControl")))
                                 : nullptr;
    if (!control)
        return 1;
    const auto request = [](SessionCommand action) {
        InputSessionRequest value{};
        value.size = sizeof(value);
        value.version = kInputProtocolVersion;
        value.command = action;
        value.aim_speed_percent = 60;
        value.aim_smoothing_ms = 80;
        return value;
    };
    const auto command = [&](SessionCommand action, bool suppress = false) {
        auto value = request(action);
        value.disable_puzzle_reposition = suppress;
        if (control(&value))
            throw std::runtime_error("Puzzle positioning fixture session command failed");
        if (action == SessionCommand::start && value.disable_puzzle_reposition != unsigned(suppress))
            throw std::runtime_error("Puzzle positioning start option was not retained");
        return value;
    };
    const auto reset = [&] {
        initialize_approach_fixture();
        fixture_height.player = &fixture_approach_player;
        fixture_height.native_vr = true;
    };
    const auto exercise = [&](bool suppressed, bool authorized = true, int mode = 1) {
        auto* player = &fixture_approach_player;
        auto* target = &fixture_approach_target;
        const auto position = fixture_field<Vec3>(player, 0x24);
        const auto before_setup = fixture_approach_calls;
        const auto before_finish = fixture_finish_approach_calls;
        const auto before_side_effects = fixture_approach_side_effects;
        const auto before_count = command(SessionCommand::status).puzzle_repositions_blocked;
        constexpr float duration = .75f, turn_scale = .625f;
        constexpr int flag6 = 3, flag7 = 5, flag8 = 7;
        if (authorized)
            game_puzzle_approach(player, target, mode, duration, turn_scale, flag6, flag7, flag8);
        else
            TestInputApproach(player, target, mode, duration, turn_scale, flag6, flag7, flag8);
        const auto& args = fixture_approach_arguments;
        if (fixture_approach_calls != before_setup + 1 ||
            fixture_approach_side_effects != before_side_effects + 1 || args.player != player ||
            args.target != target || args.mode != mode || args.duration != duration ||
            args.turn_scale != turn_scale || args.flag6 != flag6 || args.flag7 != flag7 ||
            args.flag8 != flag8)
            throw std::runtime_error("Approach setup side effects or mixed ABI arguments were changed");
        if (fixture_finish_approach_calls != before_finish + unsigned(suppressed) ||
            command(SessionCommand::status).puzzle_repositions_blocked != before_count + unsigned(suppressed))
            throw std::runtime_error("Approach finish routing or suppression counter mismatch");
        const auto native_mode = fixture_approach_change_mode ? 2 : mode;
        if (fixture_field<int>(player, 0xD0) != (suppressed ? 0 : native_mode) ||
            fixture_field<int>(player, 0x258) != (suppressed ? 0 : 7) ||
            fixture_field<int>(player, 0xFC) != (suppressed ? 0 : flag6) ||
            fixture_field<int>(player, 0x100) != (suppressed ? 0 : flag7) ||
            fixture_field<int>(player, 0x104) != (suppressed ? 0 : flag8) ||
            fixture_field<float>(player, 0x108) !=
                (suppressed ? std::numeric_limits<float>::max() : duration))
            throw std::runtime_error("Approach state did not preserve native setup/finish semantics");
        const auto expected_target =
            fixture_field<int>(target, 0x10) + (fixture_approach_change_target ? 1 : 0);
        if (fixture_field<int>(player, 0xF0) != expected_target || fixture_field<int>(player, 0xF4) != 0x3579)
            throw std::runtime_error("Selected puzzle target or setup state was discarded");
        const auto after = fixture_field<Vec3>(player, 0x24);
        if (after.x != position.x || after.y != position.y || after.z != position.z)
            throw std::runtime_error("Approach hook moved the player immediately");
        advance_approach_fixture(player);
        const auto moved = fixture_field<Vec3>(player, 0x24);
        const bool moving = !suppressed && native_mode == 1;
        if (moved.x != position.x + (moving ? 3 : 0) || moved.y != position.y + (moving ? 4 : 0) ||
            moved.z != position.z + (moving ? 5 : 0))
            throw std::runtime_error("Later movement did not follow the retained approach state");
    };
    try {
        for (auto header : {std::array<std::uint32_t, 2>{2208, kInputProtocolVersion},
                            std::array<std::uint32_t, 2>{sizeof(InputSessionRequest), 8}})
            if (control(reinterpret_cast<InputSessionRequest*>(header.data())) !=
                static_cast<DWORD>(ProbeResult::invalid_request))
                throw std::runtime_error("Old input request size or version was accepted");
        auto invalid = request(SessionCommand::start);
        invalid.disable_puzzle_reposition = 2;
        if (control(&invalid) != static_cast<DWORD>(ProbeResult::invalid_request))
            throw std::runtime_error("Invalid puzzle positioning option was accepted");
        invalid = request(SessionCommand::status);
        invalid.puzzle_repositions_blocked = 1;
        if (control(&invalid) != static_cast<DWORD>(ProbeResult::invalid_request))
            throw std::runtime_error("Nonzero puzzle positioning output counter was accepted as input");

        reset();
        command(SessionCommand::start);
        exercise(false); // Default leaves the complete native movement route intact.
        command(SessionCommand::stop);
        command(SessionCommand::start, true);
        if (fixture_field<int>(&fixture_approach_player, 0xD0) != 1)
            throw std::runtime_error("Changing the option canceled an already active approach");
        reset();
        exercise(true);
        reset();
        exercise(false, false); // Another native caller retains its approach behavior.
        reset();
        exercise(false, true, 2);
        reset();
        fixture_field<std::uintptr_t>(&fixture_approach_target, 8, 0x62E678);
        exercise(false);
        reset();
        fixture_field<int>(&fixture_approach_target, 0x10, 0);
        exercise(false);
        reset();
        fixture_field<std::uintptr_t>(&fixture_approach_player, 8, 0x62E358);
        exercise(false);
        reset();
        FixtureApproachActor other = fixture_approach_player;
        fixture_height.player = &other;
        exercise(false);
        reset();
        fixture_height.native_vr = false;
        exercise(false);
        reset();
        fixture_approach_change_mode = true;
        exercise(false);
        reset();
        fixture_approach_change_target = true;
        exercise(false);

        reset();
        command(SessionCommand::disable);
        exercise(false);
        command(SessionCommand::enable);
        reset();
        exercise(true);
        command(SessionCommand::stop);
        reset();
        exercise(false);
        if (command(SessionCommand::start).puzzle_repositions_blocked != 0)
            throw std::runtime_error("Approach suppression counter did not reset for a new session");
        reset();
        exercise(false);
        command(SessionCommand::stop);
        command(SessionCommand::start, true);
        reset();
        exercise(true);
        command(SessionCommand::stop);
        std::cout
            << "Puzzle approach ABI, preserved setup, native finish, gates, protocol and lifecycle passed.\n";
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
