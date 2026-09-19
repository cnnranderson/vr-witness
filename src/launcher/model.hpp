#pragma once
#include <windows.h>
#include <filesystem>
#include <string>

namespace witness::launcher {
namespace fs = std::filesystem;

struct Settings {
    int stick_speed{20};  // Percent of view width per second.
    int motion_speed{60}; // Percent of view width per second.
    int smoothing_ms{80};
    int snap_steps{1}; // 0 disables snapping; 1/2/4 select 22.5/45/90 degrees.
    bool legacy_axis{true};
    bool logging{false};
    bool operator==(const Settings& other) const;
};

struct Session {
    std::wstring state{L"Not attached"};
    bool loaded{};
    bool enabled{};
    bool fault{};
    bool logging{};
    int motion_speed{};
    int smoothing_ms{};
    int snap_steps{};
    bool legacy_axis{};
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
    std::wstring message{L"Choose your game folder, then select Launch VR."};
};
enum class Action { launch, attach, stop, apply, select_game, detect_game, cancel };

struct Request {
    Action action;
    fs::path game_dir;
    Settings settings;
};

} // namespace witness::launcher
