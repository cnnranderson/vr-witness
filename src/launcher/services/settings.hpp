#pragma once
#include "launcher/model.hpp"

namespace witness::launcher {
bool valid_settings(const Settings& settings);
Settings load_settings(const fs::path& root);
void save_settings(const fs::path& root, const Settings& settings);
fs::path load_game_folder(const fs::path& root);
void save_game_folder(const fs::path& root, const fs::path& folder);

} // namespace witness::launcher
