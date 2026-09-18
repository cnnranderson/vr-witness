#include "protocol.hpp"
#include "win_util.hpp"

#include <algorithm>
#include <iostream>
#include <limits>

namespace {
using namespace witness;

struct Options {
    DWORD pid = 0;
    DWORD duration_ms = 5000;
    std::filesystem::path expected_exe;
    std::filesystem::path log;
    bool test_host = false;
    bool render_trace = false;
    bool defer_cursor = false;
    bool defer_menu = false;
    bool redraw_pause = false;
    bool controller_trace = false, controller_move = false, controller_legacy_axis0 = false;
    bool controller_session = false;
    bool controller_aim = false, controller_aim_trace = false;
    bool controller_snap=false,controller_snap_trace=false,snap_angle_given=false;
    unsigned snap_steps=2;
    DWORD aim_speed_percent=80, aim_smoothing_ms=80;
    bool aim_settings_given=false;
    std::wstring session;
};

unsigned long number(const std::wstring& value) {
    if (value.empty() || value.find_first_not_of(L"0123456789") != std::wstring::npos)
        throw std::runtime_error("Expected an unsigned decimal number");
    const auto parsed = std::stoull(value);
    if (parsed > std::numeric_limits<DWORD>::max()) throw std::runtime_error("Number is too large");
    return static_cast<unsigned long>(parsed);
}

Options options(int argc, wchar_t** argv) {
    Options result;
    for (int i = 1; i < argc; ++i) {
        const std::wstring name = argv[i];
        if (name == L"--controller-legacy-axis0") { result.controller_legacy_axis0 = true; continue; }
        if (name == L"--controller-trace") { result.controller_trace = true; continue; }
        if (name == L"--controller-snap") {result.controller_snap=true;continue;}
        if (name == L"--controller-snap-trace") {result.controller_snap_trace=true;continue;}
        if (name == L"--controller-aim") {result.controller_aim=true;continue;}
        if (name == L"--controller-aim-trace") {result.controller_aim_trace=true;continue;}
        if (name == L"--controller-move") { result.controller_move = true; continue; }
        if (name == L"--test-host") { result.test_host = true; continue; }
        if (name == L"--render-trace") { result.render_trace = true; continue; }
        if (name == L"--defer-cursor") { result.defer_cursor = true; continue; }
        if (name == L"--defer-menu") { result.defer_menu = true; continue; }
        if (name == L"--redraw-pause") { result.redraw_pause = true; continue; }
        if (i + 1 >= argc) throw std::runtime_error("Missing option value");
        const std::wstring value = argv[++i];
        if (name == L"--pid") result.pid = number(value);
        else if (name == L"--game-exe") result.expected_exe = value;
        else if (name == L"--log") result.log = value;
        else if (name == L"--aim-speed-percent") {result.aim_speed_percent=number(value);result.aim_settings_given=true;}
        else if (name == L"--aim-smoothing-ms") {result.aim_smoothing_ms=number(value);result.aim_settings_given=true;}
        else if(name==L"--snap-angle") {
            if(value==L"22.5")result.snap_steps=1;else if(value==L"45")result.snap_steps=2;else if(value==L"90")result.snap_steps=4;
            else throw std::runtime_error("Snap angle must be 22.5, 45 or 90 degrees");
            result.snap_angle_given=true;
        }
        else if (name == L"--duration-ms") result.duration_ms = number(value);
        else if (name == L"--cursor-session" || name == L"--controller-session") {
            if (!result.session.empty()) throw std::runtime_error("Choose one session control");
            result.session = value; result.controller_session = name == L"--controller-session";
        }
        else throw std::runtime_error("Unknown option");
    }
    if(result.controller_snap && !((result.controller_trace && result.session.empty()) || (result.controller_session && result.session==L"start")))throw std::runtime_error("Snap turning requires a controller capture or session start");
    if(result.controller_snap_trace && (!result.controller_trace || !result.session.empty() || result.controller_snap))throw std::runtime_error("Snap trace requires a passive bounded controller capture");
    if(result.snap_angle_given && !result.controller_snap)throw std::runtime_error("Snap angle requires --controller-snap");
    if(result.aim_speed_percent<10 || result.aim_speed_percent>300 || result.aim_smoothing_ms>250) throw std::runtime_error("Aim speed must be 10..300 percent of view width/second; smoothing 0..250 ms");
    if(result.aim_settings_given && !(result.controller_session && result.session==L"start" && result.controller_aim)) throw std::runtime_error("Aim settings require controller session start with --controller-aim");
    if (result.controller_aim && !((result.controller_trace && result.session.empty()) || (result.controller_session && result.session==L"start"))) throw std::runtime_error("Aiming requires a controller capture or session start");
    if (result.controller_aim_trace && (!result.controller_trace || !result.session.empty())) throw std::runtime_error("Passive aim trace requires a bounded controller capture");
    if(result.controller_aim && result.controller_aim_trace)throw std::runtime_error("Choose aiming or passive aim trace");
    if (result.controller_move && !result.controller_trace) throw std::runtime_error("Movement flag requires --controller-trace");
    if (result.controller_legacy_axis0 && !result.controller_trace && !result.controller_session) throw std::runtime_error("Legacy axis requires controller capture/session");
    if (result.controller_trace && (result.render_trace || result.defer_cursor || result.defer_menu || result.redraw_pause || !result.session.empty()))
        throw std::runtime_error("Choose timed capture or session control");
    if (!result.session.empty()) {
        if (result.render_trace || result.defer_cursor || result.defer_menu || result.redraw_pause) throw std::runtime_error("Choose session or timed render trace");
        if (result.session != L"start" && result.session != L"stop" && result.session != L"enable" &&
            result.session != L"disable" && result.session != L"status") throw std::runtime_error("Unknown session command");
        if (result.controller_session) result.controller_trace = true;
        else result.render_trace = true;
        if (result.controller_legacy_axis0 && result.session != L"start") throw std::runtime_error("Legacy axis is set at session start only");
    }
    const bool needs_log = result.session.empty() || result.session == L"start";
    if (!result.pid || (needs_log && result.log.empty()) || result.duration_ms > kMaxDurationMs)
        throw std::runtime_error("Provide --pid and --log; duration must be 0..30000 ms");
    if (result.test_host) {
        if (!result.expected_exe.empty()) throw std::runtime_error("--test-host cannot be combined with --game-exe");
        result.expected_exe = std::filesystem::path(module_path()).parent_path() /
            (result.controller_trace ? L"witness-input-test-host.exe" : result.render_trace ? L"witness-render-test-host.exe" : L"witness-probe-test-host.exe");
    } else if (result.expected_exe.empty() || _wcsicmp(result.expected_exe.filename().c_str(), L"witness64_d3d11.exe")) {
        throw std::runtime_error("Only witness64_d3d11.exe is supported; provide its full path with --game-exe");
    }
    if (!result.expected_exe.is_absolute() || (needs_log && !result.log.is_absolute()))
        throw std::runtime_error("Executable and log paths must be absolute");
    if ((result.defer_cursor || result.defer_menu || result.redraw_pause) && !result.render_trace)
        throw std::runtime_error("Deferral requires --render-trace");
    if (static_cast<int>(result.defer_cursor) + static_cast<int>(result.defer_menu) + static_cast<int>(result.redraw_pause) > 1)
        throw std::runtime_error("Choose one timed render experiment");
    return result;
}

// Own a local DLL reference used to resolve export offsets.
class LocalLibrary {
public:
    explicit LocalLibrary(const std::filesystem::path& path) {
        value = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!value) throw win_error("LoadLibraryExW (local probe)");
    }
    ~LocalLibrary() { FreeLibrary(value); }
    LocalLibrary(const LocalLibrary&) = delete;
    LocalLibrary& operator=(const LocalLibrary&) = delete;
    HMODULE value{};
};

// Own target-process memory unless a timed-out remote call still needs it.
class RemoteBuffer {
public:
    RemoteBuffer(HANDLE process, const void* data, std::size_t size) : process_(process) {
        value = VirtualAllocEx(process, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!value) throw win_error("VirtualAllocEx");
        SIZE_T written = 0;
        if (!WriteProcessMemory(process, value, data, size, &written) || written != size) {
            const auto error = win_error("WriteProcessMemory");
            VirtualFreeEx(process, value, 0, MEM_RELEASE);
            throw error;
        }
    }
    ~RemoteBuffer() { if (value) VirtualFreeEx(process_, value, 0, MEM_RELEASE); }
    RemoteBuffer(const RemoteBuffer&) = delete;
    RemoteBuffer& operator=(const RemoteBuffer&) = delete;
    // Transfer cleanup to process exit when the target may still use this allocation.
    void preserve() { value = nullptr; }
    void* value{};
private:
    HANDLE process_;
};

DWORD remote_call(HANDLE process, std::uintptr_t function, void* parameter,
                  DWORD timeout, RemoteBuffer* allocation = nullptr) {
    Handle thread(CreateRemoteThread(process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(function), parameter, 0, nullptr));
    if (!thread) throw win_error("CreateRemoteThread");
    const DWORD wait = WaitForSingleObject(thread.get(), timeout);
    if (wait != WAIT_OBJECT_0) {
        // A timed-out target may still use this allocation; process exit reclaims it safely.
        if (allocation) allocation->preserve();
        throw std::runtime_error("Remote thread did not finish. Probe may remain loaded; close and restart the target before retrying.");
    }
    DWORD result = 0;
    if (!GetExitCodeThread(thread.get(), &result)) throw win_error("GetExitCodeThread");
    return result;
}

std::uintptr_t remote_system_function(DWORD pid, const char* name) {
    const auto kernel = GetModuleHandleW(L"kernel32.dll");
    const auto function = GetProcAddress(kernel, name);
    if (!function) throw win_error("GetProcAddress (system function)");
    MEMORY_BASIC_INFORMATION memory{};
    if (!VirtualQuery(reinterpret_cast<const void*>(function), &memory, sizeof(memory)))
        throw win_error("VirtualQuery");
    // Resolve forwarded exports to their actual owning module (often KernelBase).
    // Never assume a system DLL has the same ASLR base in two processes.
    const auto owner = static_cast<HMODULE>(memory.AllocationBase);
    const auto path = module_path(owner);
    const auto offset = reinterpret_cast<std::uintptr_t>(function) - reinterpret_cast<std::uintptr_t>(owner);
    for (const auto& module : modules(pid)) {
        if (same_path(module.path, path)) {
            if (offset >= module.size) throw std::runtime_error("System function lies outside remote image");
            return module.base + offset;
        }
    }
    throw std::runtime_error("Could not find matching system module in the target");
}

std::uintptr_t find_module(DWORD pid, const std::filesystem::path& path) {
    for (const auto& module : modules(pid)) if (same_path(module.path, path)) return module.base;
    return 0;
}

void require_x64(HANDLE process) {
    USHORT process_machine = 0, native_machine = 0;
    if (!IsWow64Process2(process, &process_machine, &native_machine)) throw win_error("IsWow64Process2");
    const auto machine = process_machine == IMAGE_FILE_MACHINE_UNKNOWN ? native_machine : process_machine;
    if (machine != IMAGE_FILE_MACHINE_AMD64) throw std::runtime_error("Target is not x64");
}

int session_control(const Options& config, HANDLE process, const std::filesystem::path& dll) {
    SessionRequest request{};
    request.size = sizeof(request);
    request.version = kProtocolVersion;
    request.command = config.session == L"start" ? SessionCommand::start :
        config.session == L"stop" ? SessionCommand::stop :
        config.session == L"enable" ? SessionCommand::enable :
        config.session == L"disable" ? SessionCommand::disable : SessionCommand::status;
    auto base = find_module(config.pid, dll);
    if (!base && request.command != SessionCommand::start) {
        std::cout << "{\"state\":\"stopped\",\"enabled\":false,\"fault\":false,\"loaded\":false}\n";
        return request.command == SessionCommand::status || request.command == SessionCommand::stop ? 0 : 1;
    }
    if (request.command == SessionCommand::start) {
        const auto path = config.log.wstring();
        if (path.size() >= kPathCapacity) throw std::runtime_error("Log path is too long");
        if (std::filesystem::exists(config.log)) throw std::runtime_error("Refusing to overwrite existing log");
        std::filesystem::create_directories(config.log.parent_path());
        std::copy(path.begin(), path.end(), request.log_path);
    }
    LocalLibrary local(dll);
    const auto run = GetProcAddress(local.value, "WitnessSessionControl");
    if (!run) throw win_error("GetProcAddress (WitnessSessionControl)");
    const auto offset = reinterpret_cast<std::uintptr_t>(run) - reinterpret_cast<std::uintptr_t>(local.value);
    if (!base) {
        const auto path = dll.wstring();
        RemoteBuffer buffer(process, path.c_str(), (path.size() + 1) * sizeof(wchar_t));
        remote_call(process, remote_system_function(config.pid, "LoadLibraryW"), buffer.value, 15000, &buffer);
        base = find_module(config.pid, dll);
        if (!base) throw std::runtime_error("Render DLL did not load");
    }
    RemoteBuffer buffer(process, &request, sizeof(request));
    const auto result = remote_call(process, base + offset, buffer.value, 15000, &buffer);
    SIZE_T received{};
    if (!ReadProcessMemory(process, buffer.value, &request, sizeof(request), &received) || received != sizeof(request))
        throw win_error("Read cursor session status");
    const char* state = request.state == SessionState::running ? "running" :
        request.state == SessionState::stopping ? "stopping" : request.state == SessionState::failed ? "failed" : "stopped";
    std::cout << "{\"state\":\"" << state << "\",\"enabled\":" << (request.enabled ? "true" : "false")
        << ",\"fault\":" << (request.fault ? "true" : "false") << ",\"loaded\":true,\"deferred\":" << request.deferred
        << ",\"submitted\":" << request.submitted << ",\"fallback\":" << request.fallback << "}\n";
    if (result) throw std::runtime_error("Session command failed (active diagnostic/session, fault, or log/setup failure); inspect session log and status");
    return 0;
}

int input_session_control(const Options& config, HANDLE process, const std::filesystem::path& dll) {
    InputSessionRequest request{};
    request.size=sizeof(request); request.version=kInputProtocolVersion;
    request.command=config.session==L"start"?SessionCommand::start:config.session==L"stop"?SessionCommand::stop:
        config.session==L"enable"?SessionCommand::enable:config.session==L"disable"?SessionCommand::disable:SessionCommand::status;
    request.legacy_axis0=config.controller_legacy_axis0?1:0;
    request.pointing=config.controller_aim?1:0;request.snap_steps=config.controller_snap?config.snap_steps:0;
    request.aim_speed_percent=config.aim_speed_percent;request.aim_smoothing_ms=config.aim_smoothing_ms;
    auto base=find_module(config.pid,dll);
    if(!base && request.command!=SessionCommand::start) {
        std::cout<<"{\"state\":\"stopped\",\"enabled\":false,\"fault\":false,\"loaded\":false}\n";
        return request.command==SessionCommand::status || request.command==SessionCommand::stop ? 0 : 1;
    }
    if(request.command==SessionCommand::start) {
        const auto path=config.log.wstring();
        if(path.size()>=kPathCapacity || std::filesystem::exists(config.log)) throw std::runtime_error("Invalid or existing input session log");
        std::filesystem::create_directories(config.log.parent_path());
        std::copy(path.begin(),path.end(),request.log_path);
    }
    LocalLibrary local(dll);
    const auto run=GetProcAddress(local.value,"WitnessInputSessionControl");
    if(!run) throw win_error("GetProcAddress (WitnessInputSessionControl)");
    const auto offset=reinterpret_cast<std::uintptr_t>(run)-reinterpret_cast<std::uintptr_t>(local.value);
    if(!base) {
        const auto path=dll.wstring();
        RemoteBuffer buffer(process,path.c_str(),(path.size()+1)*sizeof(wchar_t));
        remote_call(process,remote_system_function(config.pid,"LoadLibraryW"),buffer.value,15000,&buffer);
        base=find_module(config.pid,dll);
        if(!base) throw std::runtime_error("Input DLL did not load");
    }
    RemoteBuffer buffer(process,&request,sizeof(request));
    const auto result=remote_call(process,base+offset,buffer.value,15000,&buffer);
    SIZE_T received{};
    if(!ReadProcessMemory(process,buffer.value,&request,sizeof(request),&received)||received!=sizeof(request)) throw win_error("Read input session status");
    const char* state=request.state==SessionState::running?"running":request.state==SessionState::stopping?"stopping":request.state==SessionState::failed?"failed":"stopped";
    std::cout<<"{\"state\":\""<<state<<"\",\"enabled\":"<<(request.enabled?"true":"false")<<",\"fault\":"<<(request.fault?"true":"false")
        <<",\"legacy_axis0\":"<<(request.legacy_axis0?"true":"false")<<",\"loaded\":true,\"polls\":"<<request.polls
        <<",\"movement_calls\":"<<request.movement_calls<<",\"applied\":"<<request.applied
        <<",\"pointing\":"<<(request.pointing?"true":"false")<<",\"aim_enabled\":"<<((request.enabled&&request.pointing)?"true":"false")
        <<",\"stick_cursor_speed_percent\":"<<request.stick_speed_percent<<",\"aim_speed_percent\":"<<request.aim_speed_percent<<",\"aim_smoothing_ms\":"<<request.aim_smoothing_ms
        <<",\"puzzle_back_presses\":"<<request.puzzle_back_presses<<",\"stick_applied\":"<<request.stick_applied<<",\"cancel_suppressed\":"<<request.cancel_suppressed<<",\"cursor_calls\":"<<request.cursor_calls<<",\"aim_applied\":"<<request.aim_applied<<",\"recenter_count\":"<<request.recenter_count<<",\"snap_angle\":"<<request.snap_steps*22.5
        <<",\"snap_enabled\":"<<((request.enabled&&request.snap_steps)?"true":"false")<<",\"turn_calls\":"<<request.turn_calls<<",\"turn_applied\":"<<request.turn_applied<<"}\n";
    if(result) throw std::runtime_error("Input session command failed; inspect status/log");
    return 0;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 1 || (argc == 2 && std::wstring(argv[1]) == L"--help")) {
        std::wcout << L"Witness VR loader and cursor session control\n"
            L"  witness-probe-loader --pid N --game-exe ABSOLUTE_PATH --log ABSOLUTE_PATH [--duration-ms 5000]\n"
            L"  --test-host restricts the target to the sibling test executable.\n"
            L"  --render-trace selects the pinned render diagnostic DLL; hooks are bounded but DLL stays until exit.\n"
            L"  --defer-cursor experimentally delays scene-eye submission until after cursor drawing.\n"
            L"  --defer-menu experimentally delays scene-eye submission until after its stereo menu pass.\n"
            L"  --redraw-pause experimentally redraws paused scenes and submits after the stereo menu.\n"
            L"  --cursor-session start|stop|enable|disable|status; --log required for start only.\n"
            L"  --controller-session start|stop|enable|disable|status; start accepts --controller-aim; --log required for start only.\n"
            L"  --controller-trace [--controller-move] [--controller-aim | --controller-aim-trace] bounded input test.\n"
            L"  --controller-snap [--snap-angle 22.5|45|90] for capture/session start; --controller-snap-trace for passive capture.\n"
            L"  Pointing session tuning: --aim-speed-percent 10..300 (default 80), --aim-smoothing-ms 0..250 (default 80).\n"
            L"  --controller-legacy-axis0 explicitly accepts the observed shared stick/trackpad compatibility axis.\n";
        return argc == 1 ? 1 : 0;
    }
    try {
        const auto config = options(argc, argv);
        const auto dll = std::filesystem::path(module_path()).parent_path() /
            (config.controller_trace ? L"witness-input-probe.dll" : config.render_trace ? L"witness-render-probe.dll" : L"witness-probe.dll");
        const DWORD rights = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;
        Handle process(OpenProcess(rights, FALSE, config.pid));
        if (!process) throw win_error("OpenProcess");
        if (!same_path(process_path(process.get()), config.expected_exe))
            throw std::runtime_error("Target process path does not match the expected executable; nothing injected");
        require_x64(process.get());
        if (config.controller_trace) {
            for (const auto& m : modules(config.pid)) {
                if (!_wcsicmp(m.name.c_str(), L"witness-input-probe.dll") && !same_path(m.path,dll))
                    throw std::runtime_error("A different input DLL build remains loaded; close and restart the game before changing builds");
            }
        }
        if (find_module(config.pid, dll) && !config.render_trace && !config.controller_trace)
            throw std::runtime_error("Probe is already loaded. Wait for the other probe, or restart the game before retrying");
        // Serialize loaders targeting this PID to prevent refcount/unload races.
        const auto mutex_name = L"Local\\WitnessVrProbeLoader-" + std::to_wstring(config.pid);
        Handle mutex(CreateMutexW(nullptr, FALSE, mutex_name.c_str()));
        if (!mutex) throw win_error("CreateMutexW");
        const DWORD lock = WaitForSingleObject(mutex.get(), 0);
        if (lock != WAIT_OBJECT_0 && lock != WAIT_ABANDONED)
            throw std::runtime_error("Another probe loader is already attached to this process");

        if (!config.session.empty()) return config.controller_session ? input_session_control(config, process.get(), dll) : session_control(config, process.get(), dll);

        const auto log_path = config.log.wstring();
        if (log_path.size() >= kPathCapacity) throw std::runtime_error("Log path is too long");
        if (std::filesystem::exists(config.log)) throw std::runtime_error("Refusing to overwrite an existing log; choose a new filename");
        std::filesystem::create_directories(config.log.parent_path());
        ProbeRequest request{};
        request.size = sizeof(request);
        request.version = kProtocolVersion;
        request.duration_ms = config.duration_ms;
        request.reserved = config.controller_trace ? ((config.controller_move ? 1 : 0) | (config.controller_legacy_axis0 ? 2 : 0) | (config.controller_aim ? 4 : 0) | (config.controller_aim_trace ? 8 : 0) | (config.controller_snap ? (16 | (config.snap_steps==1?64:config.snap_steps==4?128:0)) : 0) | (config.controller_snap_trace?32:0)) : config.redraw_pause ? 3 : config.defer_menu ? 2 : config.defer_cursor ? 1 : 0;
        std::copy(log_path.begin(), log_path.end(), request.log_path);

        // Resolve the export offset locally; DllMain performs no initialization.
        LocalLibrary local(dll);
        const auto run = GetProcAddress(local.value, "WitnessProbeRun");
        if (!run) throw win_error("GetProcAddress (WitnessProbeRun)");
        const auto offset = reinterpret_cast<std::uintptr_t>(run) - reinterpret_cast<std::uintptr_t>(local.value);
        const auto load = remote_system_function(config.pid, "LoadLibraryW");
        const auto unload = remote_system_function(config.pid, "FreeLibrary");
        const auto dll_path = dll.wstring();
        RemoteBuffer path_buffer(process.get(), dll_path.c_str(), (dll_path.size() + 1) * sizeof(wchar_t));
        RemoteBuffer request_buffer(process.get(), &request, sizeof(request));
        std::wcout << L"Observing PID " << config.pid << L" for " << config.duration_ms << L" ms.\n";
        if ((!config.render_trace && !config.controller_trace) || !find_module(config.pid, dll))
            remote_call(process.get(), load, path_buffer.value, 15000, &path_buffer);
        // Thread exit codes are 32-bit; do not use LoadLibrary's truncated return
        // value as an HMODULE. Find the full x64 address in the module snapshot.
        const auto remote_base = find_module(config.pid, dll);
        if (!remote_base) throw std::runtime_error("DLL did not load in target (check matching architecture/dependencies)");
        const auto result = remote_call(process.get(), remote_base + offset, request_buffer.value,
            config.duration_ms + 15000, &request_buffer);
        // A normal exported return means there are no probe worker threads or
        // installed hooks. It is now safe to unload this module.
        if (result > static_cast<DWORD>(ProbeResult::module_snapshot_failed))
            throw std::runtime_error("Probe thread terminated unexpectedly; restart the target before retrying");
        if (!config.render_trace && !config.controller_trace) {
            const auto unloaded = remote_call(process.get(), unload, reinterpret_cast<void*>(remote_base), 15000);
            if (!unloaded || find_module(config.pid, dll))
                throw std::runtime_error("Probe did not unload completely; restart target before rebuilding");
        }
        if (result != static_cast<DWORD>(ProbeResult::success))
            throw std::runtime_error("Probe returned error " + std::to_string(result));
        if (!std::filesystem::exists(config.log) || std::filesystem::file_size(config.log) == 0)
            throw std::runtime_error("Probe finished without a log");
        std::wcout << ((config.render_trace || config.controller_trace) ? L"Capture finished; see log for hook/session status. DLL retained until game exit. Log: " :
            L"Probe finished and unloaded. Log: ") << config.log.wstring() << L"\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
