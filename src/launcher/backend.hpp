#pragma once
#include "launcher/model.hpp"
#include "launcher/services/game_readiness.hpp"
#include "launcher/services/loader_client.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace witness::launcher {
// One worker serializes process inspection and loader calls; no game calls run on the UI thread.
class Backend {
public:
    Backend(fs::path root, HWND notify_window);
    ~Backend();
    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;
    Snapshot snapshot();
    bool request(Request request);
    void shutdown();

private:
    void run();
    void handle(const Request& request);
    void select_game(const fs::path& folder);
    void apply_settings(const Settings& settings);
    void stop_sessions();
    void begin_startup(bool launch_game);
    void inspect();
    void attach();
    void start_game_if_needed();
    Session control(SessionKind kind, const wchar_t* action);
    void announce(const std::wstring& message);
    void publish();
    fs::path root_, log_path_;
    HWND window_{};
    std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<Request> request_;
    Snapshot published_; // Protected by mutex_; copied to the UI thread.
    Snapshot current_;   // Worker-owned after construction.
    std::atomic<bool> quit_{false};
    std::thread thread_;
    ULONGLONG pending_until_{};
    bool pending_launch_game_{};
    GameReadiness readiness_;
};

inline constexpr UINT status_message = WM_APP + 1;
} // namespace witness::launcher
