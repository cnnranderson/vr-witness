#pragma once

#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace witness {
inline std::runtime_error win_error(const char* operation) {
    return std::runtime_error(std::string(operation) + " failed (Windows error " +
                              std::to_string(GetLastError()) + ")");
}

// Own a CloseHandle-compatible Windows handle; null and INVALID_HANDLE_VALUE are empty.
class Handle {
public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    ~Handle() {
        if (value_ && value_ != INVALID_HANDLE_VALUE)
            CloseHandle(value_);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value_; }
    explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }

private:
    HANDLE value_;
};

inline std::wstring module_path(HMODULE module = nullptr) {
    std::wstring result(32768, L'\0');
    const DWORD count = GetModuleFileNameW(module, result.data(), static_cast<DWORD>(result.size()));
    if (!count || count >= result.size())
        throw win_error("GetModuleFileNameW");
    result.resize(count);
    return result;
}

inline std::wstring process_path(HANDLE process) {
    std::wstring result(32768, L'\0');
    DWORD count = static_cast<DWORD>(result.size());
    if (!QueryFullProcessImageNameW(process, 0, result.data(), &count))
        throw win_error("QueryFullProcessImageNameW");
    result.resize(count);
    return result;
}

inline bool same_path(const std::filesystem::path& a, const std::filesystem::path& b) {
    // Resolve relative segments and filesystem aliases before checking identity.
    std::error_code error;
    return std::filesystem::equivalent(a, b, error) && !error;
}

struct Module {
    std::wstring name;
    std::wstring path;
    std::uintptr_t base;
    DWORD size;
};

// Snapshot loaded modules, retrying transient loader-list changes before throwing.
inline std::vector<Module> modules(DWORD pid) {
    HANDLE raw = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 8; ++attempt) {
        raw = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (raw != INVALID_HANDLE_VALUE || GetLastError() != ERROR_BAD_LENGTH)
            break;
        Sleep(10);
    }
    Handle snapshot(raw);
    if (!snapshot)
        throw win_error("CreateToolhelp32Snapshot");
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Module32FirstW(snapshot.get(), &entry))
        throw win_error("Module32FirstW");
    std::vector<Module> result;
    do {
        result.push_back({entry.szModule, entry.szExePath,
                          reinterpret_cast<std::uintptr_t>(entry.modBaseAddr), entry.modBaseSize});
    } while (Module32NextW(snapshot.get(), &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES)
        throw win_error("Module32NextW");
    return result;
}

inline std::string utf8(const std::wstring& value) {
    if (value.empty())
        return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!count)
        throw win_error("WideCharToMultiByte");
    std::string result(static_cast<std::size_t>(count), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                             result.data(), count, nullptr, nullptr))
        throw win_error("WideCharToMultiByte");
    return result;
}
} // namespace witness
