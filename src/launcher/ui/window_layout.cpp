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
    MoveWindow(control, scale(x), scale(y - scroll), scale(width), scale(height), TRUE);
}

HWND Window::control(ControlId id) const {
    return GetDlgItem(window, static_cast<int>(id));
}

void Window::label(const wchar_t* text, ControlId id, Page page) {
    make(L"STATIC", text, id, SS_LEFT, page);
}

void Window::create() {
    dpi = static_cast<int>(GetDpiForWindow(window));
    font = CreateFontW(-scale(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                       CLEARTYPE_QUALITY, 0, L"Segoe UI");
    title_font = CreateFontW(-scale(28), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                             CLEARTYPE_QUALITY, 0, L"Segoe UI");
    bold_font = CreateFontW(-scale(15), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                            CLEARTYPE_QUALITY, 0, L"Segoe UI");
    label(L"Witness VR", ControlId::title, Page::shared);
    SendMessageW(control(ControlId::title), WM_SETFONT, reinterpret_cast<WPARAM>(title_font), TRUE);
    label(L"Portable VR launcher", ControlId::subtitle, Page::shared);
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
    label(L"", ControlId::message, Page::shared);
    message = control(ControlId::message);
    label(L"Closing this window leaves the game and VR fixes running.", ControlId::close_help, Page::shared);
    make(L"BUTTON", L"Open logs", ControlId::logs, WS_TABSTOP, Page::shared);
    label(L"F7: controls   |   F8: visuals   |   F9: stop fixes", ControlId::hotkeys, Page::shared);
    backend = std::make_unique<Backend>(root, window);
    fill_settings(backend->snapshot().settings);
    layout();
    refresh();
}

void Window::layout() {
    RECT client{};
    GetClientRect(window, &client);
    const int width = MulDiv(client.right, 96, dpi);
    place(control(ControlId::title), 28, 18, width - 56, 40);
    place(control(ControlId::subtitle), 30, 62, width - 60, 24);
    place(tabs, 28, 98, width - 56, 32);
    layout_status_page(width);
    layout_settings_page(width);
    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = 856;
    info.nPage = MulDiv(client.bottom, 96, dpi);
    info.nPos = scroll;
    SetScrollInfo(window, SB_VERT, &info, TRUE);
    const bool settings_page = TabCtrl_GetCurSel(tabs) == 1;
    for (HWND item : status_widgets)
        ShowWindow(item, settings_page ? SW_HIDE : SW_SHOW);
    for (HWND item : settings_widgets)
        ShowWindow(item, settings_page ? SW_SHOW : SW_HIDE);
    const int footer = 706;
    place(message, 32, footer, width - 64, 66);
    place(control(ControlId::close_help), 32, footer + 78, width - 208, 26);
    place(control(ControlId::logs), width - 156, footer + 74, 124, 30);
    place(control(ControlId::hotkeys), 32, footer + 112, width - 64, 25);
}

} // namespace witness::launcher
