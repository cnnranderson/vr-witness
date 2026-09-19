#include "launcher/ui/window.hpp"
#include "launcher/resources/resources.h"
#include <algorithm>

namespace witness::launcher {

int Window::run(HINSTANCE instance, int show) {
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_TAB_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&common);
#ifdef WITNESS_UI_TEST
    const int initial_position = -30000;
    show = SW_SHOWNOACTIVATE;
#else
    const int initial_position = CW_USEDEFAULT;
#endif
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.hInstance = instance;
    cls.lpszClassName = L"WitnessVRPortable";
    cls.lpfnWndProc = Window::window_proc;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon =
        static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_WITNESS_VR), IMAGE_ICON,
                                      GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
    cls.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_WITNESS_VR), IMAGE_ICON,
                                                GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                                LR_SHARED));
    cls.hbrBackground = background;
    RegisterClassExW(&cls);
    const UINT system_dpi = GetDpiForSystem();
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    window = CreateWindowExW(
        0, cls.lpszClassName, L"Witness VR", WS_OVERLAPPEDWINDOW | WS_VSCROLL, initial_position,
        initial_position, std::min<int>(MulDiv(860, system_dpi, 96), work.right - work.left),
        std::min<int>(MulDiv(934, system_dpi, 96), work.bottom - work.top), nullptr, nullptr, instance, this);
    if (!window)
        return 1;
    ShowWindow(window, show);
    UpdateWindow(window);
#ifdef WITNESS_UI_TEST
    SetTimer(window, 1, 1200, nullptr);
#endif
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return static_cast<int>(message.wParam);
}

} // namespace witness::launcher
