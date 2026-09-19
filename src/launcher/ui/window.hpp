#pragma once
#include "launcher/backend.hpp"
#include "launcher/ui/control_ids.hpp"
#include <commctrl.h>
#include <memory>
#include <vector>

namespace witness::launcher {
// All HWNDs belong to the UI thread; the backend publishes immutable snapshots.
class Window {
public:
    explicit Window(fs::path package_root) : root(std::move(package_root)) {}

    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    int run(HINSTANCE instance, int show);

#ifdef WITNESS_UI_TEST
    fs::path smoke_output;
#endif

private:
    enum class Page { shared, status, settings };
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    int scale(int value) const;
    HWND make(const wchar_t* cls, const wchar_t* text, ControlId id, DWORD style = 0,
              Page page = Page::status);
    HWND control(ControlId id) const;
    void place(HWND control, int x, int y, int width, int height);
    void label(const wchar_t* text, ControlId id, Page page = Page::status);
    void create();
    void layout();
    void create_status_page();
    void layout_status_page(int width);
    void create_settings_page();
    void layout_settings_page(int width);
    void fill_settings(const Settings& settings);
    Settings settings() const;
    void update_values();
    void refresh();
    static std::wstring session_text(const Session& session, bool uncertain);
    void send(Action action, fs::path folder = {});
    void browse();
    void command(ControlId id);

    HWND window{}, tabs{}, path{}, message{}, game{}, steam{}, vr{}, render{}, input{};
    HWND stick{}, motion{}, smooth{}, snap{}, legacy{}, logging{}, stick_value{}, motion_value{},
        smooth_value{};
    HFONT font{}, title_font{}, bold_font{};
    HBRUSH background{CreateSolidBrush(RGB(246, 248, 251))};
    std::vector<HWND> status_widgets, settings_widgets, action_buttons;
    std::unique_ptr<Backend> backend;
    fs::path root;
    int dpi{96};
    int scroll{};
    bool dirty{};
#ifdef WITNESS_UI_TEST
    void capture(const fs::path& output);
    void ui_smoke_tick();
    int smoke_step{};
#endif
};
} // namespace witness::launcher
