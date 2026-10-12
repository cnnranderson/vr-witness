#include "targets.hpp"
#include "common/protocol.hpp"
#include "common/win_util.hpp"
#include <array>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace witness;
using Control = DWORD(WINAPI*)(void*);

void require(bool result, const char* reason) {
    if (!result)
        throw std::runtime_error(reason);
}

SessionRequest request(SessionCommand action) {
    SessionRequest value{};
    value.size = sizeof(value);
    value.version = kSessionProtocolVersion;
    value.command = action;
    return value;
}

void check_short_headers(Control control) {
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    auto* memory = static_cast<std::byte*>(
        VirtualAlloc(nullptr, system.dwPageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    require(memory != nullptr, "Cannot allocate render protocol guard pages");

    struct Allocation {
        void* address;

        ~Allocation() { VirtualFree(address, 0, MEM_RELEASE); }
    } allocation{memory};

    DWORD previous{};
    require(VirtualProtect(memory + system.dwPageSize, system.dwPageSize, PAGE_NOACCESS, &previous),
            "Cannot protect render protocol guard page");
    // Only the eight-byte header is readable. Copying a whole request before validating it faults.
    auto* header = memory + system.dwPageSize - sizeof(std::array<std::uint32_t, 2>);
    for (const auto value : {std::array<std::uint32_t, 2>{2104, kSessionProtocolVersion},
                             std::array<std::uint32_t, 2>{2120, kSessionProtocolVersion},
                             std::array<std::uint32_t, 2>{sizeof(SessionRequest), 2},
                             std::array<std::uint32_t, 2>{sizeof(SessionRequest), 3}}) {
        std::memcpy(header, value.data(), sizeof(value));
        require(control(header) == static_cast<DWORD>(ProbeResult::invalid_request),
                "Old render protocol size/version was accepted");
    }
}
} // namespace

int check_hands_hook() {
    const auto root = std::filesystem::path(module_path()).parent_path();
    const auto library = LoadLibraryW((root / L"witness-render-probe.dll").c_str());
    const auto control = library ? reinterpret_cast<Control>(reinterpret_cast<void*>(
                                       GetProcAddress(library, "WitnessSessionControl")))
                                 : nullptr;
    if (!control)
        return 1;
    const auto command = [&](SessionCommand action, bool hands = false, bool occlusion = false) {
        auto value = request(action);
        value.show_hands = hands;
        value.world_occlusion = occlusion;
        require(!control(&value), "Hand fixture session command failed");
        if (action == SessionCommand::start)
            require(value.show_hands == unsigned(hands) && value.world_occlusion == unsigned(occlusion),
                    "Render start lost hand or world occlusion preference");
        return value;
    };
    const auto eye = [&](int which, unsigned expected_calls, bool duplicate = false) {
        const auto before = hands_calls.load();
        TestBeginEye(which);
        TestCursor(nullptr, nullptr, false);
        if (duplicate)
            TestCursor(nullptr, nullptr, false);
        TestMenu(false, which, false);
        TestMenu(true, 1 - which, false);
        TestMenu(true, which, true);
        TestMenu(true, which, false);
        require(hands_calls == before + expected_calls, "Hands drew an unexpected number of times");
    };
    const auto outside_cursor = [&] {
        const auto before = hands_calls.load();
        stage = 0;
        TestSubmit(0);
        TestCursor(nullptr, nullptr, false);
        require(hands_calls == before, "Hands drew outside a pending stereo eye");
    };
    try {
        check_short_headers(control);
        auto invalid = request(SessionCommand::start);
        invalid.show_hands = 2;
        require(control(&invalid) == static_cast<DWORD>(ProbeResult::invalid_request),
                "Invalid hands option was accepted");
        invalid = request(SessionCommand::status);
        invalid.hands_fault = 1;
        require(control(&invalid) == static_cast<DWORD>(ProbeResult::invalid_request),
                "Nonzero hands fault response was accepted as input");
        invalid = request(SessionCommand::status);
        invalid.hand_draws = 1;
        require(control(&invalid) == static_cast<DWORD>(ProbeResult::invalid_request),
                "Nonzero hands draw response was accepted as input");
        invalid = request(SessionCommand::start);
        invalid.world_occlusion = 2;
        require(control(&invalid) == static_cast<DWORD>(ProbeResult::invalid_request),
                "Invalid occlusion option was accepted");
        invalid = request(SessionCommand::status);
        invalid.occlusion_active = 1;
        require(control(&invalid) == static_cast<DWORD>(ProbeResult::invalid_request),
                "Nonzero occlusion active response was accepted as input");
        invalid = request(SessionCommand::status);
        invalid.occlusion_draws = 1;
        require(control(&invalid) == static_cast<DWORD>(ProbeResult::invalid_request),
                "Nonzero occlusion draw response was accepted as input");

        auto state = command(SessionCommand::start);
        require(!state.show_hands && !state.hands_fault && !state.hand_draws,
                "Hands were not disabled by default");
        require(!state.world_occlusion && !state.occlusion_active && !state.occlusion_draws,
                "World occlusion was not disabled by default");
        eye(0, 0);
        eye(1, 0);
        outside_cursor();
        command(SessionCommand::stop);

        command(SessionCommand::start, false, true);
        eye(0, 0);
        state = command(SessionCommand::status);
        require(state.world_occlusion && !state.show_hands && !state.occlusion_active &&
                    !state.occlusion_draws,
                "Occlusion without hands should retain preference without drawing");
        command(SessionCommand::stop);

        state = command(SessionCommand::start, true, true);
        require(!state.hands_fault && !state.hand_draws, "New hand session retained old counters/fault");
        eye(0, 1, true);
        eye(1, 1, true);
        outside_cursor();
        require(command(SessionCommand::status).hand_draws == 2,
                "Per-eye hand counter did not match successful draws");

        fixture_cursor_inside_begin = true;
        eye(0, 1); // Internal cursor passes must not draw before the scene routine returns.
        fixture_cursor_inside_begin = false;
        require(command(SessionCommand::status).hand_draws == 3,
                "Nested cursor call drew hands or consumed the later eligible draw");

        state = command(SessionCommand::disable);
        require(state.show_hands && state.world_occlusion && !state.enabled,
                "Disable lost the hand or occlusion preference");
        eye(0, 0);
        eye(1, 0);
        outside_cursor();
        require(command(SessionCommand::status).hand_draws == 3, "Disabled hands incremented draw totals");
        state = command(SessionCommand::enable);
        require(state.show_hands && state.world_occlusion && state.enabled,
                "Enable lost the hand or occlusion preference");
        eye(0, 1);
        require(command(SessionCommand::status).hand_draws == 4, "Hands did not resume after enabling");
        command(SessionCommand::stop);
        eye(0, 0);
        outside_cursor();

        command(SessionCommand::start, true);
        const auto fallback_calls = hands_calls.load();
        TestBeginEye(0); // Skip both completion passes for this eye.
        TestBeginEye(1); // Flush the old eye through fallback and suspend hand drawing.
        TestCursor(nullptr, nullptr, false);
        TestMenu(true, 1, false);
        eye(0, 0, true);
        state = command(SessionCommand::status);
        require(hands_calls == fallback_calls && !state.hand_draws && state.fault && state.fallback,
                "Fallback route drew hands or failed to suspend unsafe rendering");
        command(SessionCommand::stop);

        command(SessionCommand::start, true);
        fixture_hands_result = false;
        eye(0, 1, true);
        require(!command(SessionCommand::status).hand_draws,
                "Skipped hand draw was counted as a successful draw");
        command(SessionCommand::stop);
        fixture_hands_result = true;
        state = command(SessionCommand::start, true);
        require(!state.hands_fault && !state.hand_draws, "Restart did not reset hand state");
        eye(1, 1);
        require(command(SessionCommand::status).hand_draws == 1, "Hands did not recover on fresh start");
        command(SessionCommand::stop);
        state = command(SessionCommand::start);
        eye(0, 0);
        require(!state.show_hands && !state.hand_draws && !state.world_occlusion && !state.occlusion_active &&
                    !state.occlusion_draws,
                "Restart did not restore default hand and occlusion preferences");
        command(SessionCommand::stop);
        require(!hands_bad_stage && !hands_bad_eye, "Hands drew after native cursor or into the wrong eye");
        std::cout << "Hand draw ordering, stereo ownership, duplicate/nested cursor gates, fallback, "
                     "protocol guard pages, and lifecycle passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        fixture_hands_result = true;
        fixture_cursor_inside_begin = false;
        try {
            command(SessionCommand::stop);
        } catch (...) {
        }
        return 1;
    }
}
