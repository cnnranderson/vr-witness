#include "launcher/backend.hpp"
#include "launcher/services/settings.hpp"
#include "launcher/services/steam_discovery.hpp"
#include "launcher/services/platform.hpp"
#include <shellapi.h>

namespace witness::launcher {
void Backend::attach() {
    if (!current_.pid || !current_.vr_ready || current_.uncertain)
        throw std::runtime_error("The game has not initialized VR yet.");
    for (auto kind : {SessionKind::render, SessionKind::input}) {
        auto& session = kind == SessionKind::input ? current_.input : current_.render;
        if (session.fault || session.state == L"failed" || session.state == L"stopping")
            throw std::runtime_error("A mod component reports a fault. Close the game normally and retry.");
    }
    for (auto kind : {SessionKind::render, SessionKind::input}) {
        auto& session = kind == SessionKind::input ? current_.input : current_.render;
        if (session.state != L"running")
            session = control(kind, L"start");
        if (!session.enabled)
            session = control(kind, L"enable");
        if (!session.enabled || session.fault || session.state != L"running")
            throw std::runtime_error("A mod component did not become active.");
    }
    current_.pending = false;
    announce(L"VR mod active. Focus the game, center the sticks and release the triggers.");
}

void Backend::start_game_if_needed() {
    if (!current_.pending || !pending_launch_game_ || !current_.steamvr)
        return;
    if (!current_.pid && process_ids(L"witness_d3d11.exe").empty()) {
        const auto exe = current_.game_dir / L"witness_d3d11.exe";
        std::wstring command = quote_argument(exe.wstring()) + L" -vr";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                            current_.game_dir.c_str(), &startup, &process))
            throw win_error("Start The Witness");
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    pending_launch_game_ = false;
}

void Backend::handle(const Request& request) {
    switch (request.action) {
    case Action::cancel:
        current_.pending = false;
        announce(L"Automatic attachment canceled; the game was left running.");
        return;
    case Action::detect_game:
        select_game(find_game(steam_root()));
        return;
    case Action::select_game:
        select_game(request.game_dir);
        return;
    case Action::apply:
        apply_settings(request.settings);
        return;

    case Action::launch:
        begin_startup(true);
        return;
    case Action::toggle_attachment:
        if (current_.attached())
            stop_sessions();
        else
            begin_startup(false);
        return;
    }
}

void Backend::select_game(const fs::path& folder) {
    if (!valid_game_folder(folder))
        throw std::runtime_error("Could not find both Witness executables. Browse to the game installation.");
    current_.game_dir = fs::weakly_canonical(folder);
    readiness_.reset();
    save_game_folder(root_, current_.game_dir);
    announce(L"Game folder saved.");
}

void Backend::apply_settings(const Settings& settings) {
    if (!valid_settings(settings))
        throw std::runtime_error("Invalid settings.");
    save_settings(root_, settings);
    current_.settings = settings;
    if (!current_.uncertain && current_.input.state == L"running" &&
        (current_.input.smoothing_ms != current_.settings.smoothing_ms ||
         current_.input.snap_steps != current_.settings.snap_steps || !current_.input.legacy_axis ||
         current_.input.logging != current_.settings.logging)) {
        const bool enabled = current_.input.enabled;
        current_.input = control(SessionKind::input, L"stop");
        current_.input = control(SessionKind::input, L"start");
        if (!enabled)
            current_.input = control(SessionKind::input, L"disable");
        announce(L"Settings applied. Input restarted; center the sticks and release the triggers.");
    } else
        announce(L"Settings saved. Active cursor speeds update within about one second.");
    if (!current_.uncertain && current_.render.state == L"running" &&
        current_.render.logging != current_.settings.logging) {
        const bool enabled = current_.render.enabled;
        current_.render = control(SessionKind::render, L"stop");
        current_.render = control(SessionKind::render, L"start");
        if (!enabled)
            current_.render = control(SessionKind::render, L"disable");
        announce(L"Logging setting applied. VR fixes restarted; release the controls.");
    }
}

void Backend::stop_sessions() {
    current_.pending = false;
    if (current_.input.loaded)
        current_.input = control(SessionKind::input, L"stop");
    if (current_.render.loaded)
        current_.render = control(SessionKind::render, L"stop");
    announce(L"Detached from game. DLLs remain loaded until the game exits.");
}

void Backend::begin_startup(bool launch_game) {
    if (!valid_game_folder(current_.game_dir))
        throw std::runtime_error("Select your Witness installation first.");
    for (const auto* name :
         {L"witness-probe-loader.exe", L"witness-input-probe.dll", L"witness-render-probe.dll"})
        if (!fs::is_regular_file(root_ / L"runtime" / name))
            throw std::runtime_error("Package files are missing. Extract the entire ZIP before running.");
    if (current_.uncertain)
        throw std::runtime_error("A loader operation failed. Close the game normally before retrying.");
    save_game_folder(root_, current_.game_dir);
    save_settings(root_, current_.settings);
    if (launch_game) {
        if (!current_.steamvr) {
            if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", L"steam://rungameid/250820",
                                                        nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
                throw std::runtime_error("SteamVR could not be started. Start SteamVR manually and retry.");
        }
        pending_launch_game_ = !current_.pid;
    } else if (!current_.pid)
        throw std::runtime_error("Start the game in VR first, or select Launch VR.");
    current_.pending = true;
    pending_until_ = GetTickCount64() + 300000;
    announce(
        L"Waiting for native VR, then attaching automatically. Keep your headset and controllers awake.");
}

} // namespace witness::launcher
