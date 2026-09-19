#pragma once
#include "launcher/model.hpp"
#include <vector>

namespace witness::launcher {
struct GameModules {
    bool vr_ready{};
    bool input_loaded{};
    bool render_loaded{};
};

// Verifies the supported executable and live instructions before reading VR interface slots.
// Owned by the backend worker. Cached executable bytes are scoped to a process and folder.
class GameReadiness {
public:
    GameModules inspect(DWORD pid, const fs::path& game_dir, const fs::path& package_root);

    void reset() { verified_pid_ = 0; }

private:
    DWORD verified_pid_{};
    std::vector<unsigned char> verified_image_;
};

} // namespace witness::launcher
