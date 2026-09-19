#pragma once
#include "launcher/model.hpp"

namespace witness::launcher {
enum class SessionKind { render, input };
// Escape a Windows command argument, including trailing slashes and quotes.
std::wstring quote_argument(const std::wstring& argument);
Session parse_session(const std::string& json, SessionKind kind);
// A failure may leave remote work in flight. The caller must prevent automatic retries.
Session send_session_command(const fs::path& root, DWORD pid, const fs::path& game_dir,
                             const Settings& settings, SessionKind kind, const wchar_t* action);

} // namespace witness::launcher
