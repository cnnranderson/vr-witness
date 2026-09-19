#include "backend.hpp"
#include "session_log.hpp"
#include "build_validation.hpp"
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <regex>
#include <sstream>

namespace witness::launcher {
namespace {
std::wstring wide(const std::string& text) {
    if (text.empty())
        return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                          static_cast<int>(text.size()), nullptr, 0);
    if (!count)
        throw win_error("Decode UTF-8");
    std::wstring out(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(),
                        count);
    return out;
}
std::string read_file(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), {}};
}
std::wstring read_ini(const fs::path& file, const wchar_t* section, const wchar_t* key,
                      const wchar_t* fallback = L"") {
    std::vector<wchar_t> value(32768);
    const auto count = GetPrivateProfileStringW(section, key, fallback, value.data(),
                                                static_cast<DWORD>(value.size()), file.c_str());
    return {value.data(), count};
}
void write_ini(const fs::path& file, const wchar_t* section, const wchar_t* key, const std::wstring& value) {
    fs::create_directories(file.parent_path());
    if (!fs::exists(file)) {
        std::ofstream stream(file, std::ios::binary);
        stream.write("\xff\xfe", 2);
        if (!stream)
            throw std::runtime_error("Cannot create settings; extract the package to a writable folder.");
    }
    if (!WritePrivateProfileStringW(section, key, value.c_str(), file.c_str()))
        throw win_error("Save settings");
}
int setting_number(const fs::path& file, const wchar_t* key, int fallback) {
    const auto text = read_ini(file, L"Input", key);
    if (text.empty() || text.find_first_not_of(L"0123456789") != std::wstring::npos)
        return fallback;
    try {
        return std::stoi(text);
    } catch (...) {
        return fallback;
    }
}
std::vector<DWORD> process_ids(const wchar_t* name) {
    Handle list(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!list)
        throw win_error("List processes");
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    std::vector<DWORD> ids;
    if (Process32FirstW(list.get(), &entry))
        do {
            if (!_wcsicmp(entry.szExeFile, name))
                ids.push_back(entry.th32ProcessID);
        } while (Process32NextW(list.get(), &entry));
    return ids;
}
std::string run_loader(const fs::path& executable, const std::vector<std::wstring>& arguments) {
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE read_raw{}, write_raw{};
    if (!CreatePipe(&read_raw, &write_raw, &security, 0))
        throw win_error("Create loader output pipe");
    Handle read_pipe(read_raw), write_pipe(write_raw);
    if (!SetHandleInformation(read_pipe.get(), HANDLE_FLAG_INHERIT, 0))
        throw win_error("Configure loader pipe");
    Handle null_input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                  OPEN_EXISTING, 0, nullptr));
    std::wstring command = quote_argument(executable.wstring());
    for (const auto& arg : arguments)
        command += L" " + quote_argument(arg);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = null_input.get();
    startup.hStdOutput = startup.hStdError = write_pipe.get();
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                        executable.parent_path().c_str(), &startup, &process))
        throw win_error("Start mod loader");
    Handle child(process.hProcess), thread(process.hThread);
    std::string output;
    const auto deadline = GetTickCount64() + 40000;
    bool exited = false;
    for (;;) {
        DWORD available{};
        if (!PeekNamedPipe(read_pipe.get(), nullptr, 0, nullptr, &available, nullptr))
            throw win_error("Read loader output");
        if (available) {
            std::array<char, 4096> buffer{};
            DWORD got{};
            if (!ReadFile(read_pipe.get(), buffer.data(), std::min<DWORD>(available, buffer.size()), &got,
                          nullptr))
                throw win_error("Read loader output");
            if (output.size() + got > 65536)
                throw std::runtime_error(
                    "Unexpectedly large loader response; restart the game before retrying.");
            output.append(buffer.data(), got);
            continue;
        }
        if (exited)
            break;
        exited = WaitForSingleObject(child.get(), 25) == WAIT_OBJECT_0;
        if (!exited && GetTickCount64() >= deadline) {
            // Only terminate our stalled helper; preserve the game and any in-flight remote allocations.
            TerminateProcess(child.get(), 1);
            throw std::runtime_error("Loader timed out. Close the game normally before retrying.");
        }
    }
    DWORD exit_code{};
    if (!GetExitCodeProcess(child.get(), &exit_code))
        throw win_error("Read loader result");
    if (exit_code)
        throw std::runtime_error(output.empty() ? "The mod loader failed; see logs." : output);
    return output;
}
std::wstring log_stamp() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t text[80]{};
    swprintf(text, 80, L"%04u%02u%02u-%02u%02u%02u-%03u-%lu", now.wYear, now.wMonth, now.wDay, now.wHour,
             now.wMinute, now.wSecond, now.wMilliseconds, GetCurrentProcessId());
    return text;
}
std::optional<std::string> field(const std::string& json, const std::string& key) {
    const std::regex pattern("\\\"" + key +
                             "\\\"\\s*:\\s*(\\\"[^\\\"]*\\\"|true|false|[0-9]+(?:\\.[0-9]+)?)");
    std::smatch match;
    if (!std::regex_search(json, match, pattern))
        return std::nullopt;
    return match[1].str();
}
bool boolean(const std::string& json, const std::string& key) {
    const auto value = field(json, key);
    if (!value || (*value != "true" && *value != "false"))
        throw std::runtime_error("Invalid loader status: " + key);
    return *value == "true";
}
int integer(const std::string& json, const std::string& key) {
    const auto value = field(json, key);
    return value ? std::stoi(*value) : 0;
}
void read_memory(HANDLE process, std::uintptr_t address, void* output, SIZE_T size) {
    SIZE_T got{};
    if (!ReadProcessMemory(process, reinterpret_cast<void*>(address), output, size, &got) || got != size)
        throw win_error("Read game readiness");
}
std::size_t file_offset(const std::vector<unsigned char>& image, DWORD rva) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.data());
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image.data() + dos->e_lfanew);
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        if (rva >= section[i].VirtualAddress && rva - section[i].VirtualAddress < section[i].SizeOfRawData)
            return section[i].PointerToRawData + rva - section[i].VirtualAddress;
    throw std::runtime_error("Readiness reference is outside the verified image.");
}
} // namespace

bool Settings::operator==(const Settings& other) const {
    return stick_speed == other.stick_speed && motion_speed == other.motion_speed &&
           smoothing_ms == other.smoothing_ms && snap_steps == other.snap_steps &&
           legacy_axis == other.legacy_axis && logging == other.logging;
}
bool valid_settings(const Settings& s) {
    return s.stick_speed >= 10 && s.stick_speed <= 300 && s.motion_speed >= 10 && s.motion_speed <= 300 &&
           s.smoothing_ms >= 0 && s.smoothing_ms <= 250 &&
           (s.snap_steps == 0 || s.snap_steps == 1 || s.snap_steps == 2 || s.snap_steps == 4);
}
std::wstring quote_argument(const std::wstring& argument) {
    std::wstring result = L"\"";
    unsigned slashes = 0;
    for (wchar_t c : argument) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        result += c;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    result += L'"';
    return result;
}
std::vector<std::string> vdf_values(const std::string& text, const std::string& key) {
    const std::regex pattern("\"" + key + "\"\\s*\"((?:\\\\.|[^\"\\\\])*)\"");
    std::vector<std::string> values;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), pattern); it != std::sregex_iterator();
         ++it) {
        const auto raw = (*it)[1].str();
        std::string value;
        for (std::size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] == '\\' && i + 1 < raw.size() && (raw[i + 1] == '\\' || raw[i + 1] == '"'))
                ++i;
            value += raw[i];
        }
        values.push_back(value);
    }
    return values;
}
fs::path steam_root() {
    std::vector<wchar_t> value(32768);
    DWORD bytes = static_cast<DWORD>(value.size() * sizeof(wchar_t));
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr,
                     value.data(), &bytes) == ERROR_SUCCESS)
        return value.data();
    return {};
}
bool valid_game_folder(const fs::path& folder) {
    return !folder.empty() && fs::is_regular_file(folder / L"witness_d3d11.exe") &&
           fs::is_regular_file(folder / L"witness64_d3d11.exe");
}
fs::path find_game(const fs::path& steam) {
    if (steam.empty())
        return {};
    std::vector<fs::path> libraries{steam};
    for (const auto& path : vdf_values(read_file(steam / L"steamapps" / L"libraryfolders.vdf"), "path"))
        libraries.emplace_back(wide(path));
    for (const auto& library : libraries) {
        const auto dirs =
            vdf_values(read_file(library / L"steamapps" / L"appmanifest_210970.acf"), "installdir");
        for (const auto& dir : dirs) {
            const auto folder = library / L"steamapps" / L"common" / wide(dir);
            if (valid_game_folder(folder))
                return fs::weakly_canonical(folder);
        }
    }
    return {};
}
Settings load_settings(const fs::path& root) {
    const auto file = root / L"config" / L"input.ini";
    Settings s;
    const int stick = setting_number(file, L"StickCursorSpeedPercent", 20);
    const int motion = setting_number(file, L"MotionCursorSpeedPercent", 60);
    const int smooth = setting_number(file, L"AimSmoothingMs", 80);
    const int snap = setting_number(file, L"SnapSteps", 1);
    if (stick >= 10 && stick <= 300)
        s.stick_speed = stick;
    if (motion >= 10 && motion <= 300)
        s.motion_speed = motion;
    if (smooth >= 0 && smooth <= 250)
        s.smoothing_ms = smooth;
    if (snap == 0 || snap == 1 || snap == 2 || snap == 4)
        s.snap_steps = snap;
    s.legacy_axis = setting_number(file, L"LegacyAxis0", 1) != 0;
    s.logging = setting_number(file, L"DiagnosticLogging", 0) == 1;
    return s;
}
void save_settings(const fs::path& root, const Settings& s) {
    if (!valid_settings(s))
        throw std::runtime_error("Settings are outside supported ranges.");
    const auto file = root / L"config" / L"input.ini";
    write_ini(file, L"Input", L"StickCursorSpeedPercent", std::to_wstring(s.stick_speed));
    write_ini(file, L"Input", L"MotionCursorSpeedPercent", std::to_wstring(s.motion_speed));
    write_ini(file, L"Input", L"AimSmoothingMs", std::to_wstring(s.smoothing_ms));
    write_ini(file, L"Input", L"SnapSteps", std::to_wstring(s.snap_steps));
    write_ini(file, L"Input", L"LegacyAxis0", s.legacy_axis ? L"1" : L"0");
    write_ini(file, L"Input", L"DiagnosticLogging", s.logging ? L"1" : L"0");
}
Session parse_session(const std::string& json, bool input) {
    Session s;
    const auto state = field(json, "state");
    if (!state || (*state != "\"running\"" && *state != "\"stopped\"" && *state != "\"stopping\"" &&
                   *state != "\"failed\""))
        throw std::runtime_error("Missing or invalid loader session state.");
    s.state = wide(state->substr(1, state->size() - 2));
    s.loaded = boolean(json, "loaded");
    s.enabled = boolean(json, "enabled");
    s.fault = boolean(json, "fault");
    s.logging = field(json, "logging") && boolean(json, "logging");
    if (input) {
        s.motion_speed = integer(json, "aim_speed_percent");
        s.smoothing_ms = integer(json, "aim_smoothing_ms");
        const auto angle = field(json, "snap_angle");
        s.snap_steps = angle && *angle == "22.5" ? 1
                       : angle && *angle == "45" ? 2
                       : angle && *angle == "90" ? 4
                                                 : 0;
        s.legacy_axis = field(json, "legacy_axis0") && boolean(json, "legacy_axis0");
    }
    return s;
}
Backend::Backend(fs::path root, HWND notify_window) : root_(std::move(root)), window_(notify_window) {
    log_path_ = root_ / L"logs" / (L"launcher-" + log_stamp() + L".log");
    current_.settings = load_settings(root_);
    current_.game_dir = read_ini(root_ / L"config" / L"launcher.ini", L"Launcher", L"GameDirectory");
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
void Backend::save_game() {
    write_ini(root_ / L"config" / L"launcher.ini", L"Launcher", L"GameDirectory",
              current_.game_dir.wstring());
}
Session Backend::control(bool input, const wchar_t* action) {
    if (!current_.pid)
        throw std::runtime_error("The game is not running.");
    const auto loader = root_ / L"runtime" / L"witness-probe-loader.exe";
    std::vector<std::wstring> args{input ? L"--controller-session" : L"--cursor-session",
                                   action,
                                   L"--pid",
                                   std::to_wstring(current_.pid),
                                   L"--game-exe",
                                   (current_.game_dir / L"witness64_d3d11.exe").wstring()};
    if (!wcscmp(action, L"start")) {
        if (current_.settings.logging) {
            args.insert(
                args.end(),
                {L"--log",
                 (root_ / L"logs" / ((input ? L"input-" : L"render-") + log_stamp() + L".jsonl")).wstring()});
        } else {
            args.push_back(L"--no-log");
        }
        if (input) {
            const auto& s = current_.settings;
            args.insert(args.end(),
                        {L"--controller-aim", L"--aim-speed-percent", std::to_wstring(s.motion_speed),
                         L"--aim-smoothing-ms", std::to_wstring(s.smoothing_ms)});
            if (s.legacy_axis)
                args.push_back(L"--controller-legacy-axis0");
            if (s.snap_steps)
                args.insert(args.end(), {L"--controller-snap", L"--snap-angle",
                                         s.snap_steps == 1   ? L"22.5"
                                         : s.snap_steps == 2 ? L"45"
                                                             : L"90"});
        }
    }
    try {
        return parse_session(run_loader(loader, args), input);
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
        verified_pid_ = 0;
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
    const auto list = modules(current_.pid);
    std::uintptr_t base{};
    bool input_loaded = false, render_loaded = false;
    for (const auto& module : list) {
        if (!_wcsicmp(module.name.c_str(), L"witness64_d3d11.exe")) {
            if (module.size != 74457088)
                throw std::runtime_error("This game build is not supported.");
            base = module.base;
        }
        for (bool input : {false, true}) {
            const auto dll = input ? L"witness-input-probe.dll" : L"witness-render-probe.dll";
            if (!_wcsicmp(module.name.c_str(), dll)) {
                if (!same_path(module.path, root_ / L"runtime" / dll))
                    throw std::runtime_error(
                        "A different mod build is loaded. Close the game normally before using this package.");
                (input ? input_loaded : render_loaded) = true;
            }
        }
    }
    if (!base)
        return;
    if (verified_pid_ != current_.pid) {
        const auto exe = current_.game_dir / L"witness64_d3d11.exe";
        if (validation::sha256(exe) != "8d672d444df6a6df7130f25a517bdab1af92731fe2138dc5b324d519561d55a5")
            throw std::runtime_error(
                "Unsupported Witness executable. This package supports the tested Steam Windows x64 build only.");
        const auto image = read_file(exe);
        verified_image_.assign(image.begin(), image.end());
        verified_pid_ = current_.pid;
    }
    for (const auto& site :
         std::array<std::pair<DWORD, SIZE_T>, 3>{{{0x37A436, 7}, {0x37A4F9, 7}, {0x37A3F6, 6}}}) {
        std::array<unsigned char, 7> live{};
        read_memory(game.get(), base + site.first, live.data(), site.second);
        const auto offset = file_offset(verified_image_, site.first);
        if (offset + site.second > verified_image_.size() ||
            !std::equal(live.begin(), live.begin() + site.second, verified_image_.begin() + offset))
            throw std::runtime_error("Game readiness code differs from the supported build.");
    }
    std::array<std::uintptr_t, 2> pointers{};
    read_memory(game.get(), base + 0x469AB38, pointers.data(), sizeof(pointers));
    current_.vr_ready = pointers[0] && pointers[1];
    current_.render = render_loaded ? control(false, L"status") : Session{};
    current_.input = input_loaded ? control(true, L"status") : Session{};
}
void Backend::attach() {
    if (!current_.pid || !current_.vr_ready || current_.uncertain)
        throw std::runtime_error("The game has not initialized VR yet.");
    for (bool input : {false, true}) {
        auto& s = input ? current_.input : current_.render;
        if (s.fault || s.state == L"failed" || s.state == L"stopping")
            throw std::runtime_error("A mod component reports a fault. Close the game normally and retry.");
    }
    for (bool input : {false, true}) {
        auto& s = input ? current_.input : current_.render;
        if (s.state != L"running")
            s = control(input, L"start");
        else if (!s.enabled)
            s = control(input, L"enable");
        if (!s.enabled || s.fault || s.state != L"running")
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
    if (request.action == Action::cancel) {
        current_.pending = false;
        announce(L"Automatic attachment canceled; the game was left running.");
        return;
    }
    if (request.action == Action::detect_game || request.action == Action::select_game) {
        const auto folder =
            request.action == Action::detect_game ? find_game(steam_root()) : request.game_dir;
        if (!valid_game_folder(folder))
            throw std::runtime_error(
                "Could not find both Witness executables. Browse to the game installation.");
        current_.game_dir = fs::weakly_canonical(folder);
        verified_pid_ = 0;
        save_game();
        announce(L"Game folder saved.");
        return;
    }
    if (request.action == Action::apply) {
        if (!valid_settings(request.settings))
            throw std::runtime_error("Invalid settings.");
        save_settings(root_, request.settings);
        current_.settings = request.settings;
        if (!current_.uncertain && current_.input.state == L"running" &&
            (current_.input.smoothing_ms != current_.settings.smoothing_ms ||
             current_.input.snap_steps != current_.settings.snap_steps ||
             current_.input.legacy_axis != current_.settings.legacy_axis ||
             current_.input.logging != current_.settings.logging)) {
            const bool enabled = current_.input.enabled;
            current_.input = control(true, L"stop");
            current_.input = control(true, L"start");
            if (!enabled)
                current_.input = control(true, L"disable");
            announce(L"Settings applied. Input restarted; center the sticks and release the triggers.");
        } else
            announce(L"Settings saved. Active cursor speeds update within about one second.");
        if (!current_.uncertain && current_.render.state == L"running" &&
            current_.render.logging != current_.settings.logging) {
            const bool enabled = current_.render.enabled;
            current_.render = control(false, L"stop");
            current_.render = control(false, L"start");
            if (!enabled)
                current_.render = control(false, L"disable");
            announce(L"Logging setting applied. VR fixes restarted; release the controls.");
        }
        return;
    }
    if (request.action == Action::stop) {
        current_.pending = false;
        if (current_.input.loaded)
            current_.input = control(true, L"stop");
        if (current_.render.loaded)
            current_.render = control(false, L"stop");
        announce(L"VR fixes stopped. The game is still running; native rendering defects may return.");
        return;
    }
    if (!valid_game_folder(current_.game_dir))
        throw std::runtime_error("Select your Witness installation first.");
    for (const auto* name :
         {L"witness-probe-loader.exe", L"witness-input-probe.dll", L"witness-render-probe.dll"})
        if (!fs::is_regular_file(root_ / L"runtime" / name))
            throw std::runtime_error("Package files are missing. Extract the entire ZIP before running.");
    if (current_.uncertain)
        throw std::runtime_error("A loader operation failed. Close the game normally before retrying.");
    save_game();
    save_settings(root_, current_.settings);
    if (request.action == Action::launch) {
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
