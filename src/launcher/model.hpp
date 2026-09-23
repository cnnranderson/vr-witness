#pragma once
#include <windows.h>
#include <filesystem>
#include <string>

namespace witness::launcher {
namespace fs = std::filesystem;

enum class ControllerType { knuckles, xbox360, steam_frame };

struct Settings {
    int stick_speed{20};  // Percent of view width per second.
    int motion_speed{60}; // Percent of view width per second.
    int smoothing_ms{80};
    int snap_steps{1}; // 0 disables snapping; 1/2/4 select 22.5/45/90 degrees.
    ControllerType controller{ControllerType::knuckles};
    bool logging{false};
    bool operator==(const Settings& other) const;
};

struct Session {
    std::wstring state{L"stopped"};
    bool loaded{};
    bool enabled{};
    bool fault{};
    bool logging{};
    int motion_speed{};
    int smoothing_ms{};
    int snap_steps{};
    bool legacy_axis{};
    bool height_calibrated{};
    float height_m{}, height_offset_m{};

    bool active() const { return loaded && enabled && !fault && state == L"running"; }
};

struct Snapshot {
    fs::path game_dir;
    Settings settings;
    DWORD pid{};
    bool steamvr{};
    bool vr_ready{};
    bool busy{};      // Worker is processing a request or refreshing status.
    bool pending{};   // Waiting for native VR before automatic attachment.
    bool uncertain{}; // Remote command outcome is unknown; require game restart.
    Session render;
    Session input;

    bool active() const { return !uncertain && render.active() && input.active(); }

    bool attached() const { return render.state == L"running" || input.state == L"running"; }

    bool error{};
    std::wstring message; // Debug log text; only errors are shown in a dialog.
};
enum class Action { launch, toggle_attachment, apply, select_game, detect_game, cancel };

struct Request {
    Action action;
    fs::path game_dir;
    Settings settings;
};

} // namespace witness::launcher
