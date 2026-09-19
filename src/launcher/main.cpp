#include "backend.hpp"
#include "resources.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>
#include <fstream>
#include <memory>

namespace {
using namespace witness::launcher;
constexpr int tab_id = 100, browse_id = 101, detect_id = 102, launch_id = 103, attach_id = 104, stop_id = 105,
              cancel_id = 106, save_id = 107, defaults_id = 108, logs_id = 109, stick_id = 110,
              motion_id = 111, smooth_id = 112, snap_id = 113, legacy_id = 114, logging_id = 115;
struct App {
    HWND window{}, tabs{}, path{}, message{}, game{}, steam{}, vr{}, render{}, input{};
    HWND stick{}, motion{}, smooth{}, snap{}, legacy{}, logging{}, stick_value{}, motion_value{},
        smooth_value{};
    HFONT font{}, title_font{}, bold_font{};
    HBRUSH background{CreateSolidBrush(RGB(246, 248, 251))};
    std::vector<HWND> status_widgets, settings_widgets, action_buttons;
    std::unique_ptr<Backend> backend;
    fs::path root, smoke_output;
    int dpi{96};
    int scroll{};
    int smoke_step{};
    bool settings_initialized{}, dirty{};
    ~App() {
        backend.reset();
        DeleteObject(font);
        DeleteObject(title_font);
        DeleteObject(bold_font);
        DeleteObject(background);
    }
    int scale(int value) const { return MulDiv(value, dpi, 96); }
    HWND make(const wchar_t* cls, const wchar_t* text, int id, DWORD style = 0, bool settings = false) {
        const auto control = CreateWindowExW(
            !wcscmp(cls, L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 1,
            1, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        if (!control)
            throw witness::win_error("Create launcher control");
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        (settings ? settings_widgets : status_widgets).push_back(control);
        return control;
    }
    void place(HWND control, int x, int y, int width, int height) {
        MoveWindow(control, scale(x), scale(y - scroll), scale(width), scale(height), TRUE);
    }
    HWND control(int id) const { return GetDlgItem(window, id); }
    void label(const wchar_t* text, int id, bool settings = false) {
        make(L"STATIC", text, id, SS_LEFT, settings);
    }
    void fill_settings(const Settings& s) {
        SendMessageW(stick, TBM_SETPOS, TRUE, s.stick_speed);
        SendMessageW(motion, TBM_SETPOS, TRUE, s.motion_speed);
        SendMessageW(smooth, TBM_SETPOS, TRUE, s.smoothing_ms);
        const int index = s.snap_steps == 0 ? 0 : s.snap_steps == 1 ? 1 : s.snap_steps == 2 ? 2 : 3;
        SendMessageW(snap, CB_SETCURSEL, index, 0);
        SendMessageW(legacy, BM_SETCHECK, s.legacy_axis ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(logging, BM_SETCHECK, s.logging ? BST_CHECKED : BST_UNCHECKED, 0);
        update_values();
    }
    Settings settings() const {
        Settings s;
        s.stick_speed = static_cast<int>(SendMessageW(stick, TBM_GETPOS, 0, 0));
        s.motion_speed = static_cast<int>(SendMessageW(motion, TBM_GETPOS, 0, 0));
        s.smoothing_ms = static_cast<int>(SendMessageW(smooth, TBM_GETPOS, 0, 0));
        const int index = static_cast<int>(SendMessageW(snap, CB_GETCURSEL, 0, 0));
        s.snap_steps = index == 0 ? 0 : index == 1 ? 1 : index == 2 ? 2 : 4;
        s.legacy_axis = SendMessageW(legacy, BM_GETCHECK, 0, 0) == BST_CHECKED;
        s.logging = SendMessageW(logging, BM_GETCHECK, 0, 0) == BST_CHECKED;
        return s;
    }
    void update_values() {
        const auto s = settings();
        SetWindowTextW(stick_value, (std::to_wstring(s.stick_speed) + L"%").c_str());
        SetWindowTextW(motion_value, (std::to_wstring(s.motion_speed) + L"%").c_str());
        SetWindowTextW(smooth_value, (std::to_wstring(s.smoothing_ms) + L" ms").c_str());
    }
    void create() {
        dpi = static_cast<int>(GetDpiForWindow(window));
        font = CreateFontW(-scale(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                           CLEARTYPE_QUALITY, 0, L"Segoe UI");
        title_font = CreateFontW(-scale(28), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                                 CLEARTYPE_QUALITY, 0, L"Segoe UI");
        bold_font = CreateFontW(-scale(15), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                                CLEARTYPE_QUALITY, 0, L"Segoe UI");
        label(L"Witness VR", 200);
        SendMessageW(control(200), WM_SETFONT, reinterpret_cast<WPARAM>(title_font), TRUE);
        label(L"Portable VR launcher", 201);
        tabs = make(WC_TABCONTROLW, L"", tab_id, WS_TABSTOP);
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(L"  Status  ");
        TabCtrl_InsertItem(tabs, 0, &item);
        item.pszText = const_cast<wchar_t*>(L"  Settings  ");
        TabCtrl_InsertItem(tabs, 1, &item);
        label(L"Game installation", 202);
        path = make(L"EDIT", L"", 203, ES_AUTOHSCROLL | ES_READONLY | WS_TABSTOP);
        make(L"BUTTON", L"Browse...", browse_id, WS_TABSTOP);
        make(L"BUTTON", L"Find in Steam", detect_id, WS_TABSTOP);
        label(L"Select the folder containing The Witness, or let Steam detection find it.", 204);
        for (const auto& pair :
             std::vector<std::pair<int, const wchar_t*>>{{launch_id, L"Launch VR"},
                                                         {attach_id, L"Attach to running game"},
                                                         {stop_id, L"Stop fixes"},
                                                         {cancel_id, L"Cancel wait"}})
            action_buttons.push_back(make(L"BUTTON", pair.second, pair.first, WS_TABSTOP));
        label(L"Session status", 205);
        SendMessageW(control(205), WM_SETFONT, reinterpret_cast<WPARAM>(bold_font), TRUE);
        label(L"SteamVR", 206);
        steam = make(L"STATIC", L"Checking...", 207);
        label(L"The Witness", 208);
        game = make(L"STATIC", L"Checking...", 209);
        label(L"Native VR", 210);
        vr = make(L"STATIC", L"Waiting for game", 211);
        label(L"Headset visuals", 212);
        render = make(L"STATIC", L"Not attached", 213);
        label(L"Controller input", 214);
        input = make(L"STATIC", L"Not attached", 215);
        label(L"Cursor speed", 220, true);
        SendMessageW(control(220), WM_SETFONT, reinterpret_cast<WPARAM>(bold_font), TRUE);
        label(L"Lower values are slower. Speeds are a percentage of the view width per second.", 221, true);
        label(L"Analog stick", 222, true);
        stick = make(TRACKBAR_CLASSW, L"", stick_id, TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, true);
        stick_value = make(L"STATIC", L"20%", 223, SS_RIGHT, true);
        label(L"Controller pointing", 224, true);
        motion = make(TRACKBAR_CLASSW, L"", motion_id, TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, true);
        motion_value = make(L"STATIC", L"60%", 225, SS_RIGHT, true);
        for (auto slider : {stick, motion}) {
            SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(10, 300));
            SendMessageW(slider, TBM_SETPAGESIZE, 0, 10);
        }
        label(L"Pointing smoothing", 226, true);
        smooth = make(TRACKBAR_CLASSW, L"", smooth_id, TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, true);
        SendMessageW(smooth, TBM_SETRANGE, TRUE, MAKELPARAM(0, 250));
        SendMessageW(smooth, TBM_SETPAGESIZE, 0, 10);
        smooth_value = make(L"STATIC", L"80 ms", 227, SS_RIGHT, true);
        label(L"More smoothing reduces jitter and adds a little delay.", 228, true);
        label(L"Snap turning", 229, true);
        snap = make(L"COMBOBOX", L"", snap_id, CBS_DROPDOWNLIST | WS_TABSTOP, true);
        for (const auto* value : {L"Off", L"22.5 degrees", L"45 degrees", L"90 degrees"})
            SendMessageW(snap, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
        legacy = make(L"BUTTON", L"Knuckles legacy binding compatibility", legacy_id,
                      BS_AUTOCHECKBOX | WS_TABSTOP, true);
        label(L"Keep this enabled for the tested Knuckles controls.", 230, true);
        logging = make(L"BUTTON", L"Diagnostic logging", logging_id, BS_AUTOCHECKBOX | WS_TABSTOP, true);
        label(L"Off by default. For troubleshooting; each log is limited to 2 MiB.", 232, true);
        make(L"BUTTON", L"Save settings", save_id, WS_TABSTOP, true);
        make(L"BUTTON", L"Reset defaults", defaults_id, WS_TABSTOP, true);
        label(L"Speeds update live. Other settings briefly restart the affected VR fixes.", 231, true);
        // Shared footer stays visible on both pages.
        label(L"", 240);
        message = control(240);
        label(L"Closing this window leaves the game and VR fixes running.", 241);
        make(L"BUTTON", L"Open logs", logs_id, WS_TABSTOP);
        label(L"F7: controls   |   F8: visuals   |   F9: stop fixes", 242);
        backend = std::make_unique<Backend>(root, window);
        fill_settings(backend->snapshot().settings);
        settings_initialized = true;
        layout();
        refresh();
    }
    void layout() {
        RECT client{};
        GetClientRect(window, &client);
        const int width = MulDiv(client.right, 96, dpi);
        place(control(200), 28, 18, width - 56, 40);
        place(control(201), 30, 62, width - 60, 24);
        place(tabs, 28, 98, width - 56, 32);
        place(control(202), 32, 148, width - 64, 24);
        place(path, 32, 178, width - 302, 30);
        place(control(browse_id), width - 258, 178, 98, 30);
        place(control(detect_id), width - 150, 178, 118, 30);
        place(control(204), 32, 216, width - 64, 38);
        place(control(launch_id), 32, 264, 134, 38);
        place(control(attach_id), 178, 264, 206, 38);
        place(control(stop_id), 396, 264, 116, 38);
        place(control(cancel_id), 524, 264, 120, 38);
        place(control(205), 32, 324, width - 64, 28);
        for (int row = 0; row < 5; ++row) {
            place(control(206 + row * 2), 40, 364 + row * 32, 170, 25);
            place(control(207 + row * 2), 218, 364 + row * 32, width - 258, 25);
        }
        place(control(220), 32, 148, width - 64, 26);
        place(control(221), 32, 180, width - 64, 30);
        place(control(222), 40, 232, 170, 26);
        place(stick, 218, 226, width - 358, 36);
        place(stick_value, width - 124, 232, 84, 26);
        place(control(224), 40, 284, 174, 26);
        place(motion, 218, 278, width - 358, 36);
        place(motion_value, width - 124, 284, 84, 26);
        place(control(226), 40, 336, 174, 26);
        place(smooth, 218, 330, width - 358, 36);
        place(smooth_value, width - 124, 336, 84, 26);
        place(control(228), 218, 370, width - 258, 28);
        place(control(229), 40, 414, 174, 26);
        place(snap, 218, 408, 180, 160);
        place(legacy, 40, 458, width - 80, 26);
        place(control(230), 62, 488, width - 102, 25);
        place(logging, 40, 526, width - 80, 26);
        place(control(232), 62, 556, width - 102, 25);
        place(control(save_id), 40, 596, 148, 34);
        place(control(defaults_id), 200, 596, 142, 34);
        place(control(231), 40, 640, width - 80, 38);
        SCROLLINFO info{};
        info.cbSize = sizeof(info);
        info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        info.nMin = 0;
        info.nMax = 856;
        info.nPage = MulDiv(client.bottom, 96, dpi);
        info.nPos = scroll;
        SetScrollInfo(window, SB_VERT, &info, TRUE);
        const bool settings_page = TabCtrl_GetCurSel(tabs) == 1;
        for (HWND item : status_widgets) {
            const int id = GetDlgCtrlID(item);
            const bool shared = id == 200 || id == 201 || id == tab_id || id >= 240 || id == logs_id;
            ShowWindow(item, shared || !settings_page ? SW_SHOW : SW_HIDE);
        }
        for (HWND item : settings_widgets)
            ShowWindow(item, settings_page ? SW_SHOW : SW_HIDE);
        const int footer = 706;
        place(message, 32, footer, width - 64, 66);
        place(control(241), 32, footer + 78, width - 208, 26);
        place(control(logs_id), width - 156, footer + 74, 124, 30);
        place(control(242), 32, footer + 112, width - 64, 25);
    }
    static std::wstring session_text(const Session& s, bool uncertain) {
        if (uncertain)
            return L"Status uncertain - restart game before retrying";
        if (s.fault || s.state == L"failed")
            return L"Fault - restart game";
        if (!s.loaded)
            return L"Not attached";
        if (s.state == L"running")
            return s.enabled ? L"Attached and active" : L"Attached, disabled by hotkey";
        return s.state == L"stopped" ? L"Stopped (DLL remains loaded until game exit)" : s.state;
    }
    void refresh() {
        if (!backend)
            return;
        const auto s = backend->snapshot();
        SetWindowTextW(path, s.game_dir.c_str());
        SetWindowTextW(steam, s.steamvr ? L"Running" : L"Not running");
        SetWindowTextW(game, s.pid ? (L"Running  |  PID " + std::to_wstring(s.pid)).c_str() : L"Not running");
        SetWindowTextW(vr, s.vr_ready ? L"Ready"
                           : s.pid    ? L"Not ready - check headset / VR launch"
                                      : L"Waiting for game");
        SetWindowTextW(render, session_text(s.render, s.uncertain).c_str());
        SetWindowTextW(input, session_text(s.input, s.uncertain).c_str());
        SetWindowTextW(message, s.message.c_str());
        for (auto button : action_buttons)
            EnableWindow(button, !s.busy);
        EnableWindow(control(launch_id), !s.busy && !s.pending);
        EnableWindow(control(attach_id), !s.busy && !s.pending && s.pid && !s.uncertain);
        EnableWindow(control(stop_id), !s.busy && !s.uncertain && (s.render.loaded || s.input.loaded));
        EnableWindow(control(cancel_id), !s.busy && s.pending);
        for (int id : {browse_id, detect_id, save_id})
            EnableWindow(control(id), !s.busy && !s.pending);
        if (!dirty && settings_initialized) {
            fill_settings(s.settings);
        }
    }
    void send(Action action, fs::path folder = {}) {
        if (backend && !backend->request({action, std::move(folder), settings()}))
            SetWindowTextW(message, L"Finishing the current operation. Please try again in a moment.");
    }
    void browse() {
        wchar_t file[32768]{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = window;
        dialog.lpstrFilter =
            L"The Witness executables\0witness_d3d11.exe;witness64_d3d11.exe\0Executable files\0*.exe\0";
        dialog.lpstrFile = file;
        dialog.nMaxFile = 32768;
        dialog.lpstrTitle = L"Select The Witness executable";
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&dialog))
            send(Action::select_game, fs::path(file).parent_path());
    }
    void command(int id) {
        switch (id) {
        case browse_id:
            browse();
            break;
        case detect_id:
            send(Action::detect_game);
            break;
        case launch_id:
            send(Action::launch);
            break;
        case attach_id:
            send(Action::attach);
            break;
        case stop_id:
            send(Action::stop);
            break;
        case cancel_id:
            send(Action::cancel);
            break;
        case save_id:
            send(Action::apply);
            dirty = false;
            break;
        case defaults_id:
            fill_settings(Settings{});
            dirty = true;
            break;
        case snap_id:
        case legacy_id:
        case logging_id:
            dirty = true;
            break;
        case logs_id:
            fs::create_directories(root / L"logs");
            ShellExecuteW(window, L"open", (root / L"logs").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
    }
    // Capture this app's own window for UI regression review; never capture the desktop.
    void capture(const fs::path& output) {
        RECT bounds{};
        GetWindowRect(window, &bounds);
        const int w = bounds.right - bounds.left, h = bounds.bottom - bounds.top;
        HDC screen = GetDC(window);
        HDC memory = CreateCompatibleDC(screen);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels{};
        HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        HGDIOBJ old = SelectObject(memory, bitmap);
        PrintWindow(window, memory, 0);
        BITMAPFILEHEADER header{};
        header.bfType = 0x4d42;
        header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
        header.bfSize = header.bfOffBits + w * h * 4;
        std::ofstream file(output, std::ios::binary);
        file.write(reinterpret_cast<char*>(&header), sizeof(header));
        file.write(reinterpret_cast<char*>(&info.bmiHeader), sizeof(BITMAPINFOHEADER));
        file.write(static_cast<char*>(pixels), w * h * 4);
        SelectObject(memory, old);
        DeleteObject(bitmap);
        DeleteDC(memory);
        ReleaseDC(window, screen);
    }
};
LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        app->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app)
        return DefWindowProcW(window, message, wparam, lparam);
    try {
        switch (message) {
        case WM_CREATE:
            app->create();
            return 0;
        case WM_SIZE:
            if (app->tabs)
                app->layout();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
            limits->ptMinTrackSize = {app->scale(780), app->scale(540)};
            return 0;
        }
        case WM_COMMAND:
            app->command(LOWORD(wparam));
            return 0;
        case WM_NOTIFY:
            if (reinterpret_cast<NMHDR*>(lparam)->idFrom == tab_id &&
                reinterpret_cast<NMHDR*>(lparam)->code == TCN_SELCHANGE)
                app->layout();
            return 0;
        case WM_VSCROLL: {
            SCROLLINFO info{};
            info.cbSize = sizeof(info);
            info.fMask = SIF_ALL;
            GetScrollInfo(window, SB_VERT, &info);
            int next = info.nPos;
            switch (LOWORD(wparam)) {
            case SB_LINEUP:
                next -= 24;
                break;
            case SB_LINEDOWN:
                next += 24;
                break;
            case SB_PAGEUP:
                next -= info.nPage;
                break;
            case SB_PAGEDOWN:
                next += info.nPage;
                break;
            case SB_THUMBTRACK:
                next = info.nTrackPos;
                break;
            }
            app->scroll = std::max(0, std::min(next, info.nMax - static_cast<int>(info.nPage) + 1));
            app->layout();
            InvalidateRect(window, nullptr, TRUE);
            return 0;
        }
        case WM_MOUSEWHEEL:
            SendMessageW(window, WM_VSCROLL, GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? SB_LINEUP : SB_LINEDOWN, 0);
            return 0;
        case WM_HSCROLL:
            app->dirty = true;
            app->update_values();
            return 0;
        case status_message:
            app->refresh();
            return 0;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetTextColor(dc, RGB(34, 49, 65));
            SetBkColor(dc, RGB(246, 248, 251));
            return reinterpret_cast<LRESULT>(app->background);
        }
        case WM_ERASEBKGND: {
            RECT client{};
            GetClientRect(window, &client);
            FillRect(reinterpret_cast<HDC>(wparam), &client, app->background);
            return 1;
        }
        case WM_TIMER:
            if (!app->smoke_output.empty()) {
                if (app->smoke_step == 0) {
                    fs::create_directories(app->smoke_output);
                    auto settings = app->settings();
                    settings.stick_speed = 35;
                    settings.motion_speed = 70;
                    app->fill_settings(settings);
                    app->command(save_id);
                    ++app->smoke_step;
                    return 0;
                }
                if (app->smoke_step == 1) {
                    const auto saved = load_settings(app->root);
                    if (saved.stick_speed != 35 || saved.motion_speed != 70)
                        throw std::runtime_error("UI save settings check failed");
                    app->capture(app->smoke_output / L"status.bmp");
                    app->command(defaults_id);
                    app->command(save_id);
                    TabCtrl_SetCurSel(app->tabs, 1);
                    app->layout();
                    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
                    ++app->smoke_step;
                    return 0;
                }
                if (!(load_settings(app->root) == Settings{}))
                    throw std::runtime_error("UI reset defaults check failed");
                KillTimer(window, 1);
                RECT rect{};
                GetWindowRect(app->snap, &rect);
                std::ofstream details(app->smoke_output / L"ui-check.txt");
                details << "snap_visible=" << IsWindowVisible(app->snap)
                        << " selection=" << SendMessageW(app->snap, CB_GETCURSEL, 0, 0)
                        << " rect=" << rect.left << "," << rect.top << "," << rect.right << "," << rect.bottom
                        << "\n";
                app->capture(app->smoke_output / L"settings.bmp");
                DestroyWindow(window);
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
    } catch (const std::exception& error) {
        std::string text = error.what();
        std::wstring wide(text.begin(), text.end());
        MessageBoxW(window, wide.c_str(), L"Witness VR", MB_OK | MB_ICONERROR);
        if (message == WM_CREATE)
            return -1;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_TAB_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&common);
    witness::Handle mutex(CreateMutexW(nullptr, FALSE, L"Local\\WitnessVrPortableLauncher"));
    if (!mutex)
        return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"Witness VR is already open.", L"Witness VR", MB_OK);
        return 0;
    }
    App app;
    app.root = fs::path(witness::module_path()).parent_path();
    int argc{};
    auto** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc == 3 && !wcscmp(argv[1], L"--ui-smoke-test"))
        app.smoke_output = argv[2];
    if (argv)
        LocalFree(argv);
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.hInstance = instance;
    cls.lpszClassName = L"WitnessVRPortable";
    cls.lpfnWndProc = window_proc;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon =
        static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_WITNESS_VR), IMAGE_ICON,
                                      GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
    cls.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_WITNESS_VR), IMAGE_ICON,
                                                GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                                LR_SHARED));
    cls.hbrBackground = app.background;
    RegisterClassExW(&cls);
    const UINT dpi = GetDpiForSystem();
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    HWND window = CreateWindowExW(
        0, cls.lpszClassName, L"Witness VR", WS_OVERLAPPEDWINDOW | WS_VSCROLL,
        app.smoke_output.empty() ? CW_USEDEFAULT : -30000, app.smoke_output.empty() ? CW_USEDEFAULT : -30000,
        std::min<int>(MulDiv(860, dpi, 96), work.right - work.left),
        std::min<int>(MulDiv(934, dpi, 96), work.bottom - work.top), nullptr, nullptr, instance, &app);
    if (!window)
        return 1;
    ShowWindow(window, app.smoke_output.empty() ? show : SW_SHOWNOACTIVATE);
    UpdateWindow(window);
    if (!app.smoke_output.empty())
        SetTimer(window, 1, 1200, nullptr);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return static_cast<int>(message.wParam);
}
