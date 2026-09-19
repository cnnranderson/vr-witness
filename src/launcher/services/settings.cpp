#include "launcher/services/settings.hpp"
#include "common/win_util.hpp"
#include <fstream>

namespace witness::launcher {
namespace {
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
} // namespace

bool Settings::operator==(const Settings& other) const {
    return stick_speed == other.stick_speed && motion_speed == other.motion_speed &&
           smoothing_ms == other.smoothing_ms && snap_steps == other.snap_steps &&
           legacy_axis == other.legacy_axis && logging == other.logging;
}

bool valid_settings(const Settings& settings) {
    return settings.stick_speed >= 10 && settings.stick_speed <= 300 && settings.motion_speed >= 10 &&
           settings.motion_speed <= 300 && settings.smoothing_ms >= 0 && settings.smoothing_ms <= 250 &&
           (settings.snap_steps == 0 || settings.snap_steps == 1 || settings.snap_steps == 2 ||
            settings.snap_steps == 4);
}

Settings load_settings(const fs::path& root) {
    const auto file = root / L"config" / L"input.ini";
    Settings settings;
    const int stick = setting_number(file, L"StickCursorSpeedPercent", 20);
    const int motion = setting_number(file, L"MotionCursorSpeedPercent", 60);
    const int smooth = setting_number(file, L"AimSmoothingMs", 80);
    const int snap = setting_number(file, L"SnapSteps", 1);
    if (stick >= 10 && stick <= 300)
        settings.stick_speed = stick;
    if (motion >= 10 && motion <= 300)
        settings.motion_speed = motion;
    if (smooth >= 0 && smooth <= 250)
        settings.smoothing_ms = smooth;
    if (snap == 0 || snap == 1 || snap == 2 || snap == 4)
        settings.snap_steps = snap;
    settings.legacy_axis = setting_number(file, L"LegacyAxis0", 1) != 0;
    settings.logging = setting_number(file, L"DiagnosticLogging", 0) == 1;
    return settings;
}

void save_settings(const fs::path& root, const Settings& settings) {
    if (!valid_settings(settings))
        throw std::runtime_error("Settings are outside supported ranges.");
    const auto file = root / L"config" / L"input.ini";
    write_ini(file, L"Input", L"StickCursorSpeedPercent", std::to_wstring(settings.stick_speed));
    write_ini(file, L"Input", L"MotionCursorSpeedPercent", std::to_wstring(settings.motion_speed));
    write_ini(file, L"Input", L"AimSmoothingMs", std::to_wstring(settings.smoothing_ms));
    write_ini(file, L"Input", L"SnapSteps", std::to_wstring(settings.snap_steps));
    write_ini(file, L"Input", L"LegacyAxis0", settings.legacy_axis ? L"1" : L"0");
    write_ini(file, L"Input", L"DiagnosticLogging", settings.logging ? L"1" : L"0");
}

fs::path load_game_folder(const fs::path& root) {
    return read_ini(root / L"config" / L"launcher.ini", L"Launcher", L"GameDirectory");
}

void save_game_folder(const fs::path& root, const fs::path& folder) {
    write_ini(root / L"config" / L"launcher.ini", L"Launcher", L"GameDirectory", folder.wstring());
}

} // namespace witness::launcher
