#pragma once
#include <cstdint>

namespace tsl::detail {
    constexpr uint64_t HOME_COOLDOWN_NS = 3'000'000'000ULL;
    constexpr uint64_t POWER_COOLDOWN_NS = 8'000'000'000ULL;
    constexpr uint64_t WAKE_JUMP_THRESHOLD_NS = 3'000'000'000ULL;
    constexpr uint64_t WAKE_EARLY_MIN_NS = 2'000'000'000ULL;
    constexpr uint64_t WAKE_UNLOCK_QUIET_NS = 800'000'000ULL;
    constexpr uint64_t WAKE_RECHECK_WINDOW_NS = 45'000'000'000ULL;
    constexpr uint64_t WAKE_BUTTON_SKEW_NS = 1'000'000'000ULL;
    // On real Switch hardware, the POWER button wake event can arrive up to
    // ~1-2s after kernel suspend resume, extending powerCooldownUntilNs slightly
    // beyond wakeNs + POWER_COOLDOWN_NS. Treat extensions within this skew as
    // belonging to the same wake, while still preserving genuinely later POWER presses.
    constexpr uint64_t WAKE_POWER_COOLDOWN_SKEW_NS = 2'000'000'000ULL;

    enum class WakeRecheckGate : uint8_t {
        Inactive = 0,
        Wait,
        Ready,
        Expired,
    };

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

    // When the system lock screen is enabled and post-wake user input has been
    // observed (e.g. 3-press unlock), allow releasing the 8s POWER cooldown once
    // at least 2s have elapsed since wake AND input has stayed quiet for 0.8s.
    // Never extends beyond powerCooldownUntilNs and never shortens HOME cooldown.
    constexpr uint64_t effectivePowerCooldownUntilNs(
        uint64_t wakeNs, uint64_t powerCooldownUntilNs,
        bool lockScreenEnabled, uint64_t lastUserInputNs) {
        if (!lockScreenEnabled || wakeNs == 0 || powerCooldownUntilNs == 0 ||
            lastUserInputNs <= wakeNs ||
            powerCooldownUntilNs > saturatingAddNs(wakeNs, POWER_COOLDOWN_NS + WAKE_POWER_COOLDOWN_SKEW_NS)) {
            return powerCooldownUntilNs;
        }
        uint64_t minDeadline = saturatingAddNs(wakeNs, WAKE_EARLY_MIN_NS);
        uint64_t quietDeadline = saturatingAddNs(lastUserInputNs, WAKE_UNLOCK_QUIET_NS);
        uint64_t earlyDeadline = maxCooldownUntilNs(minDeadline, quietDeadline);
        return earlyDeadline < powerCooldownUntilNs ? earlyDeadline : powerCooldownUntilNs;
    }

    // Gate post-sleep wake rechecks so screen-off dark wakes and active lock-screen
    // button sequences never trigger an automatic overlay popup.
    constexpr WakeRecheckGate evaluateWakeRecheck(
        bool pending, uint64_t wakeNs, uint64_t wakeStartNs, uint64_t nowNs,
        bool lockScreenEnabled, bool neutral,
        uint64_t lastUserInputNs, uint64_t lastPowerEventNs, uint64_t lastHomeEventNs) {
        if (!pending || wakeNs == 0) return WakeRecheckGate::Inactive;
        if (nowNs < wakeNs) return WakeRecheckGate::Expired;
        if (nowNs - wakeNs >= WAKE_RECHECK_WINDOW_NS) return WakeRecheckGate::Expired;
        const bool sawUserInput = lastUserInputNs > wakeNs;
        if (lockScreenEnabled) {
            if (!sawUserInput || !neutral) return WakeRecheckGate::Wait;
            if (nowNs < lastUserInputNs) return WakeRecheckGate::Expired;
            if (nowNs - wakeNs < WAKE_EARLY_MIN_NS ||
                nowNs - lastUserInputNs < WAKE_UNLOCK_QUIET_NS) {
                return WakeRecheckGate::Wait;
            }
            return WakeRecheckGate::Ready;
        }
        const uint64_t postSleepFloor = saturatingAddNs(wakeStartNs, WAKE_BUTTON_SKEW_NS);
        const uint64_t wakeLowerBound = wakeNs > WAKE_BUTTON_SKEW_NS ? wakeNs - WAKE_BUTTON_SKEW_NS : 0;
        const bool powerSignal = lastPowerEventNs > postSleepFloor && lastPowerEventNs >= wakeLowerBound;
        const bool homeSignal = lastHomeEventNs > postSleepFloor && lastHomeEventNs >= wakeLowerBound;
        if (!sawUserInput && !powerSignal && !homeSignal) return WakeRecheckGate::Wait;
        if (!neutral) return WakeRecheckGate::Wait;
        if (sawUserInput && (nowNs < lastUserInputNs || nowNs - lastUserInputNs < WAKE_UNLOCK_QUIET_NS)) {
            return WakeRecheckGate::Wait;
        }
        return WakeRecheckGate::Ready;
    }
}
