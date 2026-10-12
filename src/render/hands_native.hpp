#pragma once
#include "render/hands_draw.hpp"
#include <cstdint>
#include <memory>

namespace witness::hands {
// Borrows the game's existing VR session. Calls are serialized on its render
// thread; destroy only after render callbacks drain. Never owns VR shutdown.
class NativeHands {
public:
    explicit NativeHands(std::uintptr_t image_base);
    ~NativeHands();
    // CPU-only reset between sessions, after all previous callbacks have drained.
    void new_session(bool world_occlusion) noexcept;
    void begin_eye(int eye) noexcept;
    DrawResult draw(int eye, const void* view) noexcept;
    bool occlusion_active() const noexcept;
    std::uint64_t occlusion_draws() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace witness::hands
