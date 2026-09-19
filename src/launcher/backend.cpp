#include "launcher/backend.hpp"
#include "launcher/services/settings.hpp"
#include "launcher/services/steam_discovery.hpp"
#include "launcher/services/platform.hpp"
#include "common/session_log.hpp"
#include <shellapi.h>
#include <chrono>
#include <fstream>

namespace witness::launcher {
Backend::Backend(fs::path root, HWND notify_window) : root_(std::move(root)), window_(notify_window) {
    log_path_ = root_ / L"logs" / (L"launcher-" + log_stamp() + L".log");
    current_.settings = load_settings(root_);
    current_.game_dir = load_game_folder(root_);
    if (!valid_game_folder(current_.game_dir))
        current_.game_dir = find_game(steam_root());
    publish();
    thread_ = std::thread(&Backend::run, this);
}

Backend::~Backend() {
    shutdown();
}

void Backend::shutdown() {
    quit_ = true;
    wake_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

Snapshot Backend::snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    return published_;
}

bool Backend::request(Request request) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (request_ || published_.busy)
        return false;
    request_ = std::move(request);
    wake_.notify_all();
    return true;
}

void Backend::publish() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        published_ = current_;
    }
    PostMessageW(window_, status_message, 0, 0);
}

void Backend::announce(const std::wstring& message) {
    current_.message = message;
    if (!current_.settings.logging)
        return;
    std::error_code error;
    fs::create_directories(log_path_.parent_path(), error);
    if (error)
        return;
    const auto size = fs::file_size(log_path_, error);
    if (!error && size >= witness::session_log_limit)
        return;
    std::ofstream log(log_path_, std::ios::binary | std::ios::app);
    witness::write_session_log(log, utf8(log_stamp()) + " " + utf8(message) + "\n");
}

Session Backend::control(SessionKind kind, const wchar_t* action) {
    try {
        return send_session_command(root_, current_.pid, current_.game_dir, current_.settings, kind, action);
    } catch (...) {
        current_.uncertain = true;
        throw;
    }
}

void Backend::inspect() {
    current_.vr_ready = false;
    current_.steamvr = !process_ids(L"vrserver.exe").empty();
    const auto games = process_ids(L"witness64_d3d11.exe");
    if (games.size() > 1)
        throw std::runtime_error("Multiple Witness games are running. Close the extra instance.");
    if (games.empty()) {
        if (current_.pid)
            announce(L"Game closed. Ready for another session.");
        current_.pid = 0;
        current_.vr_ready = false;
        current_.render = {};
        current_.input = {};
        current_.uncertain = false;
        readiness_.reset();
        return;
    }
    Handle game(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, games[0]));
    if (!game)
        throw win_error("Inspect game");
    if (!same_path(process_path(game.get()), current_.game_dir / L"witness64_d3d11.exe"))
        throw std::runtime_error(
            "The running game is in another folder. Select its installation or close it first.");
    if (current_.pid != games[0]) {
        current_.uncertain = false;
        current_.render = {};
        current_.input = {};
    }
    current_.pid = games[0];
    current_.vr_ready = false;
    if (current_.uncertain)
        return;
    const auto found = readiness_.inspect(current_.pid, current_.game_dir, root_);
    current_.vr_ready = found.vr_ready;
    current_.render = found.render_loaded ? control(SessionKind::render, L"status") : Session{};
    current_.input = found.input_loaded ? control(SessionKind::input, L"status") : Session{};
}

void Backend::run() {
    while (!quit_) {
        std::optional<Request> request;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            request.swap(request_);
        }
        current_.busy = true;
        publish();
        try {
            const bool selection =
                request && (request->action == Action::select_game ||
                            request->action == Action::detect_game || request->action == Action::cancel);
            if (selection)
                handle(*request);
            inspect();
            if (request && !selection)
                handle(*request);
            start_game_if_needed();
            if (current_.pending) {
                if (current_.vr_ready)
                    attach();
                else if (GetTickCount64() >= pending_until_) {
                    current_.pending = false;
                    announce(L"VR startup timed out. Check SteamVR; close a non-VR game before relaunching.");
                }
            }
        } catch (const std::exception& error) {
            const auto message = wide(error.what());
            // Do not automatically retry a remote operation whose target-side outcome is unknown.
            if (message.find(L"loader") != std::wstring::npos ||
                message.find(L"Remote thread") != std::wstring::npos ||
                message.find(L"session command") != std::wstring::npos)
                current_.uncertain = true;
            current_.pending = false;
            current_.vr_ready = false;
            if (current_.render.loaded) {
                current_.render.state = L"Status unavailable";
                current_.render.enabled = false;
            }
            if (current_.input.loaded) {
                current_.input.state = L"Status unavailable";
                current_.input.enabled = false;
            }
            if (current_.message != message)
                announce(message);
        }
        current_.busy = false;
        publish();
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait_for(lock, std::chrono::seconds(3), [&] { return quit_ || request_.has_value(); });
    }
}

} // namespace witness::launcher
