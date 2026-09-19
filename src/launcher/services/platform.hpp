#pragma once
#include "common/win_util.hpp"

namespace witness::launcher {
namespace fs = std::filesystem;
std::wstring wide(const std::string& text);
std::string read_file(const fs::path& path);
std::vector<DWORD> process_ids(const wchar_t* name);
std::wstring log_stamp();

} // namespace witness::launcher
