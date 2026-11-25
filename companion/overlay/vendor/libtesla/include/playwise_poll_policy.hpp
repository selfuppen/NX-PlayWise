#pragma once
#include <cstdint>

namespace tsl::detail {
    constexpr uint64_t HOME_COOLDOWN_NS = 3'000'000'000ULL;
    constexpr uint64_t POWER_COOLDOWN_NS = 8'000'000'000ULL;
    constexpr uint64_t WAKE_JUMP_THRESHOLD_NS = 3'000'000'000ULL;

    // Held buttons retain fast sampling so chord completion and release remain
    // responsive. Hidden idle sampling only discovers the manual launch chord.
    constexpr uint64_t inputPollNs(bool visible, bool buttonsHeld) {
        return visible || buttonsHeld ? 20'000'000ULL : 50'000'000ULL;
    }
    constexpr uint64_t hiddenPollNs(bool blocked, uint64_t interval) {
        return blocked ? UINT64_MAX : interval;
    }
    constexpr uint64_t saturatingAddNs(uint64_t baseNs, uint64_t deltaNs) {
        return baseNs > UINT64_MAX - deltaNs ? UINT64_MAX : baseNs + deltaNs;
    }
    constexpr bool cooldownActive(uint64_t nowNs, uint64_t untilNs) {
        return untilNs != 0 && nowNs < untilNs;
    }
    constexpr bool homeCooldownActive(uint64_t nowNs, uint64_t untilNs) {
        return cooldownActive(nowNs, untilNs);
    }
    constexpr uint64_t maxCooldownUntilNs(uint64_t a, uint64_t b) {
        return a > b ? a : b;
    }
    // Detect kernel suspend/sleep when monotonic time advances far beyond the
    // bounded wait timeout. Unbounded waits and backward clocks never trigger.
    constexpr bool sleepJumpDetected(uint64_t prevNs, uint64_t nowNs, uint64_t waitedTimeoutNs) {
        if (prevNs == 0 || nowNs <= prevNs || waitedTimeoutNs == UINT64_MAX) return false;
        uint64_t elapsed = nowNs - prevNs;
        uint64_t limit = saturatingAddNs(waitedTimeoutNs, WAKE_JUMP_THRESHOLD_NS);
        return elapsed >= limit;
    }
    // While cooling down with no hidden frame drawn, wait out the full remaining
    // cooldown in one sleep (manual combo still wakes immediately via comboEvent).
    constexpr uint64_t hiddenWaitNs(bool hasHiddenFrame, uint64_t nowNs,
                                    uint64_t cooldownUntilNs, uint64_t interval) {
        if (interval == UINT64_MAX) return UINT64_MAX;
        if (cooldownActive(nowNs, cooldownUntilNs)) {
            uint64_t remaining = cooldownUntilNs - nowNs;
            if (hasHiddenFrame)
                return remaining < interval ? remaining : interval;
            return remaining;
        }
        return interval;
    }
}
