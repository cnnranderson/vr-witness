#include "common/protocol.hpp"
#include "common/session_log.hpp"
#include "common/fixes_toggle.hpp"
#include "game/build_validation.hpp"
#include "render/snapshot.hpp"
#include "common/win_util.hpp"
#include "common/caller_address.hpp"
#include <MinHook.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <sstream>

namespace {
using witness::validation::match;
using witness::validation::sha256;
using Begin = bool (*)(int);
using Submit = void (*)(int);
using Cursor = void (*)(void*, void*, bool);
using Menu = void (*)(bool, int, bool);
using Paused = bool (*)();
Begin original_begin{};
Submit original_submit{};
Cursor original_cursor{};
Menu original_menu{};
Paused original_paused{};
void* begin_site{};
void* submit_site{};
void* cursor_site{};
void* menu_site{};
void* paused_site{};
void* render_pause_caller{};
bool created = false;
bool poisoned = false;
std::atomic<bool> busy{false}, defer_enabled{false};
// Selected before enabling hooks; retained through teardown so pending eyes drain.
std::atomic<bool> defer_until_menu{false};
std::atomic<bool> redraw_paused_scene{false};
std::atomic<std::uint64_t> paused_scene_redraws{0};
std::atomic<bool> detailed{false}, route_fault{false}, session_stop{false};
std::atomic<witness::SessionState> session_state{witness::SessionState::stopped};
SRWLOCK control_lock = SRWLOCK_INIT;
bool session_logging{};
HANDLE session_thread{};
std::atomic<unsigned> active{0}, active_submit{0}, pending_count{0}, event_count{0};
thread_local bool inside_begin = false;
thread_local int last_eye = -1, pending_eye = -1;
std::uintptr_t game_image_base{};
const witness::RenderSnapshot* fixture_render_state{};

enum Kind {
    begin_eye,
    submit_immediate,
    cursor_enter,
    cursor_exit,
    submit_deferred,
    submit_after_cursor,
    submit_fallback,
    menu_enter,
    menu_exit,
    submit_after_menu,
    pause_render_check
};

std::array<std::atomic<std::uint64_t>, 11> totals{};
const char* names[] = {"eye.begin",           "eye.submit.immediate",    "cursor.enter",        "cursor.exit",
                       "eye.submit.deferred", "eye.submit.after_cursor", "eye.submit.fallback", "menu.enter",
                       "menu.exit",           "eye.submit.after_menu",   "pause.render_check"};

struct Event {
    std::atomic<bool> ready{false};
    DWORD thread{};
    int eye{};
    Kind kind{};
    LONGLONG tick{};
    int flags{};
    witness::RenderSnapshot render_state;
};

std::array<Event, 131072> events;

witness::RenderSnapshot read_render_state() {
    if (fixture_render_state)
        return *fixture_render_state;
    witness::RenderSnapshot state;
    if (!game_image_base)
        return state;
    // Read verified image globals on the render thread without calling game code.
    const auto read = [](auto& destination, std::uintptr_t rva) {
        std::memcpy(&destination, reinterpret_cast<const void*>(game_image_base + rva), sizeof(destination));
    };
    read(state.current_target, 0x469A5D0);
    read(state.eye_targets[0], 0x469AB58);
    read(state.eye_targets[1], 0x469AB70);
    read(state.scene_source, 0x630C78);
    read(state.menu_fade, 0x61E994);
    read(state.menu_matrix, 0x630B50);
    return state;
}

void write_float(std::ostream& log, float value) {
    if (std::isfinite(value))
        log << value;
    else
        log << "null";
}

void write_render_state(std::ostream& log, const witness::RenderSnapshot& state) {
    log << ",\"render_state\":{\"current_target\":\"0x" << std::hex << state.current_target
        << "\",\"eye_targets\":[\"0x" << state.eye_targets[0] << "\",\"0x" << state.eye_targets[1]
        << "\"],\"scene_source\":\"0x" << state.scene_source << "\",\"menu_fade\":" << std::dec;
    write_float(log, state.menu_fade);
    log << ",\"menu_matrix\":[";
    for (std::size_t i = 0; i < state.menu_matrix.size(); ++i) {
        if (i)
            log << ',';
        write_float(log, state.menu_matrix[i]);
    }
    log << "]}";
}

struct Active {
    Active() { active.fetch_add(1); }

    ~Active() { active.fetch_sub(1); }
};

void record(Kind kind, int eye, int flags = 0) {
    totals[kind].fetch_add(1, std::memory_order_relaxed);
    if (!detailed.load(std::memory_order_relaxed))
        return;
    const auto index = event_count.fetch_add(1, std::memory_order_relaxed);
    if (index >= events.size())
        return;
    auto& entry = events[index];
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    entry.thread = GetCurrentThreadId();
    entry.eye = eye;
    entry.kind = kind;
    entry.tick = now.QuadPart;
    entry.flags = flags;
    entry.render_state = read_render_state();
    entry.ready.store(true, std::memory_order_release);
}

void flush_pending(Kind kind) {
    if (pending_eye < 0)
        return;
    // An unexpected render route invalidates deferral for the rest of this run.
    if (kind == submit_fallback)
        route_fault.store(true);
    const int eye = pending_eye;
    pending_eye = -1;
    record(kind, eye);
    original_submit(eye);
    pending_count.fetch_sub(1);
}

bool hooked_begin(int eye) {
    Active guard;
    // If the selected completion point was skipped, preserve frame delivery.
    flush_pending(submit_fallback);
    last_eye = eye;
    record(begin_eye, eye);
    inside_begin = true;
    const bool result = original_begin(eye);
    inside_begin = false;
    return result;
}

void hooked_submit(int eye) {
    Active guard;
    active_submit.fetch_add(1);
    if (defer_enabled.load() && !route_fault.load() && inside_begin && eye >= 0 && eye <= 1 &&
        pending_eye < 0) {
        pending_eye = eye;
        pending_count.fetch_add(1);
        record(submit_deferred, eye);
    } else {
        record(submit_immediate, eye);
        original_submit(eye);
    }
    active_submit.fetch_sub(1);
}

void hooked_cursor(void* cursor, void* view, bool alternate) {
    Active guard;
    record(cursor_enter, last_eye);
    original_cursor(cursor, view, alternate);
    record(cursor_exit, last_eye);
    // Always drain a pending eye, even after the timed experiment is stopped.
    if (!defer_until_menu.load())
        flush_pending(submit_after_cursor);
}

void hooked_menu(bool stereo, int eye, bool mirror) {
    Active guard;
    const int flags = (stereo ? 1 : 0) | (mirror ? 2 : 0);
    record(menu_enter, eye, flags);
    original_menu(stereo, eye, mirror);
    record(menu_exit, eye, flags);
    // Only the matching stereo pass drains a pending eye, including during teardown.
    if (defer_until_menu.load() && stereo && !mirror && pending_eye == eye)
        flush_pending(submit_after_menu);
}

bool hooked_paused() {
    // Override only the verified render pause query, preserving simulation and input state.
    const auto caller = WITNESS_RETURN_ADDRESS();
    Active guard;
    const bool paused = original_paused();
    if (caller != render_pause_caller)
        return paused;
    const bool override_draw =
        paused && redraw_paused_scene.load() && defer_enabled.load() && !route_fault.load();
    if (override_draw)
        paused_scene_redraws.fetch_add(1, std::memory_order_relaxed);
    record(pause_render_check, -1, (paused ? 1 : 0) | (override_draw ? 2 : 0));
    return override_draw ? false : paused;
}

void require(MH_STATUS result, const char* operation = "hook operation") {
    if (result != MH_OK)
        throw std::runtime_error(std::string(operation) + ": " + MH_StatusToString(result));
}

void validate_targets(HMODULE self) {
    const auto executable = std::filesystem::path(witness::module_path());
    const auto own_directory = std::filesystem::path(witness::module_path(self)).parent_path();
    const auto main = GetModuleHandleW(nullptr);
    if (witness::same_path(executable, own_directory / L"witness-render-test-host.exe")) {
        begin_site = reinterpret_cast<void*>(GetProcAddress(main, "TestBeginEye"));
        submit_site = reinterpret_cast<void*>(GetProcAddress(main, "TestSubmit"));
        cursor_site = reinterpret_cast<void*>(GetProcAddress(main, "TestCursor"));
        menu_site = reinterpret_cast<void*>(GetProcAddress(main, "TestMenu"));
        paused_site = reinterpret_cast<void*>(GetProcAddress(main, "TestPaused"));
        const auto caller = reinterpret_cast<void**>(GetProcAddress(main, "TestPauseRenderReturn"));
        render_pause_caller = caller ? *caller : nullptr;
        fixture_render_state =
            reinterpret_cast<const witness::RenderSnapshot*>(GetProcAddress(main, "TestRenderState"));
        if (!begin_site || !submit_site || !cursor_site || !menu_site || !fixture_render_state ||
            !paused_site || !render_pause_caller)
            throw std::runtime_error("Owned fixture exports missing");
        return;
    }
    if (_wcsicmp(executable.filename().c_str(), L"witness64_d3d11.exe") ||
        sha256(executable) != "8d672d444df6a6df7130f25a517bdab1af92731fe2138dc5b324d519561d55a5")
        throw std::runtime_error("Unsupported Witness executable hash");
    const auto base = reinterpret_cast<std::uintptr_t>(main);
    bool image_matches = false;
    for (const auto& module : witness::modules(GetCurrentProcessId()))
        if (module.base == base && module.size == 74457088)
            image_matches = true;
    if (!image_matches)
        throw std::runtime_error("Unexpected loaded image size");
    match(base, 0x25F5D0, "8bd18b0d80163d0041b001e900000000");
    match(base, 0x37A260, "48895c2408574883ec404863f9488d05e408");
    match(base, 0x1CD770, "488bc448895010555356415541564157488da858feffff4881ec78020000");
    match(base, 0x1C9321, "e8aa620900");
    match(base, 0x1C935C, "e80f440000");
    match(base, 0x25F769, "e8b2cd0e00");
    match(base, 0x1FC3F0, "488bc4488958104889701855488da8c8feffff4881ec30020000");
    match(base, 0x37A26D, "488d05e4083204488d147f488b0cd0488d1cd0");
    match(base, 0x357794, "4c8935352e340448893d3e2e340448892d2f2e3404488935382e3404");
    match(base, 0x1FC706, "0f1105434443000f1048100f110d4844");
    match(base, 0x1FC40A, "f30f100582254200");
    match(base, 0x25F6A0, "488b05d1153d00");
    match(base, 0x1FE730, "f30f10055c0242000f2f05914d3100730332c0c3833dc5184300040f9dc0c3");
    match(base, 0x1C8A41, "e8ea5c030084c0488d4d60400f94c633d24088b5f0030000");
    match(base, 0x1C8E7A, "4084f60f8435040000");
    game_image_base = base;
    begin_site = reinterpret_cast<void*>(base + 0x25F5D0);
    submit_site = reinterpret_cast<void*>(base + 0x37A260);
    cursor_site = reinterpret_cast<void*>(base + 0x1CD770);
    menu_site = reinterpret_cast<void*>(base + 0x1FC3F0);
    paused_site = reinterpret_cast<void*>(base + 0x1FE730);
    render_pause_caller = reinterpret_cast<void*>(base + 0x1C8A46);
}

void stop_hooks() {
    redraw_paused_scene.store(false);
    defer_enabled.store(false);
    if (!created)
        return;
    // Stop new deferrals before removing completion routes needed to drain pending eyes.
    const auto disabled = MH_DisableHook(submit_site);
    if (disabled != MH_OK && disabled != MH_ERROR_DISABLED)
        require(disabled);
    const auto deadline = GetTickCount64() + 3000;
    while ((active_submit.load() || pending_count.load()) && GetTickCount64() < deadline)
        Sleep(1);
    if (active_submit.load() || pending_count.load())
        throw std::runtime_error("Deferred eye did not drain; restart game before retrying");
    const auto result = MH_DisableHook(MH_ALL_HOOKS);
    if (result != MH_OK && result != MH_ERROR_DISABLED)
        require(result);
    while (active.load() && GetTickCount64() < deadline)
        Sleep(1);
    if (active.load())
        throw std::runtime_error("Render callback still active; restart game");
    // Keep trampolines pinned because game calls can retain return addresses into them.
}

void prepare_hooks() {
    if (poisoned)
        throw std::runtime_error("Prior hook operation failed; restart the game before retrying");
    HMODULE self{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&prepare_hooks), &self))
        throw witness::win_error("GetModuleHandleExW");
    validate_targets(self);
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&prepare_hooks), &self))
        throw witness::win_error("Pin render probe");
    if (!created) {
        require(MH_Initialize(), "initialize");
        require(MH_CreateHook(begin_site, reinterpret_cast<void*>(&hooked_begin),
                              reinterpret_cast<void**>(&original_begin)),
                "create eye hook");
        require(MH_CreateHook(submit_site, reinterpret_cast<void*>(&hooked_submit),
                              reinterpret_cast<void**>(&original_submit)),
                "create submit hook");
        require(MH_CreateHook(cursor_site, reinterpret_cast<void*>(&hooked_cursor),
                              reinterpret_cast<void**>(&original_cursor)),
                "create cursor hook");
        require(MH_CreateHook(menu_site, reinterpret_cast<void*>(&hooked_menu),
                              reinterpret_cast<void**>(&original_menu)),
                "create menu hook");
        require(MH_CreateHook(paused_site, reinterpret_cast<void*>(&hooked_paused),
                              reinterpret_cast<void**>(&original_paused)),
                "create pause draw hook");
        created = true;
    }
    route_fault.store(false);
}

void enable_hooks(bool pause_experiment) {
    require(MH_EnableHook(begin_site));
    require(MH_EnableHook(submit_site));
    require(MH_EnableHook(cursor_site));
    require(MH_EnableHook(menu_site));
    // Cursor-only/trace diagnostics never intercept pause queries.
    if (pause_experiment)
        require(MH_EnableHook(paused_site));
}

bool enabled() {
    return defer_enabled.load() && !route_fault.load();
}

std::uint64_t completed_submissions() {
    return totals[submit_after_cursor].load() + totals[submit_after_menu].load();
}

void session_record(std::ofstream& target, const char* event) {
    if (!target.is_open() || !target)
        return;
    std::ostringstream log;
    log << "{\"event\":\"" << event << "\",\"uptime_ms\":" << GetTickCount64()
        << ",\"enabled\":" << (enabled() ? "true" : "false")
        << ",\"route_fault\":" << (route_fault.load() ? "true" : "false")
        << ",\"deferred\":" << totals[submit_deferred].load() << ",\"submitted\":" << completed_submissions()
        << ",\"completion\":\"" << (defer_until_menu.load() ? "menu" : "cursor") << "\""
        << ",\"pause_redraw_enabled\":" << (enabled() && redraw_paused_scene.load() ? "true" : "false")
        << ",\"paused_scene_redraws\":" << paused_scene_redraws.load()
        << ",\"fallback\":" << totals[submit_fallback].load() << "}\n";

    if (!log)
        throw std::runtime_error("Cursor session log write failed");
    witness::write_session_log(target, log.str());
}

DWORD WINAPI session_worker(void* argument) noexcept {
    std::unique_ptr<std::ofstream> log(static_cast<std::ofstream*>(argument));
    bool was_enabled = enabled(), was_fault = false, focused = false;
    bool f8_was_down = true;
    auto heartbeat = GetTickCount64();
    try {
        witness::FixesToggle fixes;
        while (!session_stop.load()) {
            DWORD foreground_pid{};
            GetWindowThreadProcessId(GetForegroundWindow(), &foreground_pid);
            const bool game_focused = foreground_pid == GetCurrentProcessId();
            const bool f8 = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
            // Disarm held keys across focus changes, and only accept rising edges.
            if (game_focused && focused) {
                if (f8 && !f8_was_down) {
                    const bool next = !defer_enabled.load() && !route_fault.load() && !poisoned;
                    defer_enabled.store(next);
                    fixes.publish(next);
                }
            }
            focused = game_focused;
            f8_was_down = f8;
            if (was_enabled != enabled() || was_fault != route_fault.load()) {
                session_record(*log, "session.changed");
                was_enabled = enabled();
                was_fault = route_fault.load();
            }
            if (GetTickCount64() - heartbeat >= 30000) {
                session_record(*log, "session.heartbeat");
                heartbeat = GetTickCount64();
            }
            Sleep(10);
        }
        session_state.store(witness::SessionState::stopping);
        stop_hooks();
        session_record(*log, "session.stopped");
        session_state.store(witness::SessionState::stopped);
    } catch (const std::exception& error) {
        poisoned = true;
        defer_enabled.store(false);
        bool stopped = false;
        try {
            stop_hooks();
            stopped = true;
        } catch (...) {
        }
        if (*log)
            *log << "{\"event\":\"session.failed\",\"hooks_stopped\":" << (stopped ? "true" : "false")
                 << ",\"error\":" << std::quoted(error.what()) << "}\n";
        session_state.store(witness::SessionState::failed);
    }
    log.reset();
    busy.store(false);
    return 0;
}
} // namespace

// Control a render session and write status into SessionRequest; see protocol.hpp.
extern "C" __declspec(dllexport) DWORD WINAPI WitnessSessionControl(void* argument) noexcept {
    using namespace witness;
    if (!argument)
        return static_cast<DWORD>(ProbeResult::invalid_request);
    auto& response = *static_cast<SessionRequest*>(argument);
    const auto request = response;
    if (request.size != sizeof(request) || request.version != kSessionProtocolVersion ||
        request.command < SessionCommand::start || request.command > SessionCommand::status ||
        std::find(std::begin(request.log_path), std::end(request.log_path), L'\0') ==
            std::end(request.log_path))
        return static_cast<DWORD>(ProbeResult::invalid_request);
    AcquireSRWLockExclusive(&control_lock);

    struct Unlock {
        ~Unlock() { ReleaseSRWLockExclusive(&control_lock); }
    } unlock;

    DWORD result = 0;
    bool starting = false;
    std::unique_ptr<std::ofstream> log;
    try {
        if (request.command == SessionCommand::start) {
            if (busy.exchange(true))
                throw std::runtime_error("Render diagnostic or session already active");
            starting = true;
            if (session_thread) {
                if (WaitForSingleObject(session_thread, 1000) != WAIT_OBJECT_0)
                    throw std::runtime_error("Prior session thread still running");
                CloseHandle(session_thread);
                session_thread = nullptr;
            }
            const std::filesystem::path path(request.log_path);
            log = witness::open_session_log(path);
            session_logging = !path.empty();
            prepare_hooks();
            detailed.store(false);
            // Redraw paused eyes and submit each after its cursor and stereo menu.
            defer_until_menu.store(true);
            redraw_paused_scene.store(true);
            session_stop.store(false);
            enable_hooks(true);
            defer_enabled.store(true);
            session_record(*log, "session.started");
            session_state.store(SessionState::running);
            // The worker owns this log from the moment CreateThread succeeds.
            auto* worker_log = log.release();
            session_thread = CreateThread(nullptr, 0, session_worker, worker_log, 0, nullptr);
            if (!session_thread) {
                log.reset(worker_log);
                throw win_error("CreateThread (cursor session)");
            }
            starting = false;
        } else if (request.command == SessionCommand::stop) {
            session_stop.store(true);
            if (session_thread && WaitForSingleObject(session_thread, 10000) != WAIT_OBJECT_0)
                throw std::runtime_error("Session did not stop; restart game");
        } else if (request.command == SessionCommand::enable || request.command == SessionCommand::disable) {
            if (session_state.load() != SessionState::running || session_stop.load() || route_fault.load())
                throw std::runtime_error(
                    "Session is inactive or render route faulted; stop and restart session");
            defer_enabled.store(request.command == SessionCommand::enable);
        }
    } catch (const std::exception& error) {
        result = static_cast<DWORD>(ProbeResult::internal_error);
        if (starting) {
            poisoned = true;
            try {
                stop_hooks();
            } catch (...) {
            }
            session_state.store(SessionState::failed);
            busy.store(false);
        }
        if (log && *log)
            *log << "{\"event\":\"session.failed\",\"error\":" << std::quoted(error.what()) << "}\n";
    }
    response.state = session_state.load();
    response.enabled = enabled() ? 1 : 0;
    response.logging = session_logging ? 1 : 0;
    response.fault = route_fault.load() ? 1 : 0;
    response.deferred = totals[submit_deferred].load();
    response.submitted = completed_submissions();
    response.fallback = totals[submit_fallback].load();
    if (response.state == SessionState::failed)
        result = static_cast<DWORD>(ProbeResult::internal_error);
    return result;
}

// Run a bounded ProbeRequest render diagnostic; leave the DLL pinned after completion.
extern "C" __declspec(dllexport) DWORD WINAPI WitnessProbeRun(void* argument) noexcept {
    using namespace witness;
    if (!argument)
        return static_cast<DWORD>(ProbeResult::invalid_request);
    const auto request = *static_cast<const ProbeRequest*>(argument);
    if (request.size != sizeof(request) || request.version != kProtocolVersion || request.reserved > 3 ||
        request.duration_ms > kMaxDurationMs || request.duration_ms == 0 ||
        std::find(std::begin(request.log_path), std::end(request.log_path), L'\0') ==
            std::end(request.log_path))
        return static_cast<DWORD>(ProbeResult::invalid_request);
    if (busy.exchange(true))
        return static_cast<DWORD>(ProbeResult::internal_error);

    struct Release {
        ~Release() { busy.store(false); }
    } release;

    std::ofstream log;
    try {
        if (poisoned)
            throw std::runtime_error("Prior capture failed; restart the game before retrying");
        const std::filesystem::path path(request.log_path);
        if (!path.is_absolute())
            throw std::runtime_error("Log path must be absolute");
        log.open(path, std::ios::binary | std::ios::trunc);
        if (!log)
            return static_cast<DWORD>(ProbeResult::cannot_open_log);
        prepare_hooks();
        detailed.store(true);
        defer_until_menu.store(request.reserved >= 2);
        redraw_paused_scene.store(request.reserved == 3);
        // Give each capture a fresh ring slice because retained callbacks can still write.
        const auto first_event = event_count.load();
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        const char* mode = request.reserved == 3   ? "redraw_paused_scene_and_defer_menu"
                           : request.reserved == 2 ? "defer_submit_until_menu"
                           : request.reserved == 1 ? "defer_submit_until_cursor"
                                                   : "trace_only";
        log << "{\"event\":\"render.started\",\"mode\":\"" << mode << "\",\"pid\":" << GetCurrentProcessId()
            << ",\"qpc_frequency\":" << frequency.QuadPart << "}\n";
        log.flush();
        // Every original exists before any detour is enabled.
        enable_hooks(request.reserved == 3);
        defer_enabled.store(request.reserved != 0);
        Sleep(request.duration_ms);
        stop_hooks();
        const auto count = event_count.load();
        for (unsigned i = first_event; i < std::min<unsigned>(count, static_cast<unsigned>(events.size()));
             ++i) {
            const auto& entry = events[i];
            if (!entry.ready.load(std::memory_order_acquire))
                continue;
            log << "{\"event\":\"" << names[entry.kind] << "\",\"sequence\":" << i
                << ",\"thread\":" << entry.thread << ",\"eye\":" << entry.eye << ",\"qpc\":" << entry.tick
                << ",\"flags\":" << entry.flags;
            write_render_state(log, entry.render_state);
            log << "}\n";
        }
        const auto available = first_event < events.size() ? events.size() - first_event : 0;
        const auto captured = count - first_event;
        log << "{\"event\":\"render.finished\",\"hooks_disabled\":true,\"dll_pinned_until_exit\":true,\"events\":"
            << captured << ",\"route_fault\":" << (route_fault.load() ? "true" : "false")
            << ",\"dropped\":" << (captured > available ? captured - available : 0) << "}\n";
        log.flush();
        if (!log)
            return static_cast<DWORD>(ProbeResult::log_write_failed);
        return static_cast<DWORD>(ProbeResult::success);
    } catch (const std::exception& error) {
        poisoned = true;
        defer_enabled.store(false);
        bool stopped = false;
        try {
            stop_hooks();
            stopped = true;
        } catch (...) {
        }
        if (log)
            log << "{\"event\":\"render.failed\",\"hooks_stopped\":" << (stopped ? "true" : "false")
                << ",\"error\":" << std::quoted(error.what()) << "}\n";
        return static_cast<DWORD>(ProbeResult::internal_error);
    }
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
    return TRUE;
}
