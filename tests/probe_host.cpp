#include "win_util.hpp"
#include <iostream>

// Deliberately small owned process for the cross-process smoke test. Never
// masquerades as the game and does not load Steam or any VR runtime.
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    witness::Handle stop(CreateEventW(nullptr, TRUE, FALSE, argv[1]));
    if (!stop) return 3;
    const DWORD wait = WaitForSingleObject(stop.get(), 60000);
    return wait == WAIT_OBJECT_0 ? 0 : 4;
}
