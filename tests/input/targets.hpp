#pragma once
#include "input/controller_input.hpp"
#include "input/puzzle_aim.hpp"
#include "input/resting_height.hpp"
extern witness::input::HeightFrame fixture_height;
extern int fixture_player;
extern unsigned native_eye_calls;
extern "C" witness::input::Vec3* TestInputEye(witness::input::Vec3*, void*);
int check_height_hook();
int check_trigger_hook();
int check_smooth_turn_hook();
void game_vr_update();
void game_vr_rotations();
extern float fixture_rotations[3][4];
extern "C" void TestInputRotation(float*, float, float, float, float);
extern witness::input::Axis fixture_right_axis;
extern bool fixture_trigger_scenario, fixture_left_trigger, fixture_right_trigger, fixture_swap_roles,
    fixture_controllers_connected, fixture_captured, fixture_controller_state_valid;
extern witness::input::Context fixture_trigger_context;
extern unsigned fixture_trigger_presses;
extern unsigned input_frame;
extern unsigned native_polls;
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
