#include "launcher/services/platform.hpp"
#include <fstream>

namespace witness::launcher {
std::wstring wide(const std::string& text) {
    if (text.empty())
        return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                          static_cast<int>(text.size()), nullptr, 0);
    if (!count)
        throw win_error("Decode UTF-8");
    std::wstring out(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(),
                        count);
    return out;
}

std::string read_file(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), {}};
}

std::vector<DWORD> process_ids(const wchar_t* name) {
    Handle list(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!list)
        throw win_error("List processes");
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    std::vector<DWORD> ids;
    if (Process32FirstW(list.get(), &entry))
        do {
            if (!_wcsicmp(entry.szExeFile, name))
                ids.push_back(entry.th32ProcessID);
        } while (Process32NextW(list.get(), &entry));
    return ids;
}

std::wstring log_stamp() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t text[80]{};
    swprintf(text, 80, L"%04u%02u%02u-%02u%02u%02u-%03u-%lu", now.wYear, now.wMonth, now.wDay, now.wHour,
             now.wMinute, now.wSecond, now.wMilliseconds, GetCurrentProcessId());
    return text;
}

} // namespace witness::launcher
