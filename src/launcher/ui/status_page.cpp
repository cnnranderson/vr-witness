#include "launcher/ui/window.hpp"
#include <iomanip>
#include <sstream>

namespace witness::launcher {
void Window::create_status_page() {
    label(L"Game installation", ControlId::installation_label);
    path = make(L"EDIT", L"", ControlId::game_path, ES_AUTOHSCROLL | ES_READONLY | WS_TABSTOP);
    make(L"BUTTON", L"Browse...", ControlId::browse, WS_TABSTOP);
    make(L"BUTTON", L"Find in Steam", ControlId::detect, WS_TABSTOP);
    label(L"Choose your game folder, then select Launch VR.", ControlId::installation_help);
    for (const auto& pair :
         std::vector<std::pair<ControlId, const wchar_t*>>{{ControlId::launch, L"Launch VR"},
                                                           {ControlId::attach, L"Attach to Game"},
                                                           {ControlId::cancel, L"Cancel wait"}})
        action_buttons.push_back(make(L"BUTTON", pair.second, pair.first, WS_TABSTOP));
    label(L"Sessions", ControlId::session_heading);
    label(L"Inactive", ControlId::overall_status);
    SendMessageW(control(ControlId::overall_status), WM_SETFONT, reinterpret_cast<WPARAM>(bold_font), TRUE);
    SendMessageW(control(ControlId::session_heading), WM_SETFONT, reinterpret_cast<WPARAM>(bold_font), TRUE);
    label(L"SteamVR", ControlId::steam_label);
    steam = make(L"STATIC", L"Checking...", ControlId::steam_status);
    label(L"The Witness", ControlId::game_label);
    game = make(L"STATIC", L"Checking...", ControlId::game_status);
    label(L"Native VR", ControlId::vr_label);
    vr = make(L"STATIC", L"Waiting for game", ControlId::vr_status);
    label(L"Headset visuals", ControlId::render_label);
    render = make(L"STATIC", L"Inactive", ControlId::render_status);
    label(L"Controller input", ControlId::input_label);
    input = make(L"STATIC", L"Inactive", ControlId::input_status);
    label(L"Configured height", ControlId::height_label);
    label(L"Game default", ControlId::height_status);
}

void Window::layout_status_page(int width) {
    place(control(ControlId::installation_label), 24, 60, width - 48, 22);
    place(path, 24, 86, width - 288, 30);
    place(control(ControlId::browse), width - 250, 86, 98, 30);
    place(control(ControlId::detect), width - 142, 86, 118, 30);
    place(control(ControlId::installation_help), 24, 122, width - 48, 24);
    place(control(ControlId::launch), 24, 156, 134, 34);
    place(control(ControlId::attach), 170, 156, 184, 34);
    place(control(ControlId::cancel), 366, 156, 120, 34);
    place(control(ControlId::session_heading), 24, 208, 84, 24);
    place(control(ControlId::overall_status), 116, 208, width - 140, 24);
    const auto row = [&](ControlId label, ControlId value, int y) {
        place(control(label), 32, y, 168, 24);
        place(control(value), 204, y, width - 228, 24);
    };
    row(ControlId::steam_label, ControlId::steam_status, 240);
    row(ControlId::game_label, ControlId::game_status, 270);
    row(ControlId::vr_label, ControlId::vr_status, 300);
    row(ControlId::render_label, ControlId::render_status, 330);
    row(ControlId::input_label, ControlId::input_status, 360);
    row(ControlId::height_label, ControlId::height_status, 390);
}

std::wstring Window::session_text(const Session& session, bool uncertain) {
    if (!uncertain && session.active())
        return L"Active";
    return session.loaded ? L"Inactive (DLL remains loaded until game exit)" : L"Inactive";
}

std::wstring Window::height_text(const Session& session, bool uncertain) {
    if (uncertain)
        return L"Unavailable";
    if (!session.loaded || !session.height_calibrated)
        return L"Game default";
    std::wostringstream text;
    text << std::fixed << std::setprecision(2) << session.height_m << L" m (" << std::showpos
         << session.height_offset_m << L" m adjustment)";
    if (!session.active())
        text << L" - inactive";
    return text.str();
}

void Window::refresh() {
    if (!backend)
        return;
    const auto snapshot = backend->snapshot();
    active = snapshot.active();
    SetWindowTextW(control(ControlId::overall_status), active ? L"Active" : L"Inactive");
    InvalidateRect(control(ControlId::overall_status), nullptr, TRUE);
    SetWindowTextW(path, snapshot.game_dir.c_str());
    SetWindowTextW(steam, snapshot.steamvr ? L"Running" : L"Not running");
    SetWindowTextW(game, snapshot.pid ? L"Running" : L"Not running");
    SetWindowTextW(vr, snapshot.vr_ready ? L"Ready" : snapshot.pid ? L"Not ready" : L"Waiting for game");
    SetWindowTextW(render, session_text(snapshot.render, snapshot.uncertain).c_str());
    SetWindowTextW(input, session_text(snapshot.input, snapshot.uncertain).c_str());
    SetWindowTextW(control(ControlId::height_status),
                   height_text(snapshot.input, snapshot.uncertain).c_str());
    SetWindowTextW(control(ControlId::attach), snapshot.attached() ? L"Detach from game" : L"Attach to Game");
    for (auto button : action_buttons)
        EnableWindow(button, !snapshot.busy);
    EnableWindow(control(ControlId::launch), !snapshot.busy && !snapshot.pending);
    EnableWindow(control(ControlId::attach),
                 !snapshot.busy && !snapshot.pending && snapshot.pid && !snapshot.uncertain);
    EnableWindow(control(ControlId::cancel), !snapshot.busy && snapshot.pending);
    ShowWindow(control(ControlId::cancel),
               snapshot.pending && TabCtrl_GetCurSel(tabs) == 0 ? SW_SHOW : SW_HIDE);
    for (auto id : {ControlId::browse, ControlId::detect, ControlId::save})
        EnableWindow(control(id), !snapshot.busy && !snapshot.pending);
    if (!dirty)
        fill_settings(snapshot.settings);
    if (!snapshot.error)
        last_error.clear();
    else if (snapshot.message != last_error) {
        last_error = snapshot.message;
        MessageBoxW(window, last_error.c_str(), L"The Witness VR", MB_OK | MB_ICONERROR);
    }
}
} // namespace witness::launcher
