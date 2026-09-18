#include "win_util.hpp"

#include "render_targets.hpp"
#include <iostream>

__declspec(noinline) bool render_pause_query() {
    // Keep a real call (no tail call) at the same site on every iteration.
    volatile bool result = TestPaused();
    return result;
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    render_pause_query(); // Prime the exact render caller before any injection.
    witness::Handle stop(CreateEventW(nullptr, TRUE, FALSE, argv[1]));
    const auto skip_name = std::wstring(argv[1]) + L"-SkipCursor";
    witness::Handle skip(CreateEventW(nullptr, TRUE, FALSE, skip_name.c_str()));
    const auto skip_menu_name = std::wstring(argv[1]) + L"-SkipMenu";
    witness::Handle skip_menu(CreateEventW(nullptr, TRUE, FALSE, skip_menu_name.c_str()));
    if (!stop || !skip || !skip_menu) return 3;
    const auto start = GetTickCount64();
    unsigned frames = 0, paused_redraws = 0, paused_skips = 0, pause_leaks = 0, unpaused_skips = 0;
    while (WaitForSingleObject(stop.get(), 0) == WAIT_TIMEOUT && GetTickCount64() - start < 90000) {
        const bool paused = (++frames % 2) != 0;
        fixture_paused.store(paused);
        if (TestPaused() != paused) ++pause_leaks; // Simulation/input-like caller.
        const bool skip_draw = render_pause_query();
        if (paused) { if (skip_draw) ++paused_skips; else ++paused_redraws; }
        else if (skip_draw) ++unpaused_skips;
        for (int eye = 0; eye != 2; ++eye) {
            TestBeginEye(eye);
            if (WaitForSingleObject(skip.get(), 0) != WAIT_OBJECT_0) TestCursor(nullptr, nullptr, false);
            // Decoys must not release the pending eye: mono, wrong eye, mirror.
            TestMenu(false, eye, false);
            TestMenu(true, 1 - eye, false);
            TestMenu(true, eye, true);
            if (WaitForSingleObject(skip_menu.get(), 0) != WAIT_OBJECT_0) TestMenu(true, eye, false);
        }
        stage = 0;
        TestSubmit(0); // Menu/transition path outside the hooked eye routine.
        TestMenu(false, 0, true);
        Sleep(5);
    }
    std::cout << "{\"before_cursor\":" << before_cursor.load() << ",\"after_cursor\":" << after_cursor.load()
              << ",\"outside\":" << outside.load() << ",\"menu_calls\":" << menu_calls.load()
              << ",\"after_menu\":" << after_menu.load()
              << ",\"paused_redraws\":" << paused_redraws << ",\"paused_skips\":" << paused_skips
              << ",\"pause_leaks\":" << pause_leaks << ",\"unpaused_skips\":" << unpaused_skips << "}\n";
    return submissions.load() && cursors.load() ? 0 : 4;
}
