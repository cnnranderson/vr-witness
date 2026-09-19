#include "launcher/ui/window.hpp"
#include "common/win_util.hpp"
#ifdef WITNESS_UI_TEST
#include <shellapi.h>
#endif

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    using namespace witness::launcher;
#ifndef WITNESS_UI_TEST
    witness::Handle mutex(CreateMutexW(nullptr, FALSE, L"Local\\WitnessVrPortableLauncher"));
    if (!mutex)
        return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"Witness VR is already open.", L"Witness VR", MB_OK);
        return 0;
    }
#endif
    Window window(fs::path(witness::module_path()).parent_path());
#ifdef WITNESS_UI_TEST
    int argc{};
    auto** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc != 2) {
        LocalFree(argv);
        return 1;
    }
    window.smoke_output = argv[1];
    LocalFree(argv);
#endif
    return window.run(instance, show);
}
