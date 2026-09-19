#include "launcher/backend.hpp"
#include "config_path.hpp"
#include "session_log.hpp"
#include <shellapi.h>
#include <fstream>
#include <iostream>
using namespace witness::launcher;
namespace {
void require(bool value, const char* description) {
    if (!value)
        throw std::runtime_error(description);
}
void touch(const fs::path& path) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path);
    file << "fixture";
}
void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path);
    file << text;
}
} // namespace
int main() {
    try {
        const auto root = fs::path(witness::module_path()).parent_path() /
                          (L"launcher-test-" + std::to_wstring(GetCurrentProcessId()));
        const std::vector<std::wstring> values{L"",      L"C:\\A folder\\",     L"embedded \"quotes\"",
                                               L"plain", L"\\\\server\\folder", L"Unicode \u00e9"};
        std::wstring command = L"program.exe";
        for (const auto& value : values)
            command += L" " + quote_argument(value);
        int count{};
        auto** args = CommandLineToArgvW(command.c_str(), &count);
        require(args && count == static_cast<int>(values.size() + 1), "command argument count");
        for (int i = 1; i < count; ++i)
            require(args[i] == values[i - 1], "quoted argument round-trip");
        LocalFree(args);
        auto paths = vdf_values(R"("path" "C:\\Steam Library" "label" "ignored" "path" "D:\\More")", "path");
        require(paths.size() == 2 && paths[0] == "C:\\Steam Library" && paths[1] == "D:\\More",
                "VDF escaped paths");
        const auto steam = root / L"steam";
        const auto library = root / L"another library";
        auto escaped = witness::utf8(library.wstring());
        std::string encoded;
        for (char c : escaped) {
            if (c == '\\')
                encoded += '\\';
            encoded += c;
        }
        write(steam / L"steamapps/libraryfolders.vdf", "\"path\" \"" + encoded + "\"");
        write(library / L"steamapps/appmanifest_210970.acf", "\"installdir\" \"A Witness Folder\"");
        const auto game = library / L"steamapps/common/A Witness Folder";
        require(find_game(steam).empty(), "discovery refuses missing executables");
        touch(game / L"witness_d3d11.exe");
        touch(game / L"witness64_d3d11.exe");
        require(witness::same_path(find_game(steam), game), "secondary Steam library discovery");
        require(!valid_game_folder(root / L"missing"), "missing game folder");
        Settings s;
        require(load_settings(root) == s, "default settings");
        s.stick_speed = 22;
        s.motion_speed = 110;
        s.smoothing_ms = 120;
        s.snap_steps = 1;
        s.legacy_axis = false;
        s.logging = true;
        save_settings(root, s);
        require(load_settings(root) == s, "settings round trip");
        auto bad = s;
        bad.stick_speed = 0;
        bool rejected = false;
        try {
            save_settings(root, bad);
        } catch (...) {
            rejected = true;
        }
        require(rejected && load_settings(root) == s, "invalid settings preserve saved file");
        const std::string json =
            R"({"state":"running","loaded":true,"enabled":true,"fault":false,"logging":true,"legacy_axis0":true,"aim_speed_percent":80,"aim_smoothing_ms":80,"snap_angle":22.5})";
        auto parsed = parse_session(json, true);
        require(parsed.loaded && parsed.enabled && !parsed.fault && parsed.snap_steps == 1 && parsed.logging,
                "input status parse including 22.5");
        parsed = parse_session(
            R"({"state":"running","loaded":true,"enabled":false,"fault":false,"logging":false})", false);
        require(!parsed.enabled && !parsed.logging, "disabled status and logging");
        rejected = false;
        try {
            parse_session("{}", false);
        } catch (...) {
            rejected = true;
        }
        require(rejected, "malformed status rejected");
        require(witness::input_config_path(root / L"out/dev/bin") == root / L"config/input.ini",
                "development settings location");
        touch(root / L"portable/WitnessVR.exe");
        require(witness::input_config_path(root / L"portable/runtime") == root / L"portable/config/input.ini",
                "portable settings location");
        auto disabled_log = witness::open_session_log({});
        require(!disabled_log->is_open() && !witness::write_session_log(*disabled_log, "ignored\n"),
                "disabled logging does not open a file");
        const auto log_path = root / L"bounded.log";
        auto bounded_log = witness::open_session_log(log_path);
        require(witness::write_session_log(*bounded_log, "record1\n", 16), "first bounded record");
        require(witness::write_session_log(*bounded_log, "record2\n", 16), "record exactly at cap");
        require(!witness::write_session_log(*bounded_log, "overflow\n", 16) && !bounded_log->is_open() &&
                    fs::file_size(log_path) == 16,
                "cap closes log without a partial record");
        std::ofstream append_log(log_path, std::ios::binary | std::ios::app);
        require(!witness::write_session_log(append_log, "overflow\n", 16) && fs::file_size(log_path) == 16,
                "append cannot exceed cap");
        std::cout << "Launcher backend checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
