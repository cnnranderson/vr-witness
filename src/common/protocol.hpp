#pragma once

#include <windows.h>
#include <cstdint>
#include <cstddef>

namespace witness {
inline constexpr std::uint32_t kProtocolVersion = 1;
inline constexpr std::uint32_t kMaxDurationMs = 30000;
inline constexpr std::size_t kPathCapacity = 1024;

// x64 loader/DLL wire layouts: preserve field order and bump the version on ABI changes.
struct ProbeRequest {
    std::uint32_t size;
    std::uint32_t version;
    std::uint32_t duration_ms;
    std::uint32_t reserved; // Diagnostic mode bits; see docs/architecture.md.
    wchar_t log_path[kPathCapacity];
};

static_assert(sizeof(wchar_t) == 2);
static_assert(sizeof(ProbeRequest) == 2064);

enum class SessionCommand : std::uint32_t { start = 1, stop, enable, disable, status };
enum class SessionState : std::uint32_t { stopped, running, stopping, failed };
inline constexpr std::uint32_t kSessionProtocolVersion = 2;

// Start accepts an absolute log_path or an empty path to disable logging.
// State, enabled, fault and counters are response fields.
struct SessionRequest {
    std::uint32_t size;
    std::uint32_t version;
    SessionCommand command;
    SessionState state;
    std::uint32_t enabled;
    std::uint32_t fault;
    std::uint64_t deferred;
    std::uint64_t submitted;
    std::uint64_t fallback;
    std::uint32_t logging; // Response: session logging was requested.
    wchar_t log_path[kPathCapacity];
};

static_assert(sizeof(SessionRequest) == 2104);

// Input protocol versions independently of the render protocol.
inline constexpr std::uint32_t kInputProtocolVersion = 6;

// Zero-initialize, set size/version/command, and supply valid speed/smoothing values for every command.
// Start consumes feature options/log_path; state, counters and active settings are returned in place.
struct InputSessionRequest {
    std::uint32_t size;
    std::uint32_t version;
    SessionCommand command;
    SessionState state;
    std::uint32_t enabled;
    std::uint32_t fault;
    std::uint32_t legacy_axis0;
    std::uint32_t pointing;
    std::uint32_t aim_speed_percent;
    std::uint32_t aim_smoothing_ms;
    std::uint32_t snap_steps;          // 0 disables turning; 1/2/4 mean 22.5/45/90 degrees.
    std::uint32_t stick_speed_percent; // Status output; must be zero in a request.
    std::uint64_t polls;
    std::uint64_t movement_calls;
    std::uint64_t applied;
    std::uint64_t cursor_calls;
    std::uint64_t aim_applied;
    std::uint64_t recenter_count;
    std::uint64_t turn_calls;
    std::uint64_t turn_applied;
    std::uint64_t stick_applied;
    std::uint64_t cancel_suppressed;
    std::uint64_t puzzle_back_presses;
    std::uint32_t logging; // Response: session logging was requested.
    wchar_t log_path[kPathCapacity];
};

static_assert(sizeof(InputSessionRequest) == 2192);

enum class ProbeResult : DWORD {
    success = 0,
    invalid_request = 1,
    cannot_open_log = 2,
    internal_error = 3,
    log_write_failed = 4,
    module_snapshot_failed = 5,
};
} // namespace witness
