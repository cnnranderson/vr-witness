#include "input_targets.hpp"
#include "caller_address.hpp"
#include <windows.h>
using namespace witness::input;
unsigned input_frame{}, native_polls{};
bool fixture_back{}, fixture_menu{}, fixture_expect_swap{}, fixture_polling{};
unsigned native_menu_events{}, native_menu_leaks{};
void TestInputMenu(bool pressed, bool expect_swap) {
    fixture_menu = pressed;
    fixture_expect_swap = expect_swap;
}
int fixture_keyboard{};
int fixture_navigation{-1};
bool fixture_navigation_right{};
unsigned native_navigation[4]{}, navigation_leaks{};
bool navigation_held{};
void TestInputNavigation(int direction, bool right) {
    fixture_navigation = direction;
    fixture_navigation_right = right;
}
unsigned native_back_events{}, native_back_leaks{};
void TestInputBack(bool value) {
    fixture_back = value;
}
bool fixture_legacy{}, fixture_aim{}, fixture_recenter{}, fixture_snap{}, fixture_stick{};
void TestInputStickScenario(bool value) {
    fixture_stick = value;
}
void TestInputSnapScenario(bool value) {
    fixture_snap = value;
}
void TestInputRecenter(bool value) {
    fixture_recenter = value;
}
void TestInputAimScenario(bool value) {
    fixture_aim = value;
}
extern "C" __declspec(dllexport) void TestInputSetLegacy(bool value) {
    fixture_legacy = value;
}
int fixture_override{};
void TestInputOverride(int mode) {
    fixture_override = mode;
}
unsigned TestInputPhase() {
    return fixture_override == 1   ? 10
           : fixture_override == 2 ? 0
           : fixture_override == 3 ? 90
                                   : input_frame % 240;
}
unsigned phase() {
    return TestInputPhase();
}
bool neutral() {
    const auto p = phase();
    return p < 10 || (p >= 60 && p < 70) || (p >= 110 && p < 120) || (p >= 140 && p < 150) ||
           (p >= 170 && p < 180) || (p >= 200 && p < 210) || (p >= 220 && p < 230);
}
std::uint32_t get_role(void*, int role) {
    return role == 1 ? (phase() >= 210 ? 5 : 2) : ((fixture_aim || fixture_snap) && phase() >= 210 ? 3 : 1);
}
bool connected(void*, std::uint32_t device) {
    return device == 0 || !(phase() >= 150 && phase() < 160);
}
int get_property(void*, std::uint32_t, int property, int* error) {
    *error = fixture_legacy && property >= 3005 ? 4 : 0;
    return property == 3002 ? 1 : property == 3003 ? 3 : property == 3004 ? (fixture_legacy ? 3 : 2) : 0;
}
bool get_state(void*, std::uint32_t device, ControllerState* state) {
    *state = {};
    state->packet = input_frame;
    state->pressed = 1ull << 33;
    state->touched = 1ull << 32;
    if (fixture_aim && neutral())
        state->pressed = 0;
    if ((device == 2 || device == 5) ? fixture_menu : fixture_back)
        state->pressed |= 1ull << 1;
    if (fixture_recenter && (device == 1 || device == 3))
        state->pressed |= 1ull << (fixture_legacy ? 2 : 7);
    if (!fixture_legacy)
        state->axes[0] = {.9f, .9f}; // Trackpad decoy must never drive normal movement.
    if (device != 1 && !neutral())
        state->axes[fixture_legacy ? 0 : 2] =
            phase() >= 70 && phase() < 90 ? Axis{1, 0} : Axis{0, phase() >= 230 ? .6f : 1.f};
    if ((fixture_snap || fixture_stick) && (device == 1 || device == 3)) {
        state->axes[fixture_legacy ? 0 : 2] =
            neutral() ? Axis{} : Axis{phase() >= 70 && phase() < 110 ? -1.f : 1.f, 0};
        if (!neutral())
            state->pressed |= 1ull << 32; // Observed legacy stick deflection aliases pad-click.
    }
    if (fixture_navigation >= 0) {
        auto& axis = state->axes[fixture_legacy ? 0 : 2];
        axis = {};
        if ((fixture_navigation_right && (device == 1 || device == 3)) ||
            (!fixture_navigation_right && (device == 2 || device == 5))) {
            if (fixture_navigation == 1)
                axis.y = 1;
            if (fixture_navigation == 2)
                axis.y = -1;
            if (fixture_navigation == 3)
                axis.x = -1;
            if (fixture_navigation == 4)
                axis.x = 1;
        }
    }
    return !(phase() >= 180 && phase() < 190);
}
bool captured(void*) {
    return false;
}
void* table[40]{};
struct FakeSystem {
    void** table;
} system_instance{table};
extern "C" {
__declspec(dllexport) void* TestInputSystem = &system_instance;
__declspec(dllexport) void* TestInputReturn{};
}
extern void* compositor_methods[4];
int get_last_poses(void*, TrackedPose*, std::uint32_t, TrackedPose*, std::uint32_t);
void initialize_input_fixture() {
    compositor_methods[3] = reinterpret_cast<void*>(&get_last_poses);
    table[role_slot] = reinterpret_cast<void*>(&get_role);
    table[connected_slot] = reinterpret_cast<void*>(&connected);
    table[property_slot] = reinterpret_cast<void*>(&get_property);
    table[state_slot] = reinterpret_cast<void*>(&get_state);
    table[focus_slot] = reinterpret_cast<void*>(&captured);
}
extern "C" __declspec(dllexport) __declspec(noinline) void TestInputPoll() {
    ++native_polls;
    fixture_polling = true;
    TestInputKey(&fixture_keyboard, 0x136, !neutral());
    TestInputKey(&fixture_keyboard, 0x135, true);
    TestInputKey(&fixture_keyboard, 0x13d, fixture_back);
    fixture_polling = false;
}
extern "C" __declspec(dllexport) __declspec(noinline) Vec3* TestInputMove(Vec3* out) {
    if (!TestInputReturn)
        TestInputReturn = WITNESS_RETURN_ADDRESS();
    *out = {.1f, 0, 0};
    return out;
}
extern "C" __declspec(dllexport) Context TestInputContext() {
    const auto p = phase();
    return {fixture_aim ? (p >= 40 && p < 50   ? 2
                           : p >= 20 && p < 40 ? 1
                                               : 0)
                        : (p >= 40 && p < 50 ? 1 : 2),
            !(p >= 120 && p < 130),
            true,
            false,
            p >= 90 && p < 100 ? 1.f : 0.f,
            {1, 0, 0},
            {0, 1, 0},
            p >= 90 && p < 100};
}

void* compositor_methods[4]{};
struct FakeCompositor {
    void** table;
} compositor_instance{compositor_methods};
extern "C" {
__declspec(dllexport) void* TestInputCompositor = &compositor_instance;
__declspec(dllexport) void* TestInputCursorReturn{};
}
int get_last_poses(void*, TrackedPose* poses, std::uint32_t count, TrackedPose* game,
                   std::uint32_t game_count) {
    if (count != 16 || game || game_count)
        return 1;
    for (unsigned i = 0; i < count; ++i) {
        poses[i] = {};
        for (int a = 0; a < 3; ++a)
            poses[i].matrix[a][a] = 1.f;
        poses[i].connected = true;
        poses[i].valid = true;
        poses[i].tracking_result = 200;
    }
    for (unsigned i : {1u, 3u}) {
        poses[i].matrix[0][0] = std::cos(.2f);
        poses[i].matrix[0][2] = -std::sin(.2f);
        poses[i].matrix[2][0] = std::sin(.2f);
        poses[i].matrix[2][2] = std::cos(.2f);
        if (phase() >= 190 && phase() < 200)
            poses[i].valid = false;
    }
    return 0;
}
extern "C" __declspec(dllexport) __declspec(noinline) Axis* TestInputCursor(Axis* out, bool) {
    if (!TestInputCursorReturn)
        TestInputCursorReturn = WITNESS_RETURN_ADDRESS();
    *out = {.001f, -.002f};
    return out;
}
extern "C" __declspec(dllexport) AimFrame TestInputAimFrame() {
    return {{.5f, .5f}, {1, 1}, 1, {0, 0, 0}, {-1, -1, -2}, {1, 0, 0}, {0, 1, 0}, true};
}

extern "C" {
__declspec(dllexport) float TestInputYaw = 1.25f, TestInputVrYaw = -.5f;
__declspec(dllexport) void* TestInputVrReturn{};
}
float fixture_seen_yaw{}, fixture_seen_vr_yaw{};
unsigned native_vr_updates{};
extern "C" __declspec(dllexport) __declspec(noinline) void TestInputVrUpdate() {
    if (!TestInputVrReturn)
        TestInputVrReturn = WITNESS_RETURN_ADDRESS();
    fixture_seen_yaw = TestInputYaw;
    fixture_seen_vr_yaw = TestInputVrYaw;
    ++native_vr_updates;
}

extern "C" {
__declspec(dllexport) void* TestInputKeyReturn{};
__declspec(dllexport) void* TestInputMenuReturn{};
}
bool fixture_pad_pressed{}, fixture_trigger_pressed{}, fixture_menu_pressed{};
extern "C" __declspec(dllexport) __declspec(noinline) void TestInputKey(void* keyboard, int key,
                                                                        bool pressed) {
    if (keyboard == &fixture_keyboard && !fixture_polling && key == 0x136 && pressed) {
        ++native_back_events;
        if (!puzzle(TestInputContext()) && !TestInputContext().menu)
            ++native_back_leaks;
    }
    if (key >= 0x13f && key <= 0x142 && keyboard == &fixture_keyboard) {
        navigation_held = pressed;
        if (pressed) {
            ++native_navigation[key - 0x13f];
            const auto c = TestInputContext();
            if (!c.menu || !c.focused || !c.tracking)
                ++navigation_leaks;
        }
    }
    if (key == 0x136) {
        if (!TestInputKeyReturn)
            TestInputKeyReturn = WITNESS_RETURN_ADDRESS();
        fixture_pad_pressed = pressed;
    }
    if (key == 0x135)
        fixture_trigger_pressed = pressed;
    if (key == 0x13d) {
        if (!TestInputMenuReturn)
            TestInputMenuReturn = WITNESS_RETURN_ADDRESS();
        fixture_menu_pressed = pressed;
        if (keyboard == &fixture_keyboard && pressed) {
            if (fixture_polling && fixture_expect_swap)
                ++native_menu_leaks;
            if (!fixture_polling)
                ++native_menu_events;
        }
    }
}
