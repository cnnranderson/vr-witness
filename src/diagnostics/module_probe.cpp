#include "common/protocol.hpp"
#include "common/win_util.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

namespace {
std::string quote(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (const unsigned char c : value) {
        switch (c) {
        case '"':
            out << "\\\"";
            break;
        case '\\':
            out << "\\\\";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (c < 0x20)
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(c);
            else
                out << static_cast<char>(c);
        }
    }
    out << '"';
    return out.str();
}

std::string hex(std::uintptr_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << value;
    return out.str();
}

std::string timestamp() {
    SYSTEMTIME t{};
    GetSystemTime(&t);
    char value[40]{};
    std::snprintf(value, sizeof(value), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", t.wYear, t.wMonth, t.wDay,
                  t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    return value;
}

void event(std::ofstream& out, const char* name, const std::string& fields = {}) {
    out << "{\"time\":" << quote(timestamp()) << ",\"event\":" << quote(name)
        << ",\"pid\":" << GetCurrentProcessId() << fields << "}\n";
    out.flush();
}

bool interesting(const std::wstring& name) {
    const wchar_t* names[] = {L"d3d11.dll",        L"dxgi.dll",         L"openvr_api.dll",
                              L"vrclient_x64.dll", L"LibOVRRT64_1.dll", L"openxr_loader.dll"};
    for (const auto* expected : names)
        if (_wcsicmp(name.c_str(), expected) == 0)
            return true;
    return false;
}
} // namespace

// Initialize only after LoadLibraryW returns; DllMain performs no work under loader lock.
extern "C" __declspec(dllexport) DWORD WINAPI WitnessProbeRun(void* argument) noexcept {
    using namespace witness;
    if (!argument)
        return static_cast<DWORD>(ProbeResult::invalid_request);
    const auto request = *static_cast<const ProbeRequest*>(argument);
    if (request.size != sizeof(ProbeRequest) || request.version != kProtocolVersion ||
        request.reserved != 0 || request.duration_ms > kMaxDurationMs ||
        std::find(std::begin(request.log_path), std::end(request.log_path), L'\0') ==
            std::end(request.log_path))
        return static_cast<DWORD>(ProbeResult::invalid_request);

    try {
        const std::filesystem::path path(request.log_path);
        if (!path.is_absolute())
            return static_cast<DWORD>(ProbeResult::invalid_request);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
            return static_cast<DWORD>(ProbeResult::cannot_open_log);
        event(out, "probe.started",
              ",\"protocol\":1,\"mode\":\"observe_only\",\"game_exe\":" + quote(utf8(module_path())) +
                  ",\"duration_ms\":" + std::to_string(request.duration_ms));
        event(out, "probe.capabilities",
              ",\"camera_writes\":false,\"render_hooks\":false,\"vr_initialized_by_probe\":false");

        std::map<std::uintptr_t, std::wstring> seen;
        const ULONGLONG start = GetTickCount64();
        bool first = true;
        do {
            const auto loaded = modules(GetCurrentProcessId());
            std::map<std::uintptr_t, std::wstring> current;
            for (const auto& module : loaded) {
                current[module.base] = module.path;
                const auto old = seen.find(module.base);
                if (old != seen.end() && old->second == module.path)
                    continue;
                event(out, "module.loaded",
                      ",\"name\":" + quote(utf8(module.name)) + ",\"path\":" + quote(utf8(module.path)) +
                          ",\"base\":" + quote(hex(module.base)) +
                          ",\"image_size\":" + std::to_string(module.size) +
                          ",\"graphics_or_vr\":" + (interesting(module.name) ? "true" : "false"));
            }
            for (const auto& [base, name] : seen) {
                const auto now = current.find(base);
                if (now == current.end() || now->second != name)
                    event(out, "module.unloaded",
                          ",\"path\":" + quote(utf8(name)) + ",\"base\":" + quote(hex(base)));
            }
            if (first) {
                // A loaded module does not prove its VR implementation was entered.
                for (const auto* name : {L"d3d11.dll", L"dxgi.dll", L"openvr_api.dll", L"vrclient_x64.dll",
                                         L"LibOVRRT64_1.dll", L"openxr_loader.dll"}) {
                    const bool present =
                        std::any_of(loaded.begin(), loaded.end(), [name](const Module& module) {
                            return _wcsicmp(module.name.c_str(), name) == 0;
                        });
                    event(out, "runtime.snapshot",
                          ",\"module\":" + quote(utf8(name)) + ",\"loaded\":" + (present ? "true" : "false"));
                }
                first = false;
            }
            seen = std::move(current);
            const ULONGLONG elapsed = GetTickCount64() - start;
            if (elapsed >= request.duration_ms)
                break;
            Sleep(static_cast<DWORD>(std::min<ULONGLONG>(500, request.duration_ms - elapsed)));
        } while (true);
        event(out, "probe.finished", ",\"result\":\"success\"");
        return static_cast<DWORD>(out ? ProbeResult::success : ProbeResult::log_write_failed);
    } catch (...) {
        return static_cast<DWORD>(ProbeResult::internal_error);
    }
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
    return TRUE;
}
