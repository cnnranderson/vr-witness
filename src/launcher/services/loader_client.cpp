#include "launcher/services/loader_client.hpp"
#include "launcher/services/platform.hpp"
#include <algorithm>
#include <array>
#include <optional>
#include <regex>

namespace witness::launcher {
namespace {
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
} // namespace

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

Session parse_session(const std::string& json, SessionKind kind) {
    Session session;
    const auto state = field(json, "state");
    if (!state || (*state != "\"running\"" && *state != "\"stopped\"" && *state != "\"stopping\"" &&
                   *state != "\"failed\""))
        throw std::runtime_error("Missing or invalid loader session state.");
    session.state = wide(state->substr(1, state->size() - 2));
    session.loaded = boolean(json, "loaded");
    session.enabled = boolean(json, "enabled");
    session.fault = boolean(json, "fault");
    session.logging = field(json, "logging") && boolean(json, "logging");
    if (kind == SessionKind::input) {
        session.motion_speed = integer(json, "aim_speed_percent");
        session.smoothing_ms = integer(json, "aim_smoothing_ms");
        const auto angle = field(json, "snap_angle");
        session.snap_steps = angle && *angle == "22.5" ? 1
                             : angle && *angle == "45" ? 2
                             : angle && *angle == "90" ? 4
                                                       : 0;
        session.legacy_axis = field(json, "legacy_axis0") && boolean(json, "legacy_axis0");
    }
    return session;
}

Session send_session_command(const fs::path& root, DWORD pid, const fs::path& game_dir,
                             const Settings& settings, SessionKind kind, const wchar_t* action) {
    const bool input = kind == SessionKind::input;
    if (!pid)
        throw std::runtime_error("The game is not running.");
    const auto loader = root / L"runtime" / L"witness-probe-loader.exe";
    std::vector<std::wstring> args{input ? L"--controller-session" : L"--cursor-session",
                                   action,
                                   L"--pid",
                                   std::to_wstring(pid),
                                   L"--game-exe",
                                   (game_dir / L"witness64_d3d11.exe").wstring()};
    if (!wcscmp(action, L"start")) {
        if (settings.logging) {
            args.insert(
                args.end(),
                {L"--log",
                 (root / L"logs" / ((input ? L"input-" : L"render-") + log_stamp() + L".jsonl")).wstring()});
        } else {
            args.push_back(L"--no-log");
        }
        if (input) {
            args.insert(args.end(),
                        {L"--controller-aim", L"--aim-speed-percent", std::to_wstring(settings.motion_speed),
                         L"--aim-smoothing-ms", std::to_wstring(settings.smoothing_ms)});
            if (settings.legacy_axis)
                args.push_back(L"--controller-legacy-axis0");
            if (settings.snap_steps)
                args.insert(args.end(), {L"--controller-snap", L"--snap-angle",
                                         settings.snap_steps == 1   ? L"22.5"
                                         : settings.snap_steps == 2 ? L"45"
                                                                    : L"90"});
        }
    }
    return parse_session(run_loader(loader, args), kind);
}

} // namespace witness::launcher
