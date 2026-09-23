#include "launcher/ui/window.hpp"
#include "common/win_util.hpp"
#include <algorithm>

namespace witness::launcher {

int Window::scale(int value) const {
    return MulDiv(value, dpi, 96);
}

HWND Window::make(const wchar_t* cls, const wchar_t* text, ControlId id, DWORD style, Page page) {
    const auto control = CreateWindowExW(
        !wcscmp(cls, L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1,
        window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    if (!control)
        throw witness::win_error("Create launcher control");
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    if (page == Page::status)
        status_widgets.push_back(control);
    else if (page == Page::settings)
        settings_widgets.push_back(control);
    return control;
}

void Window::place(HWND control, int x, int y, int width, int height) {
    MoveWindow(control, scale(x), scale(y), scale(width), scale(height), TRUE);
}

HWND Window::control(ControlId id) const {
    return GetDlgItem(window, static_cast<int>(id));
}

void Window::label(const wchar_t* text, ControlId id, Page page) {
    make(L"STATIC", text, id, SS_LEFT, page);
}

void Window::create() {
    dpi = static_cast<int>(GetDpiForWindow(window));
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    RECT bounds{}, client{};
    GetWindowRect(window, &bounds);
    GetClientRect(window, &client);
    if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) {
        const int available =
            monitor.rcWork.bottom - monitor.rcWork.top - (bounds.bottom - bounds.top - client.bottom);
        // Fit the tallest page on the current display.
        dpi = std::min(dpi, std::max(72, MulDiv(available, 96, 546)));
    }
    font = CreateFontW(-scale(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                       CLEARTYPE_QUALITY, 0, L"Segoe UI");
    bold_font = CreateFontW(-scale(15), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                            CLEARTYPE_QUALITY, 0, L"Segoe UI");
    tabs = make(WC_TABCONTROLW, L"", ControlId::tabs, WS_TABSTOP, Page::shared);
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<wchar_t*>(L"  Status  ");
    TabCtrl_InsertItem(tabs, 0, &item);
    item.pszText = const_cast<wchar_t*>(L"  Settings  ");
    TabCtrl_InsertItem(tabs, 1, &item);
    create_status_page();
    create_settings_page();
    // Shared footer stays visible on both pages.
    label(L"Closing the launcher leaves attached fixes running.", ControlId::close_help, Page::shared);
    make(L"BUTTON", L"Open logs", ControlId::logs, WS_TABSTOP, Page::shared);
    label(L"F7: Calibrate height  |  Shift+F7: Game default  |  F8: Start/stop fixes", ControlId::hotkeys,
          Page::shared);
    backend = std::make_unique<Backend>(root, window);
    fill_settings(backend->snapshot().settings);
    layout();
    refresh();
}

void Window::layout() {
    RECT client{};
    GetClientRect(window, &client);
    const int width = MulDiv(client.right, 96, dpi);
    const bool settings_page = TabCtrl_GetCurSel(tabs) == 1;
    place(tabs, 24, 16, width - 48, 32);
    layout_status_page(width);
    layout_settings_page(width);
    for (HWND item : status_widgets)
        ShowWindow(item, settings_page ? SW_HIDE : SW_SHOW);
    for (HWND item : settings_widgets)
        ShowWindow(item, settings_page ? SW_SHOW : SW_HIDE);
    ShowWindow(control(ControlId::cancel),
               !settings_page && backend && backend->snapshot().pending ? SW_SHOW : SW_HIDE);
    const int footer = footer_top();
    place(control(ControlId::close_help), 24, footer + 4, width - 180, 24);
    place(control(ControlId::logs), width - 148, footer, 124, 28);
    place(control(ControlId::hotkeys), 24, footer + 38, width - 48, 24);
}

int Window::footer_top() const {
    return TabCtrl_GetCurSel(tabs) == 1 ? 466 : 432;
}

int Window::content_height() const {
    return footer_top() + 80;
}

int Window::window_height() const {
    RECT bounds{}, client{};
    GetWindowRect(window, &bounds);
    GetClientRect(window, &client);
    return scale(content_height()) + (bounds.bottom - bounds.top - client.bottom);
}

void Window::fit_page() {
    RECT bounds{};
    GetWindowRect(window, &bounds);
    const int height = window_height();
    int x = bounds.left, y = bounds.top;
#ifndef WITNESS_UI_TEST
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) {
        x = std::clamp<int>(
            x, monitor.rcWork.left,
            std::max<LONG>(monitor.rcWork.left, monitor.rcWork.right - (bounds.right - bounds.left)));
        y = std::clamp<int>(y, monitor.rcWork.top,
                            std::max<LONG>(monitor.rcWork.top, monitor.rcWork.bottom - height));
    }
#endif
    if (bounds.bottom - bounds.top != height || bounds.left != x || bounds.top != y)
        SetWindowPos(window, nullptr, x, y, bounds.right - bounds.left, height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
}
} // namespace witness::launcher
