#pragma once
#include "input/controller_input.hpp"
#include "input/puzzle_aim.hpp"
#include "input/resting_height.hpp"
#include <array>
#include <cstring>
extern witness::input::HeightFrame fixture_height;
extern int fixture_player;
extern unsigned native_eye_calls;
extern "C" witness::input::Vec3* TestInputEye(witness::input::Vec3*, void*);
int check_height_hook();
int check_trigger_hook();
int check_smooth_turn_hook();
int check_cursor_fallback_hook();
int check_puzzle_reposition_hook();
witness::input::Axis game_cursor();
void game_vr_update();
void game_vr_rotations();
extern float fixture_rotations[3][4];
extern "C" void TestInputRotation(float*, float, float, float, float);
extern witness::input::Axis fixture_right_axis;
extern bool fixture_trigger_scenario, fixture_left_trigger, fixture_right_trigger, fixture_swap_roles,
    fixture_controllers_connected, fixture_captured, fixture_controller_state_valid;
extern witness::input::Context fixture_trigger_context;
extern unsigned fixture_trigger_presses;
extern bool fixture_hmd_connected, fixture_right_pose_valid, fixture_pose_read_success;
extern unsigned input_frame;
extern unsigned native_polls;
extern unsigned fixture_poll_delay_ms;
extern "C" void TestInputPoll();
extern "C" witness::input::Vec3* TestInputMove(witness::input::Vec3*);
extern "C" witness::input::Context TestInputContext();
void initialize_input_fixture();

extern "C" void TestInputSetLegacy(bool);

void TestInputOverride(int);
unsigned TestInputPhase();

extern "C" witness::input::Axis* TestInputCursor(witness::input::Axis*, bool);
void TestInputRecenter(bool value);
void TestInputAimScenario(bool);

void TestInputSnapScenario(bool);
extern "C" void TestInputVrUpdate();
extern "C" float TestInputYaw, TestInputVrYaw;
extern float fixture_seen_yaw, fixture_seen_vr_yaw;
extern unsigned native_vr_updates;

extern "C" void TestInputKey(void*, int, bool);
extern bool fixture_pad_pressed, fixture_trigger_pressed, fixture_menu_pressed;
void TestInputStickScenario(bool);

void TestInputBack(bool);
void TestInputMenu(bool, bool);
extern unsigned native_menu_events, native_menu_leaks;
void TestInputNavigation(int, bool);
extern unsigned native_navigation[4], navigation_leaks;
extern bool navigation_held;
extern unsigned native_back_events, native_back_leaks;

// Byte-backed actors retain the verified native offsets without borrowing real game objects.
struct FixtureApproachActor {
    alignas(void*) std::array<unsigned char, 0x270> bytes{};
};

struct FixtureApproachTarget {
    alignas(void*) std::array<unsigned char, 0x20> bytes{};
};

struct FixtureApproachArguments {
    void* player{};
    void* target{};
    int mode{};
    float duration{}, turn_scale{};
    int flag6{}, flag7{}, flag8{};
};

template <class T> T fixture_field(const void* object, std::size_t offset) {
    T value{};
    std::memcpy(&value, static_cast<const unsigned char*>(object) + offset, sizeof(value));
    return value;
}

template <class T> void fixture_field(void* object, std::size_t offset, T value) {
    std::memcpy(static_cast<unsigned char*>(object) + offset, &value, sizeof(value));
}

extern FixtureApproachActor fixture_approach_player;
extern FixtureApproachTarget fixture_approach_target;
extern FixtureApproachArguments fixture_approach_arguments;
extern unsigned fixture_approach_calls, fixture_finish_approach_calls, fixture_approach_side_effects;
extern bool fixture_approach_change_mode, fixture_approach_change_target;
void initialize_approach_fixture();
void advance_approach_fixture(void* player);
void game_puzzle_approach(void*, void*, int, float, float, int, int, int);
extern "C" void TestInputApproach(void*, void*, int, float, float, int, int, int);
extern "C" void TestInputFinishApproach(void*);
