#pragma once
#include <array>
#include <cstdint>

namespace witness {
// Render-thread snapshot; pointer values identify resources and are never dereferenced.
struct RenderSnapshot {
    std::uintptr_t current_target{};
    std::array<std::uintptr_t, 2> eye_targets{};
    std::uintptr_t scene_source{};
    float menu_fade{};
    std::array<float, 16> menu_matrix{};
};
}
