#pragma once
#include "win_util.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace witness::launcher {
namespace fs = std::filesystem;

struct Settings {
    int stick_speed{20};
    int motion_speed{60};
    int smoothing_ms{80};
    int snap_steps{1};
    bool legacy_axis{true};
    bool logging{false};
    bool operator==(const Settings& other) const;
};
struct Session {
    std::wstring state{L"Not attached"};
    bool loaded{}, enabled{}, fault{};
    bool logging{};
    int motion_speed{}, smoothing_ms{}, snap_steps{};
    bool legacy_axis{};
};
struct Snapshot {
    fs::path game_dir;
    Settings settings;
    DWORD pid{};
    bool steamvr{}, vr_ready{}, busy{}, pending{}, uncertain{};
    Session render, input;
    std::wstring message{L"Choose your game folder, then select Launch VR."};
};
enum class Action { launch, attach, stop, apply, select_game, detect_game, cancel };
struct Request {
    Action action;
    fs::path game_dir;
    Settings settings;
};

// Windows command-line escaping, including trailing slashes and embedded quotes.
std::wstring quote_argument(const std::wstring& argument);
// Read quoted VDF values for one key, including escaped Windows paths.
std::vector<std::string> vdf_values(const std::string& text, const std::string& key);
fs::path find_game(const fs::path& steam_root);
fs::path steam_root();
bool valid_game_folder(const fs::path& folder);
bool valid_settings(const Settings& settings);
Settings load_settings(const fs::path& root);
void save_settings(const fs::path& root, const Settings& settings);
Session parse_session(const std::string& json, bool input);

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
    void inspect();
    void attach();
    void start_game_if_needed();
    Session control(bool input, const wchar_t* action);
    void announce(const std::wstring& message);
    void publish();
    void save_game();
    fs::path root_, log_path_;
    HWND window_{};
    std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<Request> request_;
    Snapshot published_, current_;
    std::atomic<bool> quit_{false};
    std::thread thread_;
    ULONGLONG pending_until_{};
    bool pending_launch_game_{};
    DWORD verified_pid_{};
    std::vector<unsigned char> verified_image_;
};
inline constexpr UINT status_message = WM_APP + 1;
} // namespace witness::launcher
