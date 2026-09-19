#include "launcher/ui/window.hpp"

namespace witness::launcher {

void Window::create_status_page() {
    label(L"Game installation", ControlId::installation_label);
    path = make(L"EDIT", L"", ControlId::game_path, ES_AUTOHSCROLL | ES_READONLY | WS_TABSTOP);
    make(L"BUTTON", L"Browse...", ControlId::browse, WS_TABSTOP);
    make(L"BUTTON", L"Find in Steam", ControlId::detect, WS_TABSTOP);
    label(L"Select the folder containing The Witness, or let Steam detection find it.",
          ControlId::installation_help);
    for (const auto& pair :
         std::vector<std::pair<ControlId, const wchar_t*>>{{ControlId::launch, L"Launch VR"},
                                                           {ControlId::attach, L"Attach to running game"},
                                                           {ControlId::stop, L"Stop fixes"},
                                                           {ControlId::cancel, L"Cancel wait"}})
        action_buttons.push_back(make(L"BUTTON", pair.second, pair.first, WS_TABSTOP));
    label(L"Session status", ControlId::session_heading);
    SendMessageW(control(ControlId::session_heading), WM_SETFONT, reinterpret_cast<WPARAM>(bold_font), TRUE);
    label(L"SteamVR", ControlId::steam_label);
    steam = make(L"STATIC", L"Checking...", ControlId::steam_status);
    label(L"The Witness", ControlId::game_label);
    game = make(L"STATIC", L"Checking...", ControlId::game_status);
    label(L"Native VR", ControlId::vr_label);
    vr = make(L"STATIC", L"Waiting for game", ControlId::vr_status);
    label(L"Headset visuals", ControlId::render_label);
    render = make(L"STATIC", L"Not attached", ControlId::render_status);
    label(L"Controller input", ControlId::input_label);
    input = make(L"STATIC", L"Not attached", ControlId::input_status);
}

void Window::layout_status_page(int width) {
    place(control(ControlId::installation_label), 32, 148, width - 64, 24);
    place(path, 32, 178, width - 302, 30);
    place(control(ControlId::browse), width - 258, 178, 98, 30);
    place(control(ControlId::detect), width - 150, 178, 118, 30);
    place(control(ControlId::installation_help), 32, 216, width - 64, 38);
    place(control(ControlId::launch), 32, 264, 134, 38);
    place(control(ControlId::attach), 178, 264, 206, 38);
    place(control(ControlId::stop), 396, 264, 116, 38);
    place(control(ControlId::cancel), 524, 264, 120, 38);
    place(control(ControlId::session_heading), 32, 324, width - 64, 28);
    const std::pair<ControlId, ControlId> rows[] = {
        {ControlId::steam_label, ControlId::steam_status},
        {ControlId::game_label, ControlId::game_status},
        {ControlId::vr_label, ControlId::vr_status},
        {ControlId::render_label, ControlId::render_status},
        {ControlId::input_label, ControlId::input_status},
    };
    int y = 364;
    for (const auto& row : rows) {
        place(control(row.first), 40, y, 170, 25);
        place(control(row.second), 218, y, width - 258, 25);
        y += 32;
    }
}

std::wstring Window::session_text(const Session& session, bool uncertain) {
    if (uncertain)
        return L"Status uncertain - restart game before retrying";
    if (session.fault || session.state == L"failed")
        return L"Fault - restart game";
    if (!session.loaded)
        return L"Not attached";
    if (session.state == L"running")
        return session.enabled ? L"Attached and active" : L"Attached, disabled by hotkey";
    return session.state == L"stopped" ? L"Stopped (DLL remains loaded until game exit)" : session.state;
}

void Window::refresh() {
    if (!backend)
        return;
    const auto snapshot = backend->snapshot();
    SetWindowTextW(path, snapshot.game_dir.c_str());
    SetWindowTextW(steam, snapshot.steamvr ? L"Running" : L"Not running");
    SetWindowTextW(game, snapshot.pid ? (L"Running  |  PID " + std::to_wstring(snapshot.pid)).c_str()
                                      : L"Not running");
    SetWindowTextW(vr, snapshot.vr_ready ? L"Ready"
                       : snapshot.pid    ? L"Not ready - check headset / VR launch"
                                         : L"Waiting for game");
    SetWindowTextW(render, session_text(snapshot.render, snapshot.uncertain).c_str());
    SetWindowTextW(input, session_text(snapshot.input, snapshot.uncertain).c_str());
    SetWindowTextW(message, snapshot.message.c_str());
    for (auto button : action_buttons)
        EnableWindow(button, !snapshot.busy);
    EnableWindow(control(ControlId::launch), !snapshot.busy && !snapshot.pending);
    EnableWindow(control(ControlId::attach),
                 !snapshot.busy && !snapshot.pending && snapshot.pid && !snapshot.uncertain);
    EnableWindow(control(ControlId::stop),
                 !snapshot.busy && !snapshot.uncertain && (snapshot.render.loaded || snapshot.input.loaded));
    EnableWindow(control(ControlId::cancel), !snapshot.busy && snapshot.pending);
    for (auto id : {ControlId::browse, ControlId::detect, ControlId::save})
        EnableWindow(control(id), !snapshot.busy && !snapshot.pending);
    if (!dirty) {
        fill_settings(snapshot.settings);
    }
}

} // namespace witness::launcher
