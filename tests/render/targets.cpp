#include "targets.hpp"
#include "render/snapshot.hpp"
#include "common/caller_address.hpp"
#include <limits>

extern "C" {
__declspec(dllexport) void* TestPauseRenderReturn{};
__declspec(dllexport) witness::RenderSnapshot TestRenderState{
    0,
    {0x1110, 0x2220},
    0x3330,
    1,
    {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, std::numeric_limits<float>::quiet_NaN()}};
}

std::atomic<unsigned> submissions{0}, cursors{0}, before_cursor{0}, after_cursor{0}, outside{0};
int stage = 0;
std::atomic<unsigned> menu_calls{0};
std::atomic<unsigned> after_menu{0};
int current_eye = -1;
std::atomic<bool> fixture_paused{false};

extern "C" __declspec(dllexport) __declspec(noinline) bool TestPaused() {
    // The host primes this once via its render-query wrapper before injection.
    // Subsequent calls (including calls through the detour) cannot change it.
    if (!TestPauseRenderReturn)
        TestPauseRenderReturn = WITNESS_RETURN_ADDRESS();
    return fixture_paused.load();
}

// Compile separately from the caller so it cannot assume private register usage
// across a patched function. Hook boundaries must obey the Windows x64 ABI.
extern "C" __declspec(dllexport) __declspec(noinline) void TestSubmit(int eye) {
    submissions.fetch_add(static_cast<unsigned>(eye) + 1, std::memory_order_relaxed);
    if (stage == 1)
        ++before_cursor;
    if (stage == 2)
        ++after_cursor;
    if (stage == 3)
        ++after_menu;
    if (stage == 0)
        ++outside;
}

extern "C" __declspec(dllexport) __declspec(noinline) bool TestBeginEye(int eye) {
    stage = 1;
    current_eye = eye;
    TestRenderState.current_target = TestRenderState.eye_targets[eye];
    TestSubmit(eye);
    return true;
}

extern "C" __declspec(dllexport) __declspec(noinline) void TestCursor(void*, void*, bool) {
    cursors.fetch_add(1, std::memory_order_relaxed);
    stage = 2;
}

extern "C" __declspec(dllexport) __declspec(noinline) void TestMenu(bool stereo, int eye, bool mirror) {
    if ((stereo && eye >= 0 && eye <= 1) || (!stereo && eye == 0 && mirror))
        ++menu_calls;
    if (stereo && !mirror && eye == current_eye)
        stage = 3;
}
