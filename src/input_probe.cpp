#include "protocol.hpp"
#include "session_log.hpp"
#include "build_validation.hpp"
#include "config_path.hpp"
#include "win_util.hpp"
#include "controller_input.hpp"
#include "puzzle_aim.hpp"
#include "snap_turn.hpp"
#include "stick_cursor.hpp"
#include "input_settings.hpp"
#include "controller_buttons.hpp"
#include "menu_navigation.hpp"
#include "caller_address.hpp"
#include <MinHook.h>
#include <bcrypt.h>
#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <limits>
#include <sstream>

namespace {
using witness::validation::match;
using witness::validation::sha256;
using namespace witness::input;
using Poll = void (*)();
using NativeKey = void (*)(void*, int, bool);
NativeKey original_key{};
void* key_site{};
void* key_caller{};
void* menu_caller{};
std::atomic<std::uint64_t> stick_applied{0}, cancel_suppressed{0};
StickCursor stick_cursor;
BButton puzzle_back, menu_button;
MenuStick menu_left, menu_right;
std::atomic<std::uint64_t> back_presses{0};
thread_local bool within_native_poll{};
thread_local void* polled_keyboard{};
using Move = Vec3* (*)(Vec3*);
using GetContext = Context (*)();
using CursorDelta = Axis* (*)(Axis*, bool);
using LastPoses = int (*)(void*, TrackedPose*, std::uint32_t, TrackedPose*, std::uint32_t);
using GetAimFrame = AimFrame (*)();
CursorDelta original_cursor{};
void* cursor_site{};
void* cursor_caller{};
GetAimFrame fixture_aim_frame{};
void** fixture_compositor{};
void* compositor_object{};
void** compositor_table{};
LastPoses last_poses{};
std::atomic<bool> aim_capture{false}, allow_aim{false};
std::atomic<std::uint64_t> cursor_calls{0}, aim_applied{0}, pose_samples{0};
Poll original_poll{}, original_vr_update{};
void* vr_update_site{};
void* vr_update_caller{};
float* yaw_pointer{};
float* vr_yaw_pointer{};
std::atomic<bool> snap_capture{false}, allow_snap{false};
std::atomic<unsigned> snap_steps{0};
std::atomic<std::uint64_t> turn_calls{0}, turn_applied{0}, turn_route_calls{0};
SnapTurn snap_turn;
Move original_move{};
void* poll_site{};
void* move_site{};
void* move_caller{};
GetContext fixture_context{};
void** fixture_system{};
void* system_object{};
void** system_table{};
std::array<void*, 40> methods{};
std::uintptr_t game_base{};
bool created{};
std::atomic<bool> poisoned{false};
std::atomic<witness::SessionState> session_state{witness::SessionState::stopped};
std::atomic<bool> session_stop{false};
SRWLOCK control_lock = SRWLOCK_INIT;
bool session_logging{};
HANDLE session_thread{};
std::atomic<bool> busy{false}, enabled{false}, allow_move{false}, legacy_axis{false}, emergency{false};
std::atomic<unsigned> active{0};
std::atomic<ULONGLONG> deadline{0};
std::atomic<std::uint64_t> polls{0}, movement_calls{0}, applied{0};
SRWLOCK sample_lock = SRWLOCK_INIT;
Movement movement;
Pointing pointing;
AimCalibration calibration;
AimSettings aim_settings;
std::atomic<std::uint32_t> stick_speed_percent{default_stick_speed_percent};
std::atomic<std::uint32_t> motion_speed_percent{60};
std::filesystem::path input_settings_file;
bool refresh_input_settings() {
    const auto read_speed = [&](const wchar_t* key, std::atomic<std::uint32_t>& setting) {
        wchar_t value[64]{};
        const auto size =
            GetPrivateProfileStringW(L"Input", key, L"", value, 64, input_settings_file.c_str());
        std::uint32_t parsed{};
        // Each missing/partial/invalid edit retains that field's last valid value.
        if (size == 0 || size >= 63 || !parse_cursor_speed({value, size}, parsed))
            return false;
        return setting.exchange(parsed) != parsed;
    };
    const bool stick_changed = read_speed(L"StickCursorSpeedPercent", stick_speed_percent);
    const bool motion_changed = read_speed(L"MotionCursorSpeedPercent", motion_speed_percent);
    return stick_changed || motion_changed;
}
std::atomic<std::uint64_t> recenter_count{0};
struct Sample {
    ULONGLONG tick{};
    DWORD thread{};
    Hand hands[2];
    Context context;
    bool captured{}, allowed{};
    Axis movement{};
    TrackedPose head_pose{}, right_pose{};
    bool pose_valid{};
    bool aim_allowed{}, stick_active{};
    Axis aim_target{}, aim_delta{};
    int snap_direction{};
    bool turn_ready{}, turn_allowed{};
    float yaw_before{}, yaw_after{}, vr_yaw_before{}, vr_yaw_after{};
} latest;
struct Active {
    Active() { ++active; }
    ~Active() { --active; }
};
struct Lock {
    Lock() { AcquireSRWLockExclusive(&sample_lock); }
    ~Lock() { ReleaseSRWLockExclusive(&sample_lock); }
};

template <class T> bool read(std::uintptr_t address, T& value) {
    SIZE_T size{};
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), &value, sizeof(value),
                             &size) &&
           size == sizeof(value);
}
template <class T> T global(std::uintptr_t rva) {
    T v{};
    read(game_base + rva, v);
    return v;
}
bool foreground() {
    DWORD pid{};
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}
Context context() {
    if (fixture_context)
        return fixture_context();
    Context c{};
    c.mode = global<int>(0x62D5C4);
    c.fade = global<float>(0x61E994);
    c.forward = global<Vec3>(0x61FF98);
    c.left = global<Vec3>(0x61FFA8);
    c.tracking = global<unsigned char>(0x469A55C) != 0;
    const auto renderer = global<std::uintptr_t>(0x469A5B0);
    unsigned char focus{}, suppressed{1};
    if (renderer && read(renderer + 0x21, focus) && read(renderer + 0x2B, suppressed)) {
        c.focused = focus && foreground();
        c.suppressed = suppressed != 0;
    } else
        c.suppressed = true;
    c.menu = std::isfinite(c.fade) && c.fade >= 1.f && global<unsigned char>(0x61E99C) == 0 &&
             global<int>(0x630010) >= 4 && global<std::uintptr_t>(0x6300E0) != 0;
    return c;
}
void* current_system() {
    return fixture_system ? *fixture_system : global<void*>(0x469AB38);
}

template <class Function> Function method(unsigned slot) {
    return reinterpret_cast<Function>(methods[slot]);
}
Hand hand(int role) {
    Hand h;
    h.device = method<std::uint32_t (*)(void*, int)>(role_slot)(system_object, role);
    if (h.device >= 16 || !method<bool (*)(void*, std::uint32_t)>(connected_slot)(system_object, h.device))
        return h;
    // IVRSystem_012 has no controller-state size argument.
    if (!method<bool (*)(void*, std::uint32_t, ControllerState*)>(state_slot)(system_object, h.device,
                                                                              &h.state))
        return h;
    for (int a = 0; a < 5; ++a) {
        int error = -1;
        h.types[a] = method<int (*)(void*, std::uint32_t, int, int*)>(property_slot)(system_object, h.device,
                                                                                     3002 + a, &error);
        if (error) {
            h.types[a] = -1;
            continue;
        }
    }
    select_axis(h, legacy_axis.load());
    h.valid = true;
    return h;
}
void* current_compositor() {
    return fixture_compositor ? *fixture_compositor : global<void*>(0x469AB40);
}
AimFrame aim_frame() {
    if (fixture_aim_frame)
        return fixture_aim_frame();
    AimFrame f;
    const auto cursor = global<std::uintptr_t>(0x62D4D0);
    if (!cursor)
        return f;
    f.valid = read(cursor + 0x18, f.cursor) && read(cursor + 0xCC, f.bounds) &&
              read(cursor + 0x94, f.scale) && read(cursor + 0x9C, f.origin) && read(cursor + 0xA8, f.plane) &&
              read(cursor + 0xB4, f.u) && read(cursor + 0xC0, f.v) &&
              cursor == global<std::uintptr_t>(0x62D4D0);
    return f;
}
void hooked_poll() {
    Active guard;
    polled_keyboard = nullptr;
    within_native_poll = true;
    original_poll();
    within_native_poll = false;
    if (!enabled.load())
        return;
    Sample s;
    s.tick = GetTickCount64();
    s.thread = GetCurrentThreadId();
    s.context = context();
    void** table{};
    const bool live_system = current_system() == system_object &&
                             read(reinterpret_cast<std::uintptr_t>(system_object), table) &&
                             table == system_table;
    if (live_system) {
        s.captured = method<bool (*)(void*)>(focus_slot)(system_object);
        s.context.tracking =
            s.context.tracking && method<bool (*)(void*, std::uint32_t)>(connected_slot)(system_object, 0);
        s.hands[0] = hand(1);
        s.hands[1] = hand(2);
        if (s.hands[0].device == s.hands[1].device) {
            s.hands[0].valid = false;
            s.hands[1].valid = false;
        }
    }
    if (aim_capture.load() && live_system && s.hands[1].valid && !s.captured) {
        void** table{};
        if (current_compositor() == compositor_object &&
            read(reinterpret_cast<std::uintptr_t>(compositor_object), table) && table == compositor_table) {
            std::array<TrackedPose, 16> poses{};
            if (last_poses(compositor_object, poses.data(), 16, nullptr, 0) == 0) {
                s.head_pose = poses[0];
                s.right_pose = poses[s.hands[1].device];
                s.pose_valid = valid_pose(s.head_pose) && valid_pose(s.right_pose);
                if (s.pose_valid)
                    ++pose_samples;
            }
        }
    }
    if (!fixture_context && s.context.focused && (GetAsyncKeyState(VK_F9) & 0x8000))
        emergency.store(true);
    bool request_back = false, request_menu = false;
    MenuDirection navigation{};
    s.allowed =
        live_system && walking(s.context) && !s.captured && !emergency.load() && s.tick < deadline.load();
    {
        Lock lock;
        if (latest.thread != s.thread || s.tick - latest.tick > 100) {
            puzzle_back.reset();
            menu_button.reset();
            menu_left.reset();
            menu_right.reset();
            stick_cursor.reset();
            movement.reset();
            pointing.reset();
            calibration.disarm();
            snap_turn.reset();
        }
        // Reset cursor latches here too: menus can skip the cursor callback.
        if (!puzzle(s.context) || s.captured || !s.pose_valid || !s.hands[1].valid || !allow_aim.load() ||
            emergency.load()) {
            pointing.reset();
            stick_cursor.reset();
            calibration.disarm();
        }
        request_back = puzzle_back.update(
            s.hands[1],
            live_system &&
                (puzzle(s.context) || (s.context.menu && s.context.focused && s.context.tracking)) &&
                !s.captured && allow_aim.load() && !emergency.load() && s.tick < deadline.load() &&
                polled_keyboard);
        // The pause toggle remains available inside menus, with focus/tracking gates.
        request_menu =
            menu_button.update(s.hands[0], live_system && s.context.focused && s.context.tracking &&
                                               !s.captured && allow_aim.load() && !emergency.load() &&
                                               s.tick < deadline.load() && polled_keyboard);
        const bool menu_allowed = live_system && s.context.menu && s.context.focused && s.context.tracking &&
                                  !s.captured && allow_aim.load() && !emergency.load() &&
                                  s.tick < deadline.load() && polled_keyboard;
        const auto left = menu_left.update(s.hands[0], menu_allowed, s.tick);
        const auto right = menu_right.update(s.hands[1], menu_allowed, s.tick);
        navigation = left != MenuDirection::none ? left : right;
        s.snap_direction = snap_turn.update(s.hands[1], s.allowed && allow_snap.load(), s.tick);
        s.allowed = s.allowed && allow_move.load();
        s.movement = movement.update(s.hands[0], s.allowed);
        latest = s;
    }
    ++polls;
    if (navigation != MenuDirection::none && !request_menu && !request_back && enabled.load() &&
        allow_aim.load() && !emergency.load() && GetTickCount64() < deadline.load()) {
        const auto c = context();
        if (c.menu && c.focused && c.tracking) {
            // Queue a complete native press/release pair; no held key can outlive this callback.
            const auto key = static_cast<int>(navigation);
            original_key(polled_keyboard, key, true);
            original_key(polled_keyboard, key, false);
        }
    }
    if (request_menu && enabled.load() && allow_aim.load() && !emergency.load() &&
        GetTickCount64() < deadline.load()) {
        const auto c = context();
        if (c.focused && c.tracking)
            original_key(polled_keyboard, 0x13d, true);
    }
    if (request_back && enabled.load() && allow_aim.load() && !emergency.load() &&
        GetTickCount64() < deadline.load()) {
        const auto c = context();
        if (!(puzzle(c) || (c.menu && c.focused && c.tracking)))
            return;
        // Send one native back press; the next VR poll restores the physical pad state.
        original_key(polled_keyboard, 0x136, true);
        ++back_presses;
    }
}
Vec3* hooked_move(Vec3* output) {
    const auto caller = WITNESS_RETURN_ADDRESS();
    Active guard;
    auto* result = original_move(output);
    ++movement_calls;
    if (!enabled.load() || !allow_move.load() || emergency.load() || caller != move_caller ||
        result != output || !result)
        return result;
    const auto now = GetTickCount64();
    Lock lock;
    const auto c = context();
    if (!enabled.load() || !allow_move.load() || !latest.allowed || !walking(c) ||
        latest.thread != GetCurrentThreadId() || now - latest.tick > 50 || now >= deadline.load()) {
        movement.reset();
        latest.movement = {};
        return result;
    }
    if (blend(*result, latest.movement, c))
        ++applied;
    return result;
}
Axis* hooked_cursor(Axis* output, bool native_mode) {
    const auto caller = WITNESS_RETURN_ADDRESS();
    Active guard;
    auto* result = original_cursor(output, native_mode);
    ++cursor_calls;
    if (!enabled.load() || !aim_capture.load() || caller != cursor_caller || result != output || !result)
        return result;
    Lock lock;
    const auto now = GetTickCount64();
    const auto c = context();
    const auto f = aim_frame();
    const bool allowed = enabled.load() && allow_aim.load() && !emergency.load() && now < deadline.load() &&
                         latest.thread == GetCurrentThreadId() && now - latest.tick <= 50 && puzzle(c) &&
                         puzzle(latest.context) && !latest.captured && latest.hands[1].valid &&
                         latest.pose_valid;
    Axis target{}, delta{};
    const bool trigger = (latest.hands[1].state.pressed & (1ull << 33)) != 0;
    const bool recenter = recenter_pressed(latest.hands[1]);
    const bool recentered = calibration.update(latest.hands[1].device, allowed, recenter,
                                               trigger || c.mode != 0, latest.head_pose, latest.right_pose);
    if (recentered) {
        pointing.reset();
        ++recenter_count;
    }
    const bool manual_allowed = enabled.load() && allow_aim.load() && !emergency.load() &&
                                now < deadline.load() && latest.thread == GetCurrentThreadId() &&
                                now - latest.tick <= 50 && puzzle(c) && puzzle(latest.context) &&
                                !latest.captured && f.valid;
    const bool manual = stick_cursor.update(latest.hands[1], manual_allowed, recentered, now, f.bounds.x,
                                            stick_speed_percent.load() / 100.f, delta);
    latest.stick_active = manual;
    if (manual) {
        pointing.reset();
        latest.aim_allowed = true;
        latest.aim_delta = delta;
        // Retain real mouse input when the stick rests, with pointing suspended.
        if (delta.x || delta.y) {
            *result = delta;
            ++stick_applied;
        }
        return result;
    }
    const bool projected = aim_target(latest.head_pose, latest.right_pose, f, target, calibration);
    const bool apply =
        pointing.update(latest.hands[1].device, allowed && projected, trigger, target, f.cursor, delta, now,
                        f.bounds.x, {motion_speed_percent.load() / 100.f, aim_settings.smoothing_ms});
    latest.aim_allowed = apply;
    latest.aim_target = target;
    latest.aim_delta = delta;
    if (apply) {
        *result = delta;
        ++aim_applied;
    }
    return result;
}
void hooked_key(void* keyboard, int key, bool pressed) {
    const auto caller = WITNESS_RETURN_ADDRESS();
    Active guard;
    if (within_native_poll && caller == key_caller && key == 0x136)
        polled_keyboard = keyboard;
    if (caller == key_caller && key == 0x136 && pressed && enabled.load() && allow_aim.load() &&
        !emergency.load() && GetTickCount64() < deadline.load()) {
        const auto c = context();
        Lock lock;
        // Suppress only the legacy VR pad-cancel route that also fires on stick deflection.
        if ((puzzle(c) || (c.menu && c.focused && c.tracking)) && latest.hands[1].valid &&
            latest.hands[1].legacy_axis && !latest.captured && latest.thread == GetCurrentThreadId() &&
            GetTickCount64() - latest.tick <= 50) {
            pressed = false;
            ++cancel_suppressed;
        }
    }
    // Consume only the verified native controller menu route; left B supplies its edge.
    if (within_native_poll && caller == menu_caller && key == 0x13d && enabled.load() && allow_aim.load() &&
        !emergency.load() && GetTickCount64() < deadline.load())
        pressed = false;
    original_key(keyboard, key, pressed);
}
void hooked_vr_update() {
    const auto caller = WITNESS_RETURN_ADDRESS();
    Active guard;
    ++turn_calls;
    if (enabled.load() && snap_capture.load() && caller == vr_update_caller) {
        Lock lock;
        const auto now = GetTickCount64();
        const auto c = context();
        const auto renderer = fixture_context ? 0 : global<std::uintptr_t>(0x469A5B0);
        unsigned char native_vr{};
        const bool vr_active =
            fixture_context || (renderer && read(renderer + 0x25, native_vr) && native_vr == 1);
        ++turn_route_calls;
        const bool ready = enabled.load() && !emergency.load() && now < deadline.load() &&
                           latest.thread == GetCurrentThreadId() && now - latest.tick <= 50 && walking(c) &&
                           walking(latest.context) && !latest.captured && latest.hands[1].valid && vr_active;
        latest.turn_ready = ready;
        const bool allowed = ready && allow_snap.load();
        const int direction = latest.snap_direction;
        latest.snap_direction = 0;
        latest.turn_allowed = allowed;
        float yaw{}, vr_yaw{}, next_yaw{}, next_vr_yaw{};
        const bool readable = read(reinterpret_cast<std::uintptr_t>(yaw_pointer), yaw) &&
                              read(reinterpret_cast<std::uintptr_t>(vr_yaw_pointer), vr_yaw);
        latest.yaw_before = latest.yaw_after = yaw;
        latest.vr_yaw_before = latest.vr_yaw_after = vr_yaw;
        if (!allowed)
            snap_turn.reset();
        if (allowed && readable &&
            snap_headings(yaw, vr_yaw, direction, snap_steps.load(), next_yaw, next_vr_yaw)) {
            // Update both verified heading fields before the native VR update consumes them.
            *yaw_pointer = next_yaw;
            *vr_yaw_pointer = next_vr_yaw;
            latest.yaw_after = next_yaw;
            latest.vr_yaw_after = next_vr_yaw;
            ++turn_applied;
        }
    }
    original_vr_update();
}
void require(MH_STATUS status) {
    if (status != MH_OK)
        throw std::runtime_error(MH_StatusToString(status));
}
void validate() {
    HMODULE self{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&validate), &self))
        throw witness::win_error("input module");
    const auto exe = std::filesystem::path(witness::module_path());
    const auto own = std::filesystem::path(witness::module_path(self)).parent_path();
    const auto main = GetModuleHandleW(nullptr);
    if (witness::same_path(exe, own / L"witness-input-test-host.exe")) {
        input_settings_file =
            own / (L"input-settings-test-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
        poll_site = reinterpret_cast<void*>(GetProcAddress(main, "TestInputPoll"));
        move_site = reinterpret_cast<void*>(GetProcAddress(main, "TestInputMove"));
        fixture_context =
            reinterpret_cast<GetContext>(reinterpret_cast<void*>(GetProcAddress(main, "TestInputContext")));
        fixture_system = reinterpret_cast<void**>(GetProcAddress(main, "TestInputSystem"));
        cursor_site = reinterpret_cast<void*>(GetProcAddress(main, "TestInputCursor"));
        fixture_aim_frame =
            reinterpret_cast<GetAimFrame>(reinterpret_cast<void*>(GetProcAddress(main, "TestInputAimFrame")));
        fixture_compositor = reinterpret_cast<void**>(GetProcAddress(main, "TestInputCompositor"));
        const auto cursor_return_ptr =
            reinterpret_cast<void**>(GetProcAddress(main, "TestInputCursorReturn"));
        cursor_caller = cursor_return_ptr ? *cursor_return_ptr : nullptr;
        const auto return_ptr = reinterpret_cast<void**>(GetProcAddress(main, "TestInputReturn"));
        move_caller = return_ptr ? *return_ptr : nullptr;
        vr_update_site = reinterpret_cast<void*>(GetProcAddress(main, "TestInputVrUpdate"));
        const auto vr_return = reinterpret_cast<void**>(GetProcAddress(main, "TestInputVrReturn"));
        vr_update_caller = vr_return ? *vr_return : nullptr;
        yaw_pointer = reinterpret_cast<float*>(GetProcAddress(main, "TestInputYaw"));
        vr_yaw_pointer = reinterpret_cast<float*>(GetProcAddress(main, "TestInputVrYaw"));
        key_site = reinterpret_cast<void*>(GetProcAddress(main, "TestInputKey"));
        const auto key_return = reinterpret_cast<void**>(GetProcAddress(main, "TestInputKeyReturn"));
        key_caller = key_return ? *key_return : nullptr;
        const auto menu_return = reinterpret_cast<void**>(GetProcAddress(main, "TestInputMenuReturn"));
        menu_caller = menu_return ? *menu_return : nullptr;
        if (aim_capture.load() && (!key_site || !key_caller || !menu_caller))
            throw std::runtime_error("Native key fixture exports missing");
        if (snap_capture.load() && (!vr_update_site || !vr_update_caller || !yaw_pointer || !vr_yaw_pointer))
            throw std::runtime_error("Snap fixture exports missing");
        if (!poll_site || !move_site || !fixture_context || !fixture_system || !move_caller || !cursor_site ||
            !fixture_aim_frame || !fixture_compositor || !cursor_caller)
            throw std::runtime_error("Input fixture exports missing");
    } else {
        input_settings_file = witness::input_config_path(own);
        if (_wcsicmp(exe.filename().c_str(), L"witness64_d3d11.exe") ||
            sha256(exe) != "8d672d444df6a6df7130f25a517bdab1af92731fe2138dc5b324d519561d55a5")
            throw std::runtime_error("Unsupported game hash");
        game_base = reinterpret_cast<std::uintptr_t>(main);
        bool correct_size = false;
        for (const auto& m : witness::modules(GetCurrentProcessId()))
            if (m.base == game_base && m.size == 74457088)
                correct_size = true;
        if (!correct_size)
            throw std::runtime_error("Unsupported image size");
        match(game_base, 0x242230, "48895c24184889742420574881ec80000000488b05177e4504");
        match(game_base, 0x24226e, "488b053b834504488bd9403870210f842e03");
        match(
            game_base, 0x242396,
            "f30f100502dc3d00f30f102df2db3d00f30f102502dc3d00488bcbf3410f59c0f3410f59e8f30f59e6f30f1015e1db3d00f30f101ddddb3d00f30f100dc5db3d00");
        match(game_base, 0x24a509, "e8227dffff");
        match(game_base, 0x24960e, "833daf3f3e0001488b9c2420010000");
        match(game_base, 0x37aa00, "405355415441564881ecd8050000488b0d2b0132044885c97436488b0145");
        match(game_base, 0x37aa80,
              "488b0db10032048bd5488b01ff909800000083f80175714585e40f85b80100000f10030f104b104c");
        match(game_base, 0x37ab6a, "ff90000100004585f60f85dc");
        match(game_base, 0x1fc40a, "f30f100582254200");
        if (aim_capture.load()) {
            match(game_base, 0x1d0e10, "40555657488d6c24b94881eca0000000");
            match(game_base, 0x1d46d8, "e833c7ffff488b00");
            match(game_base, 0x1ccaf0, "40534883ec60488b05d3094600");
            match(game_base, 0x1d4718, "f30f1096cc0000000f2fca");
            match(game_base, 0x1d4730, "f30f1096d00000000f2fc2");
            match(game_base, 0x364210,
                  "40574883ec204863fa488bc74c8d0cb98b4cb908f6c1017409f6c1047504b001eb0232c0");
            match(game_base, 0x37ac36, "488b0523f43104440fb6c6488b08ba36010000488b8990000000e8bb95feff");
            match(game_base, 0x37abed,
                  "ba3d010000f30f114f28488b442438488b442440488b0558f43104488b08488b8990000000e8f995feff");
            match(
                game_base, 0x1F9A83,
                "ba40010000488d05956543004889442440488d05856543008d4aff4889442438448d4a04448d4203f30f11442430895c2428895c2420e8b2550000");
            match(
                game_base, 0x1FB327,
                "ba42010000488d05f94c43004889442440488d05e94c43008d4aff4889442438448d4a04448d4203f30f1144243044897c242844897c2420e80c3d0000");
            match(game_base, 0x1FC852,
                  "803d43214200007559f30f1005312142000f2f05666c31007233833d9d374300047c2a488b0564384300");
            menu_caller = reinterpret_cast<void*>(game_base + 0x37AC17);
            key_site = reinterpret_cast<void*>(game_base + 0x364210);
            key_caller = reinterpret_cast<void*>(game_base + 0x37AC55);
            cursor_site = reinterpret_cast<void*>(game_base + 0x1D0E10);
            cursor_caller = reinterpret_cast<void*>(game_base + 0x1D46DD);
        }
        if (snap_capture.load()) {
            match(game_base, 0x23e540,
                  "4881eca8000000488b0562c0450480782500750fc605391f3f00004881c4a8000000c3");
            match(game_base, 0x24a462, "e8d940ffff");
            match(game_base, 0x23e571, "f30f1015a71e3f00");
            match(game_base, 0x240f36, "f30f1005e2f43e00");
            match(
                game_base, 0x249745,
                "f30f10159f6c3e00f30f101dcb6c3e00f30f1065500f28ce0f28c7f30f59e7f30f5cd4f30f5cdcf30f1115786c3e00f30f105558f30f111d9f6c3e00f30f59d6f30f101d636c3e00f30f5cdaf30f111d576c3e00");
            vr_update_site = reinterpret_cast<void*>(game_base + 0x23E540);
            vr_update_caller = reinterpret_cast<void*>(game_base + 0x24A467);
            yaw_pointer = reinterpret_cast<float*>(game_base + 0x6303EC);
            vr_yaw_pointer = reinterpret_cast<float*>(game_base + 0x630420);
        }
        poll_site = reinterpret_cast<void*>(game_base + 0x37AA00);
        move_site = reinterpret_cast<void*>(game_base + 0x242230);
        move_caller = reinterpret_cast<void*>(game_base + 0x24A50E);
    }
    if (snap_capture.load())
        for (auto* pointer : {yaw_pointer, vr_yaw_pointer}) {
            MEMORY_BASIC_INFORMATION info{};
            if (!VirtualQuery(pointer, &info, sizeof(info)) || info.AllocationBase != main ||
                info.State != MEM_COMMIT || info.Type != MEM_IMAGE || info.Protect != PAGE_READWRITE ||
                reinterpret_cast<std::uintptr_t>(pointer) + sizeof(float) >
                    reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize)
                throw std::runtime_error("Heading storage is not writable game-image data");
        }
    system_object = current_system();
    if (!system_object || !read(reinterpret_cast<std::uintptr_t>(system_object), system_table) ||
        !read(reinterpret_cast<std::uintptr_t>(system_table), methods))
        throw std::runtime_error("No readable native VR system");
    const auto verify_method = [&](void* address) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
            info.Type != MEM_IMAGE ||
            (info.Protect &
             (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0)
            throw std::runtime_error("Invalid VR method address");
        const auto owner =
            std::filesystem::path(witness::module_path(static_cast<HMODULE>(info.AllocationBase)));
        if (fixture_context ? info.AllocationBase != main
                            : (_wcsicmp(owner.filename().c_str(), L"vrclient_x64.dll") &&
                               _wcsicmp(owner.filename().c_str(), L"openvr_api.dll")))
            throw std::runtime_error("Unexpected VR method owner");
    };
    for (const auto slot : {role_slot, connected_slot, property_slot, state_slot, focus_slot})
        verify_method(methods[slot]);
    if (aim_capture.load()) {
        compositor_object = current_compositor();
        void* address{};
        if (!compositor_object ||
            !read(reinterpret_cast<std::uintptr_t>(compositor_object), compositor_table) ||
            !read(reinterpret_cast<std::uintptr_t>(compositor_table) + 3 * sizeof(void*), address))
            throw std::runtime_error("No readable native compositor");
        verify_method(address);
        last_poses = reinterpret_cast<LastPoses>(address);
    }
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&validate), &self))
        throw witness::win_error("pin input DLL");
}
void set_controls(bool value) {
    Lock lock;
    puzzle_back.reset();
    menu_button.reset();
    menu_left.reset();
    menu_right.reset();
    movement.reset();
    latest.movement = {};
    latest.allowed = false;
    pointing.reset();
    stick_cursor.reset();
    calibration.reset();
    latest.aim_allowed = false;
    latest.aim_delta = {};
    snap_turn.reset();
    latest.snap_direction = 0;
    latest.turn_allowed = false;
    allow_move.store(value);
    allow_aim.store(value && aim_capture.load());
    allow_snap.store(value && snap_steps.load() != 0);
}
void prepare(bool moving, bool legacy, ULONGLONG until, bool aiming = false, bool aim_trace = false,
             const AimSettings& settings = AimSettings{}, unsigned turning_steps = 0,
             bool turn_trace = false) {
    if (turning_steps && !valid_snap_steps(turning_steps))
        throw std::runtime_error("Invalid snap angle");
    snap_steps = turning_steps;
    snap_capture = turning_steps != 0 || turn_trace;
    allow_snap = turning_steps != 0;
    if (!valid_settings(settings))
        throw std::runtime_error("Invalid pointing settings");
    aim_capture = aiming || aim_trace;
    allow_aim = aiming;
    if (poisoned.load())
        throw std::runtime_error("Input instance failed previously; restart game");
    validate();
    stick_speed_percent = default_stick_speed_percent;
    motion_speed_percent = static_cast<std::uint32_t>(std::lround(settings.speed * 100));
    refresh_input_settings();
    if (!created) {
        require(MH_Initialize());
        require(MH_CreateHook(poll_site, reinterpret_cast<void*>(&hooked_poll),
                              reinterpret_cast<void**>(&original_poll)));
        require(MH_CreateHook(move_site, reinterpret_cast<void*>(&hooked_move),
                              reinterpret_cast<void**>(&original_move)));
        created = true;
    }
    if (aim_capture.load() && !original_cursor)
        require(MH_CreateHook(cursor_site, reinterpret_cast<void*>(&hooked_cursor),
                              reinterpret_cast<void**>(&original_cursor)));
    if (aim_capture.load() && !original_key)
        require(MH_CreateHook(key_site, reinterpret_cast<void*>(&hooked_key),
                              reinterpret_cast<void**>(&original_key)));
    if (snap_capture.load() && !original_vr_update)
        require(MH_CreateHook(vr_update_site, reinterpret_cast<void*>(&hooked_vr_update),
                              reinterpret_cast<void**>(&original_vr_update)));
    {
        Lock lock;
        latest = {};
        puzzle_back.reset();
        menu_button.reset();
        menu_left.reset();
        menu_right.reset();
        stick_cursor.reset();
        snap_turn.reset();
        movement.reset();
        pointing.reset();
        calibration.reset();
        aim_settings = settings;
    }
    back_presses = 0;
    stick_applied = 0;
    cancel_suppressed = 0;
    recenter_count = 0;
    turn_calls = 0;
    turn_applied = 0;
    turn_route_calls = 0;
    cursor_calls = 0;
    aim_applied = 0;
    pose_samples = 0;
    polls = 0;
    movement_calls = 0;
    applied = 0;
    emergency = false;
    allow_move = moving;
    legacy_axis = legacy;
    deadline = until;
    // Originals are ready before either detour can execute.
    require(MH_EnableHook(poll_site));
    require(MH_EnableHook(move_site));
    if (aim_capture.load()) {
        require(MH_EnableHook(cursor_site));
        require(MH_EnableHook(key_site));
    }
    if (snap_capture.load())
        require(MH_EnableHook(vr_update_site));
    enabled = true;
}
void stop() {
    enabled.store(false);
    allow_aim = false;
    {
        Lock lock;
        pointing.reset();
    }
    set_controls(false);
    if (!created)
        return;
    if (original_key) {
        const auto r = MH_DisableHook(key_site);
        if (r != MH_OK && r != MH_ERROR_DISABLED)
            require(r);
    }
    if (original_vr_update) {
        const auto r = MH_DisableHook(vr_update_site);
        if (r != MH_OK && r != MH_ERROR_DISABLED)
            require(r);
    }
    if (original_cursor) {
        const auto r = MH_DisableHook(cursor_site);
        if (r != MH_OK && r != MH_ERROR_DISABLED)
            require(r);
    }
    auto r = MH_DisableHook(move_site);
    if (r != MH_OK && r != MH_ERROR_DISABLED)
        require(r);
    r = MH_DisableHook(poll_site);
    if (r != MH_OK && r != MH_ERROR_DISABLED)
        require(r);
    const auto end = GetTickCount64() + 3000;
    while (active.load() && GetTickCount64() < end)
        Sleep(1);
    if (active.load())
        throw std::runtime_error("Input callback did not drain; restart game");
    // Retain pinned code and trampolines even after callbacks drain.
}
void number(std::ostream& out, float f) {
    if (std::isfinite(f))
        out << f;
    else
        out << "null";
}
void emit(std::ostream& out) {
    Sample s;
    {
        Lock lock;
        s = latest;
    }
    out << "{\"event\":\"input.sample\",\"tick\":" << s.tick << ",\"mode\":" << s.context.mode
        << ",\"focused\":" << s.context.focused << ",\"tracking\":" << s.context.tracking << ",\"fade\":";
    number(out, s.context.fade);
    out << ",\"captured\":" << s.captured << ",\"allowed\":" << s.allowed << ",\"hands\":[";
    for (int h = 0; h < 2; ++h) {
        if (h)
            out << ',';
        const auto& hand = s.hands[h];
        out << "{\"role\":" << h + 1 << ",\"device\":" << hand.device << ",\"valid\":" << hand.valid
            << ",\"legacy_axis\":" << hand.legacy_axis << ",\"stick\":" << hand.stick
            << ",\"pressed\":" << hand.state.pressed << ",\"touched\":" << hand.state.touched
            << ",\"types\":[";
        for (int a = 0; a < 5; ++a) {
            if (a)
                out << ',';
            out << hand.types[a];
        }
        out << "],\"axes\":[";
        for (int a = 0; a < 5; ++a) {
            if (a)
                out << ',';
            out << '[';
            number(out, hand.state.axes[a].x);
            out << ',';
            number(out, hand.state.axes[a].y);
            out << ']';
        }
        out << "]}";
    }
    out << "],\"movement\":[";
    number(out, s.movement.x);
    out << ',';
    number(out, s.movement.y);
    out << "],\"pose_valid\":" << s.pose_valid << ",\"stick_active\":" << s.stick_active
        << ",\"stick_applied\":" << stick_applied.load() << ",\"puzzle_back_presses\":" << back_presses.load()
        << ",\"cancel_suppressed\":" << cancel_suppressed.load() << ",\"aim_allowed\":" << s.aim_allowed
        << ",\"aim_target\":[";
    number(out, s.aim_target.x);
    out << ',';
    number(out, s.aim_target.y);
    out << "],\"aim_applied\":" << aim_applied.load() << ",\"cursor_calls\":" << cursor_calls.load()
        << ",\"applied\":" << applied.load() << ",\"movement_calls\":" << movement_calls.load()
        << ",\"turn_calls\":" << turn_calls.load() << ",\"turn_applied\":" << turn_applied.load()
        << ",\"turn_route_calls\":" << turn_route_calls.load() << ",\"turn_ready\":" << s.turn_ready
        << ",\"turn_allowed\":" << s.turn_allowed << ",\"yaw_before\":";
    number(out, s.yaw_before);
    out << ",\"yaw_after\":";
    number(out, s.yaw_after);
    out << ",\"vr_yaw_before\":";
    number(out, s.vr_yaw_before);
    out << ",\"vr_yaw_after\":";
    number(out, s.vr_yaw_after);
    out << "}\n";
}

void session_record(std::ofstream& target, const char* event) {
    if (!target.is_open() || !target)
        return;
    std::ostringstream log;
    log << "{\"event\":\"" << event << "\",\"tick\":" << GetTickCount64()
        << ",\"enabled\":" << (enabled.load() && allow_move.load())
        << ",\"legacy_axis0\":" << legacy_axis.load() << ",\"polls\":" << polls.load()
        << ",\"movement_calls\":" << movement_calls.load() << ",\"applied\":" << applied.load()
        << ",\"pointing\":" << aim_capture.load()
        << ",\"aim_enabled\":" << (enabled.load() && allow_aim.load())
        << ",\"cursor_calls\":" << cursor_calls.load() << ",\"aim_applied\":" << aim_applied.load()
        << ",\"stick_applied\":" << stick_applied.load() << ",\"puzzle_back_presses\":" << back_presses.load()
        << ",\"cancel_suppressed\":" << cancel_suppressed.load()
        << ",\"recenter_count\":" << recenter_count.load()
        << ",\"stick_cursor_speed_percent\":" << stick_speed_percent.load()
        << ",\"aim_speed\":" << motion_speed_percent.load() / 100.f
        << ",\"aim_smoothing_ms\":" << aim_settings.smoothing_ms << ",\"snap_steps\":" << snap_steps.load()
        << ",\"snap_enabled\":" << (enabled.load() && allow_snap.load())
        << ",\"turn_calls\":" << turn_calls.load() << ",\"turn_applied\":" << turn_applied.load() << "}\n";

    if (!log)
        throw std::runtime_error("Input session log write failed");
    witness::write_session_log(target, log.str());
}
std::atomic<bool> observing{false};
DWORD observe_session(const witness::ProbeRequest& req) {
    if (observing.exchange(true))
        return 3;
    struct Finish {
        ~Finish() { observing = false; }
    } finish;
    // Observe copied samples without changing hooks, settings or worker ownership.
    std::ofstream log(std::filesystem::path(req.log_path), std::ios::out | std::ios::trunc);
    if (!log)
        return 2;
    const auto started = GetTickCount64(), until = started + req.duration_ms;
    log << "{\"event\":\"input.observation.started\",\"tick\":" << started
        << ",\"session_unchanged\":true,\"legacy_axis0\":" << legacy_axis.load() << "}\n";
    while (GetTickCount64() < until && session_state.load() == witness::SessionState::running &&
           !session_stop.load()) {
        emit(log);
        if (!log)
            return 4;
        Sleep(20);
    }
    log << "{\"event\":\"input.observation.finished\",\"session_unchanged\":true,\"session_running\":"
        << (session_state.load() == witness::SessionState::running && !session_stop.load()) << "}\n";
    log.flush();
    return log ? 0 : 4;
}
DWORD WINAPI session_worker(void* argument) noexcept {
    std::unique_ptr<std::ofstream> log(static_cast<std::ofstream*>(argument));
    bool focused = false, f7_down = true, f9_down = true, previous_enabled = allow_move.load();
    auto heartbeat = GetTickCount64(), settings_check = heartbeat;
    try {
        while (!session_stop.load() && !emergency.load()) {
            if (GetTickCount64() - settings_check >= 1000) {
                if (refresh_input_settings())
                    session_record(*log, "input.settings.changed");
                settings_check = GetTickCount64();
            }
            const bool now_focused = foreground();
            const bool f7 = (GetAsyncKeyState(VK_F7) & 0x8000) != 0,
                       f9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
            if (now_focused && focused) {
                if (f7 && !f7_down)
                    set_controls(!allow_move.load());
                if (f9 && !f9_down)
                    session_stop = true;
            }
            focused = now_focused;
            f7_down = f7;
            f9_down = f9;
            if (previous_enabled != allow_move.load()) {
                session_record(*log, "input.session.changed");
                previous_enabled = allow_move.load();
            }
            if (GetTickCount64() - heartbeat >= 30000) {
                session_record(*log, "input.session.heartbeat");
                heartbeat = GetTickCount64();
            }
            Sleep(10);
        }
        session_state = witness::SessionState::stopping;
        stop();
        session_record(*log, "input.session.stopped");
        session_state = witness::SessionState::stopped;
    } catch (const std::exception& e) {
        poisoned = true;
        try {
            stop();
        } catch (...) {
        }
        if (*log)
            *log << "{\"event\":\"input.session.failed\",\"error\":" << std::quoted(e.what()) << "}\n";
        session_state = witness::SessionState::failed;
    }
    log.reset();
    busy = false;
    return 0;
}
} // namespace
// Run a bounded ProbeRequest diagnostic; zero flags can observe an active input session.
extern "C" __declspec(dllexport) DWORD WINAPI WitnessProbeRun(void* argument) noexcept {
    using namespace witness;
    if (!argument)
        return 1;
    const auto req = *static_cast<ProbeRequest*>(argument);
    if (req.size != sizeof(req) || req.version != kProtocolVersion || req.duration_ms > kMaxDurationMs ||
        req.reserved > 255 || (req.reserved & 192) == 192 || ((req.reserved & 192) && !(req.reserved & 16)) ||
        (req.reserved & 12) == 12 || ((req.reserved & 16) && (req.reserved & 32)) || !req.log_path[0] ||
        req.log_path[kPathCapacity - 1])
        return 1;
    if (session_state.load() == SessionState::running && req.reserved == 0) {
        try {
            return observe_session(req);
        } catch (...) {
            return 3;
        }
    }
    if (busy.exchange(true))
        return 3;
    struct Release {
        ~Release() { busy.store(false); }
    } release;
    std::ofstream log;
    try {
        if (poisoned)
            throw std::runtime_error("Input instance failed previously; restart game");
        log.open(std::filesystem::path(req.log_path), std::ios::out | std::ios::trunc);
        if (!log)
            return 2;
        prepare((req.reserved & 1) != 0, (req.reserved & 2) != 0, GetTickCount64() + req.duration_ms,
                (req.reserved & 4) != 0, (req.reserved & 8) != 0, AimSettings{},
                (req.reserved & 16) ? ((req.reserved >> 6) == 1   ? 1
                                       : (req.reserved >> 6) == 2 ? 4
                                                                  : 2)
                                    : 0,
                (req.reserved & 32) != 0);
        log << "{\"event\":\"input.started\",\"movement_enabled\":" << allow_move.load()
            << ",\"legacy_axis0\":" << legacy_axis.load() << ",\"aim_enabled\":" << allow_aim.load()
            << ",\"aim_capture\":" << aim_capture.load() << ",\"snap_enabled\":" << allow_snap.load()
            << ",\"snap_capture\":" << snap_capture.load() << ",\"snap_steps\":" << snap_steps.load()
            << ",\"aim_model\":\"angular_pointing\",\"walking_mode\":2,\"deadzone\":0.2,\"interface\":\"IVRSystem_012\"}\n";
        while (GetTickCount64() < deadline.load() && !emergency.load()) {
            emit(log);
            if (!log)
                throw std::runtime_error("Input log write failed");
            Sleep(20);
        }
        stop();
        emit(log);
        log << "{\"event\":\"input.finished\",\"hooks_disabled\":true,\"dll_pinned_until_exit\":true,\"polls\":"
            << polls.load() << ",\"movement_calls\":" << movement_calls.load()
            << ",\"applied\":" << applied.load() << ",\"cursor_calls\":" << cursor_calls.load()
            << ",\"aim_applied\":" << aim_applied.load() << ",\"pose_samples\":" << pose_samples.load()
            << ",\"stick_applied\":" << stick_applied.load()
            << ",\"puzzle_back_presses\":" << back_presses.load()
            << ",\"cancel_suppressed\":" << cancel_suppressed.load()
            << ",\"recenter_count\":" << recenter_count.load() << ",\"turn_calls\":" << turn_calls.load()
            << ",\"turn_applied\":" << turn_applied.load() << ",\"emergency\":" << emergency.load() << "}\n";
        log.flush();
        return log ? 0 : 4;
    } catch (const std::exception& e) {
        poisoned = true;
        try {
            stop();
        } catch (...) {
        }
        if (log)
            log << "{\"event\":\"input.failed\",\"error\":" << std::quoted(e.what()) << "}\n";
        return 3;
    }
}

// Control an input session and write status into InputSessionRequest; see protocol.hpp.
extern "C" __declspec(dllexport) DWORD WINAPI WitnessInputSessionControl(void* argument) noexcept {
    using namespace witness;
    if (!argument)
        return 1;
    auto& response = *static_cast<InputSessionRequest*>(argument);
    const auto request = response;
    if (request.size != sizeof(request) || request.version != kInputProtocolVersion ||
        request.command < SessionCommand::start || request.command > SessionCommand::status ||
        request.legacy_axis0 > 1 || request.pointing > 1 || request.aim_speed_percent < 10 ||
        request.aim_speed_percent > 300 || request.aim_smoothing_ms > 250 || request.stick_speed_percent ||
        (request.snap_steps && !valid_snap_steps(request.snap_steps)) ||
        std::find(std::begin(request.log_path), std::end(request.log_path), L'\0') ==
            std::end(request.log_path))
        return 1;
    AcquireSRWLockExclusive(&control_lock);
    struct Unlock {
        ~Unlock() { ReleaseSRWLockExclusive(&control_lock); }
    } unlock;
    bool starting = false;
    DWORD result = 0;
    std::unique_ptr<std::ofstream> log;
    try {
        if (request.command == SessionCommand::start) {
            if (busy.exchange(true))
                throw std::runtime_error("Input diagnostic/session already active");
            starting = true;
            if (session_thread) {
                if (WaitForSingleObject(session_thread, 1000) != WAIT_OBJECT_0)
                    throw std::runtime_error("Prior input worker still active");
                CloseHandle(session_thread);
                session_thread = nullptr;
            }
            const std::filesystem::path path(request.log_path);
            log = witness::open_session_log(path);
            session_logging = !path.empty();
            session_stop = false;
            prepare(true, request.legacy_axis0 != 0, std::numeric_limits<ULONGLONG>::max(),
                    request.pointing != 0, false,
                    {request.aim_speed_percent / 100.f, request.aim_smoothing_ms}, request.snap_steps);
            session_record(*log, "input.session.started");
            session_state = SessionState::running;
            auto* worker_log = log.release();
            session_thread = CreateThread(nullptr, 0, session_worker, worker_log, 0, nullptr);
            if (!session_thread) {
                log.reset(worker_log);
                throw win_error("CreateThread (input session)");
            }
            starting = false;
        } else if (request.command == SessionCommand::stop) {
            session_stop = true;
            if (session_thread && WaitForSingleObject(session_thread, 10000) != WAIT_OBJECT_0)
                throw std::runtime_error("Input worker did not stop; restart game");
        } else if (request.command == SessionCommand::enable || request.command == SessionCommand::disable) {
            if (session_state.load() != SessionState::running || session_stop.load() || emergency.load() ||
                poisoned.load())
                throw std::runtime_error("Input session is inactive/failed");
            set_controls(request.command == SessionCommand::enable);
        }
    } catch (const std::exception& e) {
        result = 3;
        if (starting) {
            poisoned = true;
            try {
                stop();
            } catch (...) {
            }
            session_state = SessionState::failed;
            busy = false;
        }
        if (log && *log)
            *log << "{\"event\":\"input.session.failed\",\"error\":" << std::quoted(e.what()) << "}\n";
    }
    response.state = session_state.load();
    response.enabled = response.state == SessionState::running && enabled.load() && allow_move.load();
    response.logging = session_logging ? 1 : 0;
    response.fault = poisoned.load();
    response.legacy_axis0 = legacy_axis.load();
    response.pointing = aim_capture.load();
    response.polls = polls.load();
    response.movement_calls = movement_calls.load();
    response.applied = applied.load();
    response.puzzle_back_presses = back_presses.load();
    response.stick_speed_percent = stick_speed_percent.load();
    response.stick_applied = stick_applied.load();
    response.cancel_suppressed = cancel_suppressed.load();
    response.snap_steps = snap_steps.load();
    response.turn_calls = turn_calls.load();
    response.turn_applied = turn_applied.load();
    response.cursor_calls = cursor_calls.load();
    response.aim_applied = aim_applied.load();
    response.recenter_count = recenter_count.load();
    {
        Lock lock;
        response.aim_speed_percent = motion_speed_percent.load();
        response.aim_smoothing_ms = aim_settings.smoothing_ms;
    }
    if (response.state == SessionState::failed)
        result = 3;
    return result;
}
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
    return TRUE;
}
