#pragma once
#include "common/win_util.hpp"
#include <optional>

namespace witness {
// Process-local broadcast: the render worker owns F8; input consumes each published state once.
class FixesToggle {
    Handle mapping_;
    volatile LONG* state_{};
    LONG seen_{};

public:
    FixesToggle()
        : mapping_(CreateFileMappingW(
              INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(LONG),
              (L"Local\\WitnessVR-FixesToggle-v1-" + std::to_wstring(GetCurrentProcessId())).c_str())) {
        if (!mapping_)
            throw win_error("Create fixes toggle");
        state_ = static_cast<volatile LONG*>(
            MapViewOfFile(mapping_.get(), FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LONG)));
        if (!state_)
            throw win_error("Map fixes toggle");
        seen_ = InterlockedCompareExchange(state_, 0, 0);
    }

    ~FixesToggle() { UnmapViewOfFile(const_cast<LONG*>(state_)); }

    FixesToggle(const FixesToggle&) = delete;
    FixesToggle& operator=(const FixesToggle&) = delete;

    void publish(bool enabled) {
        LONG before = InterlockedCompareExchange(state_, 0, 0);
        for (;;) {
            const LONG next = ((before ^ 2) & 2) | (enabled ? 1 : 0);
            const auto actual = InterlockedCompareExchange(state_, next, before);
            if (actual == before)
                return;
            before = actual;
        }
    }

    std::optional<bool> consume() {
        const auto current = InterlockedCompareExchange(state_, 0, 0);
        if (current == seen_)
            return std::nullopt;
        seen_ = current;
        return (current & 1) != 0;
    }
};
} // namespace witness
