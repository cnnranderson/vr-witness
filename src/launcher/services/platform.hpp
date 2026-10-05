#pragma once
#include "common/win_util.hpp"

namespace witness::launcher {
namespace fs = std::filesystem;
std::wstring wide(const std::string& text);
std::string read_file(const fs::path& path);
std::vector<DWORD> process_ids(const wchar_t* name);
std::wstring log_stamp();

class ProcessExited : public std::runtime_error {
public:
    ProcessExited() : std::runtime_error("The game has exited.") {}
};

// Open a process with wait rights; a PID that disappeared is a normal exit.
Handle open_game_process(DWORD pid, DWORD access);

// Keep the handle alive throughout the operation; only confirmed exit suppresses its failure.
template <class Operation> auto with_running_process(HANDLE process, Operation&& operation) {
    const auto state = WaitForSingleObject(process, 0);
    if (state == WAIT_OBJECT_0)
        throw ProcessExited{};
    if (state != WAIT_TIMEOUT)
        throw win_error("Check game process");
    try {
        return operation();
    } catch (...) {
        // Shutdown can reject remote calls before the process handle becomes signaled.
        if (WaitForSingleObject(process, 1000) == WAIT_OBJECT_0)
            throw ProcessExited{};
        throw;
    }
}

} // namespace witness::launcher
