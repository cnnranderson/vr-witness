#pragma once
#include <filesystem>

namespace witness {
// Portable DLLs use the runner's config; development builds use the repository config.
inline std::filesystem::path input_config_path(const std::filesystem::path& dll_directory) {
    const auto portable_root = dll_directory.parent_path();
    const auto root = std::filesystem::exists(portable_root / L"WitnessVR.exe")
                          ? portable_root
                          : dll_directory.parent_path().parent_path().parent_path();
    return root / L"config" / L"input.ini";
}
} // namespace witness
