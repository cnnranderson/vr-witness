#pragma once
#include <cstdint>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>

namespace witness {
inline constexpr std::size_t session_log_limit = 2 * 1024 * 1024;

// An empty path disables logging; an explicit path must be absolute and writable.
inline std::unique_ptr<std::ofstream> open_session_log(const std::filesystem::path& path) {
    auto log = std::make_unique<std::ofstream>();
    if (!path.empty()) {
        if (!path.is_absolute())
            throw std::runtime_error("Session log path must be absolute");
        log->open(path, std::ios::binary | std::ios::trunc);
        if (!*log)
            throw std::runtime_error("Cannot open session log");
    }
    return log;
}

// Write complete records only; reaching the cap or a disk error must not stop VR.
inline bool write_session_log(std::ofstream& log, std::string_view record,
                              std::size_t limit = session_log_limit) {
    if (!log.is_open() || !log)
        return false;
    log.seekp(0, std::ios::end);
    const auto position = log.tellp();
    if (position < 0 || static_cast<std::uint64_t>(position) > limit ||
        record.size() > limit - static_cast<std::size_t>(position)) {
        log.close();
        log.setstate(std::ios::failbit);
        return false;
    }
    log.write(record.data(), static_cast<std::streamsize>(record.size()));
    log.flush();
    return static_cast<bool>(log);
}
} // namespace witness
