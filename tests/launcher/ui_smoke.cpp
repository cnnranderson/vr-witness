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
        if (smoke_step == 0) {
            fs::create_directories(smoke_output);
            auto settings = this->settings();
            settings.stick_speed = 35;
            settings.motion_speed = 70;
            fill_settings(settings);
            command(ControlId::save);
            ++smoke_step;
            return;
        }
        if (smoke_step == 1) {
            const auto saved = load_settings(root);
            if (saved.stick_speed != 35 || saved.motion_speed != 70)
                throw std::runtime_error("UI save settings check failed");
            capture(smoke_output / L"status.bmp");
            command(ControlId::defaults);
            command(ControlId::save);
            TabCtrl_SetCurSel(tabs, 1);
            layout();
            RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            ++smoke_step;
            return;
        }
        if (!(load_settings(root) == Settings{}))
            throw std::runtime_error("UI reset defaults check failed");
        KillTimer(window, 1);
        RECT rect{};
        GetWindowRect(snap, &rect);
        std::ofstream details(smoke_output / L"ui-check.txt");
        details << "snap_visible=" << IsWindowVisible(snap)
                << " selection=" << SendMessageW(snap, CB_GETCURSEL, 0, 0) << " rect=" << rect.left << ","
                << rect.top << "," << rect.right << "," << rect.bottom << "\n";
        capture(smoke_output / L"settings.bmp");
        DestroyWindow(window);
    }
}
} // namespace witness::launcher
