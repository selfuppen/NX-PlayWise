#pragma once
#include <cstdint>

namespace tsl::detail {
    // Held buttons retain fast sampling so chord completion and release remain
    // responsive. Hidden idle sampling only discovers the manual launch chord.
    constexpr uint64_t inputPollNs(bool visible, bool buttonsHeld) {
        return visible || buttonsHeld ? 20'000'000ULL : 50'000'000ULL;
    }
    constexpr uint64_t hiddenPollNs(bool blocked, uint64_t interval) {
        return blocked ? UINT64_MAX : interval;
    }
}
