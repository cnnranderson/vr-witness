#pragma once

#include "hands_math.hpp"
#include <cstdint>
#include <memory>

struct ID3D11Texture2D;

namespace witness::hands {

struct SceneDepth {
    ID3D11Texture2D* texture{};      // Borrowed for draw; never bound for native writes.
    float a{}, b{};                  // Native depth = a + b / positive eye-forward distance in meters.
    std::uint32_t width{}, height{}; // Active viewport at (0,0), inside texture bounds.
};

struct Frame {
    std::array<HandDraw, 2> hands{}; // Left, right. Invalid hands are independent.
    // Authored grip animation, not finger tracking: 0 open, 1 closed. Finite
    // values are clamped; a nonfinite value hides only the affected hand.
    std::array<float, 2> grip{};
    SceneDepth depth{}; // Optional experimental world occlusion; otherwise self-depth only.
};

enum class DrawResult { skipped, drawn, unsupported, failed };

// All calls, including destruction/reset, must be serialized with the game's
// immediate render context. Never reset from a worker while a hook can draw.
// D3D11.1 context state isolation is required; unsupported devices draw nothing.
// Private depth self-occludes the gloves. Optional world depth is copied and
// sampled separately; unsupported world depth leaves the gloves visible.
class Renderer {
public:
    Renderer() noexcept;
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    DrawResult draw(ID3D11Texture2D* color, const Frame& frame) noexcept;

    bool used_world_depth() const noexcept { return world_depth_used_; }

    void reset() noexcept;

private:
    struct Resources;
    std::unique_ptr<Resources> resources_;
    unsigned long thread_{};
    bool world_depth_used_{};
};

} // namespace witness::hands
