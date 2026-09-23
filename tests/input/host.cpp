#include "targets.hpp"
#include "common/win_util.hpp"
#include <cmath>
#include <iostream>
#include <memory>

__declspec(noinline) witness::input::Vec3 game_move() {
    witness::input::Vec3 out;
    volatile auto* result = TestInputMove(&out);
    (void)result;
    return out;
}

__declspec(noinline) witness::input::Axis game_cursor() {
    witness::input::Axis out;
    volatile auto* result = TestInputCursor(&out, false);
    (void)result;
    return out;
}

__declspec(noinline) void game_vr_update() {
    TestInputVrUpdate();
    // Keep a real call/return address even in optimized builds.
    volatile auto observed = fixture_seen_yaw;
    (void)observed;
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2)
        return 2;
    initialize_input_fixture();
    game_move();
    game_cursor();
    game_vr_update();
    TestInputPoll();
    if (!wcscmp(argv[1], L"--height"))
        return check_height_hook();
    witness::Handle stop(CreateEventW(nullptr, TRUE, FALSE, argv[1]));
    const auto legacy_name = std::wstring(argv[1]) + L"-Legacy";
    witness::Handle legacy(CreateEventW(nullptr, TRUE, FALSE, legacy_name.c_str()));
    const auto held_name = std::wstring(argv[1]) + L"-Held",
               neutral_name = std::wstring(argv[1]) + L"-Neutral";
    witness::Handle held(CreateEventW(nullptr, TRUE, FALSE, held_name.c_str()));
    witness::Handle neutral_event(CreateEventW(nullptr, TRUE, FALSE, neutral_name.c_str()));
    const auto aim_name = std::wstring(argv[1]) + L"-Aim";
    witness::Handle aim_event(CreateEventW(nullptr, TRUE, FALSE, aim_name.c_str()));
    const auto recenter_name = std::wstring(argv[1]) + L"-Recenter";
    witness::Handle recenter_event(CreateEventW(nullptr, TRUE, FALSE, recenter_name.c_str()));
    const auto snap_name = std::wstring(argv[1]) + L"-Snap";
    witness::Handle snap_event(CreateEventW(nullptr, TRUE, FALSE, snap_name.c_str()));
    const auto stick_name = std::wstring(argv[1]) + L"-Stick";
    witness::Handle stick_event(CreateEventW(nullptr, TRUE, FALSE, stick_name.c_str()));
    const auto back_name = std::wstring(argv[1]) + L"-Back";
    witness::Handle back_event(CreateEventW(nullptr, TRUE, FALSE, back_name.c_str()));
    witness::Handle menu_event(
        CreateEventW(nullptr, TRUE, FALSE, (std::wstring(argv[1]) + L"-Menu").c_str()));
    witness::Handle menu_check(
        CreateEventW(nullptr, TRUE, FALSE, (std::wstring(argv[1]) + L"-MenuCheck").c_str()));
    witness::Handle paused_event(
        CreateEventW(nullptr, TRUE, FALSE, (std::wstring(argv[1]) + L"-Paused").c_str()));
    if (!menu_event || !menu_check || !paused_event || !back_event || !stick_event || !snap_event ||
        !recenter_event || !aim_event || !stop || !legacy || !held || !neutral_event)
        return 3;
    const wchar_t* nav_names[]{L"-Nav", L"-NavUp", L"-NavDown", L"-NavLeft", L"-NavRight", L"-NavHand"};
    std::unique_ptr<witness::Handle> nav_events[6];
    for (unsigned i = 0; i < 6; ++i) {
        nav_events[i] = std::make_unique<witness::Handle>(
            CreateEventW(nullptr, TRUE, FALSE, (std::wstring(argv[1]) + nav_names[i]).c_str()));
        if (!*nav_events[i])
            return 3;
    }
    const auto start = GetTickCount64();
    unsigned moved{}, leaks{}, right{}, neutral{}, decoy_leaks{}, aim_changed{}, aim_leaks{},
        aim_decoy_leaks{};
    unsigned angle22{}, angle45{}, angle90{}, turns{}, turn_leaks{}, turn_decoy_leaks{}, turn_repeat_leaks{},
        turn_order_leaks{};
    unsigned pad_filtered{}, key_leaks{};
    bool turn_in_deflection = false;
    while (WaitForSingleObject(stop.get(), 0) == WAIT_TIMEOUT && GetTickCount64() - start < 90000) {
        TestInputOverride(WaitForSingleObject(paused_event.get(), 0) == WAIT_OBJECT_0    ? 3
                          : WaitForSingleObject(neutral_event.get(), 0) == WAIT_OBJECT_0 ? 2
                          : WaitForSingleObject(held.get(), 0) == WAIT_OBJECT_0          ? 1
                                                                                         : 0);
        TestInputSetLegacy(WaitForSingleObject(legacy.get(), 0) == WAIT_OBJECT_0);
        const bool aim_scenario = WaitForSingleObject(aim_event.get(), 0) == WAIT_OBJECT_0;
        TestInputAimScenario(aim_scenario);
        const bool snap_scenario = WaitForSingleObject(snap_event.get(), 0) == WAIT_OBJECT_0;
        TestInputSnapScenario(snap_scenario);
        TestInputRecenter(WaitForSingleObject(recenter_event.get(), 0) == WAIT_OBJECT_0);
        const bool stick_scenario = WaitForSingleObject(stick_event.get(), 0) == WAIT_OBJECT_0;
        TestInputStickScenario(stick_scenario);
        TestInputBack(WaitForSingleObject(back_event.get(), 0) == WAIT_OBJECT_0);
        TestInputMenu(WaitForSingleObject(menu_event.get(), 0) == WAIT_OBJECT_0,
                      WaitForSingleObject(menu_check.get(), 0) == WAIT_OBJECT_0);
        int nav = -1;
        if (WaitForSingleObject(nav_events[0]->get(), 0) == WAIT_OBJECT_0) {
            nav = 0;
            for (int i = 1; i <= 4; ++i)
                if (WaitForSingleObject(nav_events[i]->get(), 0) == WAIT_OBJECT_0)
                    nav = i;
        }
        TestInputNavigation(nav, WaitForSingleObject(nav_events[5]->get(), 0) == WAIT_OBJECT_0);
        TestInputPoll();
        if (navigation_held)
            ++navigation_leaks;
        const auto result = game_move();
        const unsigned p = TestInputPhase();
        if (!fixture_trigger_pressed)
            ++key_leaks;
        if (!fixture_pad_pressed &&
            ((p >= 10 && p < 60) || (p >= 70 && p < 110) || (p >= 120 && p < 140) || (p >= 150 && p < 170) ||
             (p >= 180 && p < 200) || (p >= 210 && p < 220) || p >= 230)) {
            ++pad_filtered;
            const auto c = TestInputContext();
            if (!puzzle(c) && !c.menu)
                ++key_leaks;
        }
        TestInputKey(nullptr, 0x136, true);
        if (!fixture_pad_pressed)
            ++key_leaks; // unrelated caller
        TestInputKey(nullptr, 0x13d, true);
        if (!fixture_menu_pressed)
            ++key_leaks;
        const bool changed =
            std::abs(result.x - .1f) > .0001f || std::abs(result.y) > .0001f || result.z != 0;
        const bool can_move = (p >= 10 && p < 40) || (p >= 70 && p < 90) || (p >= 230);
        if (changed) {
            ++moved;
            if (!can_move)
                ++leaks;
            if (result.y < -.9f)
                ++right;
        } else
            ++neutral;
        witness::input::Vec3 decoy;
        TestInputMove(&decoy);
        if (decoy.x != .1f || decoy.y != 0 || decoy.z != 0)
            ++decoy_leaks;
        const float before_yaw = TestInputYaw, before_vr = TestInputVrYaw;
        const auto original_count = native_vr_updates;
        TestInputVrUpdate(); // Different caller must neither consume the edge nor write headings.
        if (TestInputYaw != before_yaw || TestInputVrYaw != before_vr)
            ++turn_decoy_leaks;
        game_vr_update();
        const float yaw_delta = TestInputYaw - before_yaw, vr_delta = TestInputVrYaw - before_vr;
        const bool centered = p < 10 || (p >= 60 && p < 70) || (p >= 110 && p < 120) ||
                              (p >= 140 && p < 150) || (p >= 170 && p < 180) || (p >= 200 && p < 210) ||
                              (p >= 220 && p < 230);
        if (centered)
            turn_in_deflection = false;
        if (std::abs(yaw_delta) > .00001f || std::abs(vr_delta) > .00001f) {
            ++turns;
            const bool legal_phase = (p >= 10 && p < 40) || (p >= 70 && p < 90) || p >= 230;
            const float magnitude = std::abs(yaw_delta);
            if (std::abs(magnitude - .3926990817f) < .00001f)
                ++angle22;
            if (std::abs(magnitude - .7853981634f) < .00001f)
                ++angle45;
            if (std::abs(magnitude - 1.5707963268f) < .00001f)
                ++angle90;
            const bool legal_angle = std::abs(magnitude - .3926990817f) < .00001f ||
                                     std::abs(magnitude - .7853981634f) < .00001f ||
                                     std::abs(magnitude - 1.5707963268f) < .00001f;
            if (!snap_scenario || aim_scenario || !legal_phase || !legal_angle ||
                std::abs(yaw_delta - vr_delta) > .00001f ||
                (p >= 70 && p < 90 ? yaw_delta < 0 : yaw_delta > 0))
                ++turn_leaks;
            if (turn_in_deflection)
                ++turn_repeat_leaks;
            turn_in_deflection = true;
        }
        if (fixture_seen_yaw != TestInputYaw || fixture_seen_vr_yaw != TestInputVrYaw)
            ++turn_order_leaks;
        const float once_yaw = TestInputYaw, once_vr = TestInputVrYaw;
        game_vr_update(); // Same native caller a second time cannot replay a consumed sample.
        if (TestInputYaw != once_yaw || TestInputVrYaw != once_vr)
            ++turn_repeat_leaks;
        if (native_vr_updates != original_count + 3)
            ++turn_order_leaks;
        const auto cursor = game_cursor();
        const bool cursor_changed =
            std::abs(cursor.x - .001f) > .00001f || std::abs(cursor.y + .002f) > .00001f;
        const bool can_aim = aim_scenario && (p < 40 || (p >= 60 && p < 90) || (p >= 110 && p < 120) ||
                                              (p >= 140 && p < 150) || (p >= 170 && p < 180) ||
                                              (p >= 200 && p < 210) || p >= 220);
        if (cursor_changed) {
            ++aim_changed;
            if (!can_aim || (!stick_scenario && cursor.x < 0) || std::hypot(cursor.x, cursor.y) > .08001f)
                ++aim_leaks;
        }
        witness::input::Axis decoy_cursor;
        TestInputCursor(&decoy_cursor, false);
        if (decoy_cursor.x != .001f || decoy_cursor.y != -.002f)
            ++aim_decoy_leaks;
        input_frame += 3;
        Sleep(3);
    }
    std::cout << "{\"moved\":" << moved << ",\"right\":" << right << ",\"neutral\":" << neutral
              << ",\"leaks\":" << leaks << ",\"decoy_leaks\":" << decoy_leaks
              << ",\"aim_changed\":" << aim_changed << ",\"aim_leaks\":" << aim_leaks
              << ",\"aim_decoy_leaks\":" << aim_decoy_leaks << ",\"nav_up\":" << native_navigation[0]
              << ",\"nav_down\":" << native_navigation[1] << ",\"nav_left\":" << native_navigation[2]
              << ",\"nav_right\":" << native_navigation[3] << ",\"navigation_leaks\":" << navigation_leaks
              << ",\"native_menu_events\":" << native_menu_events
              << ",\"native_menu_leaks\":" << native_menu_leaks
              << ",\"native_back_events\":" << native_back_events
              << ",\"native_back_leaks\":" << native_back_leaks << ",\"pad_filtered\":" << pad_filtered
              << ",\"key_leaks\":" << key_leaks << ",\"angle22\":" << angle22 << ",\"angle45\":" << angle45
              << ",\"angle90\":" << angle90 << ",\"turns\":" << turns << ",\"turn_leaks\":" << turn_leaks
              << ",\"turn_decoy_leaks\":" << turn_decoy_leaks
              << ",\"turn_repeat_leaks\":" << turn_repeat_leaks
              << ",\"turn_order_leaks\":" << turn_order_leaks << ",\"polls\":" << native_polls << "}\n";
    return navigation_leaks || native_menu_leaks || native_back_leaks || key_leaks || leaks || decoy_leaks ||
                   aim_leaks || aim_decoy_leaks || turn_leaks || turn_decoy_leaks || turn_repeat_leaks ||
                   turn_order_leaks
               ? 4
               : 0;
}
