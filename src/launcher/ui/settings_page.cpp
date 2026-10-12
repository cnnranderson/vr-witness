#include "launcher/ui/window.hpp"
#include <tuple>

namespace witness::launcher {
void Window::create_settings_page() {
    label(L"Cursor speed", ControlId::cursor_heading, Page::settings);
    SendMessageW(control(ControlId::cursor_heading), WM_SETFONT, reinterpret_cast<WPARAM>(bold_font), TRUE);
    label(L"Lower values are slower. Speeds use view width per second.", ControlId::cursor_help,
          Page::settings);
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
    label(L"Turning", ControlId::snap_label, Page::settings);
    snap = make(L"COMBOBOX", L"", ControlId::snap_choice, CBS_DROPDOWNLIST | WS_TABSTOP, Page::settings);
    for (const auto* value :
         {L"Off", L"Snap 22.5 degrees", L"Snap 45 degrees", L"Snap 90 degrees", L"Smooth"})
        SendMessageW(snap, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    label(L"Smooth turn speed", ControlId::turn_speed_label, Page::settings);
    turn_speed = make(TRACKBAR_CLASSW, L"", ControlId::turn_speed_slider, TBS_HORZ | TBS_NOTICKS | WS_TABSTOP,
                      Page::settings);
    SendMessageW(turn_speed, TBM_SETRANGE, TRUE, MAKELPARAM(30, 180));
    SendMessageW(turn_speed, TBM_SETPAGESIZE, 0, 10);
    turn_speed_value = make(L"STATIC", L"60 deg/s", ControlId::turn_speed_value, SS_RIGHT, Page::settings);
    label(L"Controller type", ControlId::controller_label, Page::settings);
    controller = make(L"COMBOBOX", L"", ControlId::controller_choice,
                      CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, Page::settings);
    for (const auto* value : {L"VR", L"Gamepad"})
        SendMessageW(controller, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    make(L"BUTTON", L"Automatic puzzle positioning", ControlId::puzzle_reposition,
         BS_AUTOCHECKBOX | WS_TABSTOP, Page::settings);
    label(L"Turn off to stay in place when entering a panel; some viewpoints may need it.",
          ControlId::puzzle_reposition_help, Page::settings);
    make(L"BUTTON", L"Show hands", ControlId::show_hands, BS_AUTOCHECKBOX | WS_TABSTOP, Page::settings);
    label(L"Experimental controller-tracked gloves.", ControlId::hands_help, Page::settings);
    logging =
        make(L"BUTTON", L"Debug logging", ControlId::logging, BS_AUTOCHECKBOX | WS_TABSTOP, Page::settings);
    label(L"Off by default; logs are limited to 2 MiB each.", ControlId::logging_help, Page::settings);
    make(L"BUTTON", L"Save settings", ControlId::save, WS_TABSTOP, Page::settings);
    make(L"BUTTON", L"Reset defaults", ControlId::defaults, WS_TABSTOP, Page::settings);
    label(L"Cursor speeds update live. Other changes briefly restart the affected fixes.",
          ControlId::settings_help, Page::settings);
}

void Window::layout_settings_page(int width) {
    place(control(ControlId::cursor_heading), 24, 60, width - 48, 24);
    place(control(ControlId::cursor_help), 24, 86, width - 48, 24);
    const std::tuple<ControlId, HWND, HWND, int> rows[] = {
        {ControlId::stick_label, stick, stick_value, 120},
        {ControlId::motion_label, motion, motion_value, 156},
        {ControlId::smoothing_label, smooth, smooth_value, 192},
    };
    for (const auto& [label_id, slider, value, y] : rows) {
        place(control(label_id), 32, y, 168, 26);
        place(slider, 204, y - 6, width - 324, 32);
        place(value, width - 104, y, 80, 26);
    }
    place(control(ControlId::smoothing_help), 204, 222, width - 228, 25);
    place(control(ControlId::snap_label), 32, 264, 168, 26);
    place(snap, 204, 258, 230, 160);
    place(control(ControlId::turn_speed_label), 32, 298, 168, 26);
    place(turn_speed, 204, 292, width - 324, 32);
    place(turn_speed_value, width - 104, 298, 80, 26);
    place(control(ControlId::controller_label), 32, 332, 168, 26);
    place(controller, 204, 326, 230, 160);
    place(control(ControlId::puzzle_reposition), 32, 370, 330, 26);
    place(control(ControlId::puzzle_reposition_help), 32, 400, width - 64, 26);
    place(control(ControlId::show_hands), 32, 430, 160, 26);
    place(control(ControlId::hands_help), 204, 432, width - 228, 26);
    place(logging, 32, 466, 160, 26);
    place(control(ControlId::logging_help), 204, 468, width - 228, 26);
    place(control(ControlId::save), 32, 508, 148, 32);
    place(control(ControlId::defaults), 192, 508, 142, 32);
    place(control(ControlId::settings_help), 32, 552, width - 64, 36);
}

void Window::fill_settings(const Settings& settings) {
    SendMessageW(stick, TBM_SETPOS, TRUE, settings.stick_speed);
    SendMessageW(motion, TBM_SETPOS, TRUE, settings.motion_speed);
    SendMessageW(smooth, TBM_SETPOS, TRUE, settings.smoothing_ms);
    const int index = settings.smooth_turn       ? 4
                      : settings.snap_steps == 0 ? 0
                      : settings.snap_steps == 1 ? 1
                      : settings.snap_steps == 2 ? 2
                                                 : 3;
    SendMessageW(snap, CB_SETCURSEL, index, 0);
    SendMessageW(turn_speed, TBM_SETPOS, TRUE, settings.smooth_turn_speed);
    SendMessageW(controller, CB_SETCURSEL, 0, 0);
    SendMessageW(logging, BM_SETCHECK, settings.logging ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(control(ControlId::puzzle_reposition), BM_SETCHECK,
                 settings.puzzle_repositioning ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(control(ControlId::show_hands), BM_SETCHECK,
                 settings.show_hands ? BST_CHECKED : BST_UNCHECKED, 0);
    update_values();
}

Settings Window::settings() const {
    Settings settings;
    settings.stick_speed = static_cast<int>(SendMessageW(stick, TBM_GETPOS, 0, 0));
    settings.motion_speed = static_cast<int>(SendMessageW(motion, TBM_GETPOS, 0, 0));
    settings.smoothing_ms = static_cast<int>(SendMessageW(smooth, TBM_GETPOS, 0, 0));
    const int index = static_cast<int>(SendMessageW(snap, CB_GETCURSEL, 0, 0));
    settings.snap_steps = index == 1 ? 1 : index == 2 ? 2 : index == 3 ? 4 : 0;
    settings.smooth_turn = index == 4;
    settings.smooth_turn_speed = static_cast<int>(SendMessageW(turn_speed, TBM_GETPOS, 0, 0));
    settings.controller = ControllerType::vr;
    settings.logging = SendMessageW(logging, BM_GETCHECK, 0, 0) == BST_CHECKED;
    settings.puzzle_repositioning =
        SendMessageW(control(ControlId::puzzle_reposition), BM_GETCHECK, 0, 0) == BST_CHECKED;
    settings.show_hands = SendMessageW(control(ControlId::show_hands), BM_GETCHECK, 0, 0) == BST_CHECKED;
    return settings;
}

void Window::update_values() {
    const auto settings = this->settings();
    SetWindowTextW(stick_value, (std::to_wstring(settings.stick_speed) + L"%").c_str());
    SetWindowTextW(motion_value, (std::to_wstring(settings.motion_speed) + L"%").c_str());
    SetWindowTextW(smooth_value, (std::to_wstring(settings.smoothing_ms) + L" ms").c_str());
    SetWindowTextW(turn_speed_value, (std::to_wstring(settings.smooth_turn_speed) + L" deg/s").c_str());
    for (const auto control : {turn_speed, turn_speed_value, this->control(ControlId::turn_speed_label)})
        EnableWindow(control, settings.smooth_turn);
}
} // namespace witness::launcher
