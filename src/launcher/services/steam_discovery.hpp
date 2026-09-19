#pragma once
#include "launcher/model.hpp"
#include <vector>

namespace witness::launcher {
// Parse quoted VDF values, including escaped Windows paths.
std::vector<std::string> vdf_values(const std::string& text, const std::string& key);
fs::path find_game(const fs::path& steam_root);
fs::path steam_root();
bool valid_game_folder(const fs::path& folder);

} // namespace witness::launcher
