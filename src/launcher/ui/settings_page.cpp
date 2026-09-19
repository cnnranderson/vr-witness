#include "launcher/ui/window.hpp"

namespace witness::launcher {

void Window::create_settings_page() {
    label(L"Cursor speed", ControlId::cursor_heading, Page::settings);
    SendMessageW(control(ControlId::cursor_heading), WM_SETFONT, reinterpret_cast<WPARAM>(bold_font), TRUE);
    label(L"Lower values are slower. Speeds are a percentage of the view width per second.",
          ControlId::cursor_help, Page::settings);
    label(L"Analog stick", ControlId::stick_label, Page::settings);
    stick = make(TRACKBAR_CLASSW, L"", ControlId::stick_slider, TBS_HORZ | TBS_NOTICKS | WS_TABSTOP,
                 Page::settings);
    stick_value = make(L"STATIC", L"20%", ControlId::stick_value, SS_RIGHT, Page::settings);
    label(L"Controller pointing", ControlId::motion_label, Page::settings);
    motion = make(TRACKBAR_CLASSW, L"", ControlId::motion_slider, TBS_HORZ | TBS_NOTICKS | WS_TABSTOP,
                  Page::settings);
    motion_value = make(L"STATIC", L"60%", ControlId::motion_value, SS_RIGHT, Page::settings);
    for (auto slider : {stick, motion}) {
        SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(10, 300));
        SendMessageW(slider, TBM_SETPAGESIZE, 0, 10);
    }
    label(L"Pointing smoothing", ControlId::smoothing_label, Page::settings);
    smooth = make(TRACKBAR_CLASSW, L"", ControlId::smoothing_slider, TBS_HORZ | TBS_NOTICKS | WS_TABSTOP,
                  Page::settings);
    SendMessageW(smooth, TBM_SETRANGE, TRUE, MAKELPARAM(0, 250));
    SendMessageW(smooth, TBM_SETPAGESIZE, 0, 10);
    smooth_value = make(L"STATIC", L"80 ms", ControlId::smoothing_value, SS_RIGHT, Page::settings);
    label(L"More smoothing reduces jitter and adds a little delay.", ControlId::smoothing_help,
          Page::settings);
    label(L"Snap turning", ControlId::snap_label, Page::settings);
    snap = make(L"COMBOBOX", L"", ControlId::snap_choice, CBS_DROPDOWNLIST | WS_TABSTOP, Page::settings);
    for (const auto* value : {L"Off", L"22.5 degrees", L"45 degrees", L"90 degrees"})
        SendMessageW(snap, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    legacy = make(L"BUTTON", L"Knuckles legacy binding compatibility", ControlId::legacy_binding,
                  BS_AUTOCHECKBOX | WS_TABSTOP, Page::settings);
    label(L"Keep this enabled for the tested Knuckles controls.", ControlId::legacy_help, Page::settings);
    logging = make(L"BUTTON", L"Diagnostic logging", ControlId::logging, BS_AUTOCHECKBOX | WS_TABSTOP,
                   Page::settings);
    label(L"Off by default. For troubleshooting; each log is limited to 2 MiB.", ControlId::logging_help,
          Page::settings);
    make(L"BUTTON", L"Save settings", ControlId::save, WS_TABSTOP, Page::settings);
    make(L"BUTTON", L"Reset defaults", ControlId::defaults, WS_TABSTOP, Page::settings);
    label(L"Speeds update live. Other settings briefly restart the affected VR fixes.",
          ControlId::settings_help, Page::settings);
}

void Window::layout_settings_page(int width) {
    place(control(ControlId::cursor_heading), 32, 148, width - 64, 26);
    place(control(ControlId::cursor_help), 32, 180, width - 64, 30);
    place(control(ControlId::stick_label), 40, 232, 170, 26);
    place(stick, 218, 226, width - 358, 36);
    place(stick_value, width - 124, 232, 84, 26);
    place(control(ControlId::motion_label), 40, 284, 174, 26);
    place(motion, 218, 278, width - 358, 36);
    place(motion_value, width - 124, 284, 84, 26);
    place(control(ControlId::smoothing_label), 40, 336, 174, 26);
    place(smooth, 218, 330, width - 358, 36);
    place(smooth_value, width - 124, 336, 84, 26);
    place(control(ControlId::smoothing_help), 218, 370, width - 258, 28);
    place(control(ControlId::snap_label), 40, 414, 174, 26);
    place(snap, 218, 408, 180, 160);
    place(legacy, 40, 458, width - 80, 26);
    place(control(ControlId::legacy_help), 62, 488, width - 102, 25);
    place(logging, 40, 526, width - 80, 26);
    place(control(ControlId::logging_help), 62, 556, width - 102, 25);
    place(control(ControlId::save), 40, 596, 148, 34);
    place(control(ControlId::defaults), 200, 596, 142, 34);
    place(control(ControlId::settings_help), 40, 640, width - 80, 38);
}

void Window::fill_settings(const Settings& settings) {
    SendMessageW(stick, TBM_SETPOS, TRUE, settings.stick_speed);
    SendMessageW(motion, TBM_SETPOS, TRUE, settings.motion_speed);
    SendMessageW(smooth, TBM_SETPOS, TRUE, settings.smoothing_ms);
    const int index = settings.snap_steps == 0   ? 0
                      : settings.snap_steps == 1 ? 1
                      : settings.snap_steps == 2 ? 2
                                                 : 3;
    SendMessageW(snap, CB_SETCURSEL, index, 0);
    SendMessageW(legacy, BM_SETCHECK, settings.legacy_axis ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(logging, BM_SETCHECK, settings.logging ? BST_CHECKED : BST_UNCHECKED, 0);
    update_values();
}

Settings Window::settings() const {
    Settings settings;
    settings.stick_speed = static_cast<int>(SendMessageW(stick, TBM_GETPOS, 0, 0));
    settings.motion_speed = static_cast<int>(SendMessageW(motion, TBM_GETPOS, 0, 0));
    settings.smoothing_ms = static_cast<int>(SendMessageW(smooth, TBM_GETPOS, 0, 0));
    const int index = static_cast<int>(SendMessageW(snap, CB_GETCURSEL, 0, 0));
    settings.snap_steps = index == 0 ? 0 : index == 1 ? 1 : index == 2 ? 2 : 4;
    settings.legacy_axis = SendMessageW(legacy, BM_GETCHECK, 0, 0) == BST_CHECKED;
    settings.logging = SendMessageW(logging, BM_GETCHECK, 0, 0) == BST_CHECKED;
    return settings;
}

void Window::update_values() {
    const auto settings = this->settings();
    SetWindowTextW(stick_value, (std::to_wstring(settings.stick_speed) + L"%").c_str());
    SetWindowTextW(motion_value, (std::to_wstring(settings.motion_speed) + L"%").c_str());
    SetWindowTextW(smooth_value, (std::to_wstring(settings.smoothing_ms) + L" ms").c_str());
}

} // namespace witness::launcher
