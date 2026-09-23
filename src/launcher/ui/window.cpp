#include "launcher/ui/window.hpp"
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>
#ifdef WITNESS_UI_TEST
#include <fstream>
#endif

namespace witness::launcher {

Window::~Window() {
    backend.reset();
    DeleteObject(font);
    DeleteObject(bold_font);
    DeleteObject(background);
}

void Window::send(Action action, fs::path folder) {
    if (backend && !backend->request({action, std::move(folder), settings()}))
        MessageBeep(MB_OK);
}

void Window::browse() {
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

void Window::command(ControlId id) {
    switch (id) {
    case ControlId::browse:
        browse();
        break;
    case ControlId::detect:
        send(Action::detect_game);
        break;
    case ControlId::launch:
        send(Action::launch);
        break;
    case ControlId::attach:
        send(Action::toggle_attachment);
        break;
    case ControlId::cancel:
        send(Action::cancel);
        break;
    case ControlId::save:
        send(Action::apply);
        dirty = false;
        break;
    case ControlId::defaults:
        fill_settings(Settings{});
        dirty = true;
        break;
    case ControlId::controller_choice:
        // Native combo boxes have no per-item disabled state; reject unavailable choices.
        SendMessageW(controller, CB_SETCURSEL, 0, 0);
        break;
    case ControlId::snap_choice:
    case ControlId::logging:
        dirty = true;
        break;
    case ControlId::logs:
        fs::create_directories(root / L"logs");
        ShellExecuteW(window, L"open", (root / L"logs").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    default:
        break; // Labels and sliders do not dispatch button commands.
    }
}

LRESULT CALLBACK Window::window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* app = reinterpret_cast<Window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
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
            limits->ptMinTrackSize = {app->scale(740), app->tabs ? app->window_height() : app->scale(420)};
            if (app->tabs)
                limits->ptMaxTrackSize.y = limits->ptMinTrackSize.y;
            return 0;
        }
        case WM_MEASUREITEM: {
            auto* item = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
            if (item->CtlID == static_cast<UINT>(ControlId::controller_choice)) {
                item->itemHeight = app->scale(24);
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM: {
            auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
            if (item->CtlID != static_cast<UINT>(ControlId::controller_choice) || item->itemID == UINT(-1))
                break;
            const bool available = item->itemID == 0;
            const bool selected = available && (item->itemState & ODS_SELECTED);
            FillRect(item->hDC, &item->rcItem, GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
            SetBkMode(item->hDC, TRANSPARENT);
            SetTextColor(item->hDC, GetSysColor(!available ? COLOR_GRAYTEXT
                                                : selected ? COLOR_HIGHLIGHTTEXT
                                                           : COLOR_WINDOWTEXT));
            wchar_t text[128]{};
            SendMessageW(item->hwndItem, CB_GETLBTEXT, item->itemID, reinterpret_cast<LPARAM>(text));
            auto rect = item->rcItem;
            rect.left += app->scale(4);
            DrawTextW(item->hDC, text, -1, &rect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            if (available && (item->itemState & ODS_FOCUS))
                DrawFocusRect(item->hDC, &item->rcItem);
            return TRUE;
        }
        case WM_COMMAND:
            app->command(static_cast<ControlId>(LOWORD(wparam)));
            return 0;
        case WM_NOTIFY:
            if (reinterpret_cast<NMHDR*>(lparam)->idFrom == static_cast<UINT_PTR>(ControlId::tabs) &&
                reinterpret_cast<NMHDR*>(lparam)->code == TCN_SELCHANGE) {
                app->fit_page();
                app->layout();
                app->refresh();
            }
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
            const bool overall = reinterpret_cast<HWND>(lparam) == app->control(ControlId::overall_status);
            SetTextColor(dc, overall ? (app->active ? RGB(25, 120, 52) : RGB(180, 38, 38)) : RGB(34, 49, 65));
            SetBkColor(dc, RGB(246, 248, 251));
            return reinterpret_cast<LRESULT>(app->background);
        }
        case WM_ERASEBKGND: {
            RECT client{};
            GetClientRect(window, &client);
            FillRect(reinterpret_cast<HDC>(wparam), &client, app->background);
            return 1;
        }
#ifdef WITNESS_UI_TEST
        case WM_TIMER:
            app->ui_smoke_tick();
            return 0;
#endif
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
#ifdef WITNESS_UI_TEST
        std::ofstream error_file(app->smoke_output / L"error.txt");
        error_file << text;
        PostQuitMessage(1);
#else
        MessageBoxW(window, wide.c_str(), L"The Witness VR", MB_OK | MB_ICONERROR);
#endif
        if (message == WM_CREATE)
            return -1;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace witness::launcher
