#include "launcher/ui/window.hpp"
#include "launcher/services/settings.hpp"
#include <fstream>

namespace witness::launcher {

void Window::capture(const fs::path& output) {
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

void Window::ui_smoke_tick() {
    if (!smoke_output.empty()) {
        const auto select_page = [&](int page) {
            // Drain the old page, then exercise the same notification as a real tab click.
            RedrawWindow(window, nullptr, nullptr, RDW_UPDATENOW | RDW_ALLCHILDREN);
            TabCtrl_SetCurSel(tabs, page);
            NMHDR changed{tabs, static_cast<UINT_PTR>(ControlId::tabs), TCN_SELCHANGE};
            SendMessageW(window, WM_NOTIFY, changed.idFrom, reinterpret_cast<LPARAM>(&changed));
            RECT client{};
            GetClientRect(window, &client);
            const auto invalid = CreateRectRgn(0, 0, 0, 0);
            const auto uncovered = CreateRectRgnIndirect(&client);
            const auto repaint = GetUpdateRgn(window, invalid, FALSE);
            const auto remainder = CombineRgn(uncovered, uncovered, invalid, RGN_DIFF);
            DeleteObject(uncovered);
            DeleteObject(invalid);
            if (repaint == ERROR || remainder != NULLREGION)
                throw std::runtime_error("Tab change did not invalidate the full page and old footer");
            // PrintWindow redraws into a fresh bitmap and would otherwise conceal stale screen pixels.
            UpdateWindow(window);
            RECT footer[3]{};
            const ControlId ids[]{ControlId::close_help, ControlId::logs, ControlId::hotkeys};
            for (unsigned i = 0; i < 3; ++i) {
                GetWindowRect(control(ids[i]), &footer[i]);
                MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&footer[i]), 2);
                if (!IsWindowVisible(control(ids[i])) || footer[i].left < 0 ||
                    footer[i].right > client.right || footer[i].top < scale(footer_top()) ||
                    footer[i].bottom > client.bottom)
                    throw std::runtime_error("Shared footer is hidden or clipped after changing tabs");
            }
            RECT overlap{};
            if (IntersectRect(&overlap, &footer[0], &footer[1]) ||
                IntersectRect(&overlap, &footer[0], &footer[2]) ||
                IntersectRect(&overlap, &footer[1], &footer[2]))
                throw std::runtime_error("Shared footer controls overlap after changing tabs");
            const std::vector<HWND>* pages[]{&status_widgets, &settings_widgets, &controls_widgets};
            for (int index = 0; index < 3; ++index) {
                for (const auto item : *pages[index]) {
                    if (!IsWindowVisible(item))
                        continue;
                    RECT rect{};
                    GetWindowRect(item, &rect);
                    MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&rect), 2);
                    if (index != page || rect.left < 0 || rect.right > client.right ||
                        rect.bottom > scale(footer_top()))
                        throw std::runtime_error(
                            "Tab content is visible on another page or overlaps its footer");
                }
            }
        };
        if (smoke_step == 0) {
            fs::create_directories(smoke_output);
            if (backend)
                throw std::runtime_error("UI fixtures must not start a game/session backend");
            SetWindowTextW(steam, L"Not running");
            SetWindowTextW(game, L"Not running");
            EnableWindow(control(ControlId::attach), FALSE);
            capture(smoke_output / L"startup.bmp");
            auto settings = this->settings();
            if (settings.show_hands ||
                SendMessageW(control(ControlId::show_hands), BM_GETCHECK, 0, 0) != BST_UNCHECKED)
                throw std::runtime_error("Hands must start disabled");
            if (GetDlgItem(window, 119) || GetDlgItem(window, 238))
                throw std::runtime_error("Retired world occlusion controls must not be created");
            settings.stick_speed = 35;
            settings.motion_speed = 70;
            settings.smooth_turn = true;
            settings.snap_steps = 0;
            settings.smooth_turn_speed = 90;
            fill_settings(settings);
            SendMessageW(control(ControlId::puzzle_reposition), BM_SETCHECK, BST_UNCHECKED, 0);
            command(ControlId::puzzle_reposition);
            if (!dirty || this->settings().puzzle_repositioning)
                throw std::runtime_error("Puzzle positioning checkbox did not mark edited settings");
            dirty = false;
            SendMessageW(control(ControlId::show_hands), BM_SETCHECK, BST_CHECKED, 0);
            command(ControlId::show_hands);
            if (!dirty || !this->settings().show_hands)
                throw std::runtime_error("Hands checkbox did not mark edited settings");
            command(ControlId::save);
            ++smoke_step;
            return;
        }
        if (smoke_step == 1) {
            const auto saved = load_settings(root);
            if (saved.stick_speed != 35 || saved.motion_speed != 70 || !saved.smooth_turn ||
                saved.snap_steps != 0 || saved.smooth_turn_speed != 90 || saved.puzzle_repositioning ||
                !saved.show_hands || !IsWindowEnabled(turn_speed))
                throw std::runtime_error("UI save settings check failed");
            capture(smoke_output / L"status.bmp");
            select_page(1);
            capture(smoke_output / L"smooth-settings.bmp");
            command(ControlId::defaults);
            command(ControlId::save);
            select_page(1);
            if (SendMessageW(controller, CB_GETCOUNT, 0, 0) != 2)
                throw std::runtime_error("Controller choices must be VR and Gamepad");
            SendMessageW(controller, CB_SETCURSEL, 1, 0);
            command(ControlId::controller_choice);
            if (SendMessageW(controller, CB_GETCURSEL, 0, 0) != 0)
                throw std::runtime_error("Unavailable controller was selectable");
            RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            ++smoke_step;
            return;
        }
        if (!(load_settings(root) == Settings{}) || IsWindowEnabled(turn_speed) ||
            SendMessageW(control(ControlId::puzzle_reposition), BM_GETCHECK, 0, 0) != BST_CHECKED ||
            SendMessageW(control(ControlId::show_hands), BM_GETCHECK, 0, 0) != BST_UNCHECKED)
            throw std::runtime_error("UI reset defaults check failed");
        KillTimer(window, 1);
        RECT rect{};
        GetWindowRect(snap, &rect);
        std::ofstream details(smoke_output / L"ui-check.txt");
        details << "snap_visible=" << IsWindowVisible(snap)
                << " selection=" << SendMessageW(snap, CB_GETCURSEL, 0, 0) << " rect=" << rect.left << ","
                << rect.top << "," << rect.right << "," << rect.bottom << "\n";
        capture(smoke_output / L"settings.bmp");
        select_page(2);
        if (TabCtrl_GetItemCount(tabs) != 3 || !IsWindowVisible(control(ControlId::controls_heading)) ||
            IsWindowVisible(stick) || IsWindowVisible(path) || IsWindowVisible(control(ControlId::cancel)))
            throw std::runtime_error("Controls tab visibility check failed");
        RECT client{};
        GetClientRect(window, &client);
        for (const auto item : controls_widgets) {
            GetWindowRect(item, &rect);
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&rect), 2);
            if (rect.left < 0 || rect.right > client.right || rect.bottom > scale(footer_top()))
                throw std::runtime_error("Control mapping extends outside the page");
        }
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        capture(smoke_output / L"controls.bmp");
        for (const int page : {0, 2, 1, 0})
            select_page(page);
        capture(smoke_output / L"status-return.bmp");
        details << "tab_repaint_and_footer_checks=passed\n";
        DestroyWindow(window);
    }
}
} // namespace witness::launcher
