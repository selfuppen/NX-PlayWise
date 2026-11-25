#pragma once
#include <cstdint>

namespace tsl::detail {
    constexpr uint64_t HOME_COOLDOWN_NS = 3'000'000'000ULL;

    // Held buttons retain fast sampling so chord completion and release remain
    // responsive. Hidden idle sampling only discovers the manual launch chord.
    constexpr uint64_t inputPollNs(bool visible, bool buttonsHeld) {
        return visible || buttonsHeld ? 20'000'000ULL : 50'000'000ULL;
    }
    constexpr uint64_t hiddenPollNs(bool blocked, uint64_t interval) {
        return blocked ? UINT64_MAX : interval;
    }
    constexpr bool homeCooldownActive(uint64_t nowNs, uint64_t untilNs) {
        return untilNs != 0 && nowNs < untilNs;
    }
    constexpr uint64_t hiddenWaitNs(bool powerBlocked, uint64_t nowNs, uint64_t homeCooldownUntilNs,
                                    uint64_t interval) {
        if (powerBlocked) return UINT64_MAX;
        if (homeCooldownActive(nowNs, homeCooldownUntilNs)) {
            uint64_t remaining = homeCooldownUntilNs - nowNs;
            return remaining < interval ? remaining : interval;
        }
        return interval;
    }
}
