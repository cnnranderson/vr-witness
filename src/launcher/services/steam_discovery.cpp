#include "launcher/services/steam_discovery.hpp"
#include "launcher/services/platform.hpp"
#include <regex>

namespace witness::launcher {
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

} // namespace witness::launcher
