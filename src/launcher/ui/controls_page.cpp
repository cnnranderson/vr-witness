#include "launcher/ui/window.hpp"
#include <array>

namespace witness::launcher {
namespace {
struct Binding {
    const wchar_t* control;
    const wchar_t* action;
};

constexpr std::array<Binding, 8> bindings{{
    {L"Left stick", L"Walk"},
    {L"Right stick", L"Turn while walking; move the puzzle cursor"},
    {L"Right controller", L"Point to move the puzzle cursor"},
    {L"Right trigger", L"Activate a puzzle, click, and draw"},
    {L"Right A", L"Recenter pointing; switch back from stick cursor"},
    {L"Left B", L"Open or close the pause/settings menu"},
    {L"Right B", L"Cancel drawing, leave a puzzle, or go back"},
    {L"Either stick in menus", L"Up/down selects; left/right adjusts; hold to repeat"},
}};

ControlId binding_id(std::size_t row, bool action = false) {
    return static_cast<ControlId>(static_cast<int>(ControlId::binding_first) + 2 * row + action);
}
} // namespace

void Window::create_controls_page() {
    label(L"VR controls", ControlId::controls_heading, Page::controls);
    SendMessageW(control(ControlId::controls_heading), WM_SETFONT, reinterpret_cast<WPARAM>(bold_font), TRUE);
    label(L"Current bindings are tested with Knuckles controllers.", ControlId::controls_help,
          Page::controls);
    for (std::size_t row = 0; row < bindings.size(); ++row) {
        label(bindings[row].control, binding_id(row), Page::controls);
        label(bindings[row].action, binding_id(row, true), Page::controls);
    }
    label(L"Keep the game focused. Center sticks and release buttons after resuming.\n"
          L"Release the trigger before recentering. Leaving a puzzle may take two B presses.",
          ControlId::controls_note, Page::controls);
}

void Window::layout_controls_page(int width) {
    place(control(ControlId::controls_heading), 24, 60, width - 48, 24);
    place(control(ControlId::controls_help), 24, 86, width - 48, 24);
    for (std::size_t row = 0; row < bindings.size(); ++row) {
        const int y = 122 + 28 * static_cast<int>(row);
        place(control(binding_id(row)), 32, y, 168, 24);
        place(control(binding_id(row, true)), 204, y, width - 228, 24);
    }
    place(control(ControlId::controls_note), 24, 354, width - 48, 42);
}
} // namespace witness::launcher
