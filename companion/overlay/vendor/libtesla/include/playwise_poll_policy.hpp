#pragma once
#include <cstdint>

namespace tsl::detail {
    constexpr uint64_t HOME_COOLDOWN_NS = 3'000'000'000ULL;
    constexpr uint64_t POWER_COOLDOWN_NS = 8'000'000'000ULL;
    constexpr uint64_t WAKE_JUMP_THRESHOLD_NS = 3'000'000'000ULL;
    constexpr uint64_t WAKE_EARLY_MIN_NS = 2'000'000'000ULL;
    // When system lock screen is disabled, there is no 3-press unlock delay;
    // 3s matches HOME_COOLDOWN_NS so display resume and native PCTL popup settle first.
    constexpr uint64_t WAKE_NO_LOCK_EARLY_MIN_NS = 3'000'000'000ULL;
    constexpr uint64_t WAKE_UNLOCK_QUIET_NS = 800'000'000ULL;
    constexpr uint64_t WAKE_RECHECK_WINDOW_NS = 45'000'000'000ULL;
    constexpr uint64_t WAKE_BUTTON_SKEW_NS = 1'000'000'000ULL;
    // On real Switch hardware, the POWER button wake event can arrive up to
    // ~2-3s after kernel suspend resume (e.g. while OLED display finishes waking),
    // extending powerCooldownUntilNs beyond wakeNs + POWER_COOLDOWN_NS. Treat
    // extensions within this skew as belonging to the same wake, while still
    // preserving genuinely later post-wake POWER presses (e.g. power menu).
    constexpr uint64_t WAKE_POWER_COOLDOWN_SKEW_NS = 3'000'000'000ULL;
    constexpr uint64_t WAKE_UNLOCK_STEP_TIMEOUT_NS = 10'000'000'000ULL;
    constexpr uint8_t WAKE_UNLOCK_PRESSES_REQUIRED = 3;
    // Physical buttons only (A..Down = bits 0..15, Joy-Con SL/SR = bits 24..27).
    // Excludes analog stick pseudo-buttons (bits 16..23) which do not unlock HOS lock screen.
    constexpr uint64_t WAKE_UNLOCK_BUTTON_MASK = 0x0F00FFFFULL;
    constexpr uint64_t WAKE_UNLOCK_TOKEN_TOUCH = UINT64_C(1) << 63;

    enum class WakeEarlyMode : uint8_t {
        None = 0,
        LockScreen,
        NoLockScreen,
    };

    enum class WakeRecheckGate : uint8_t {
        Inactive = 0,
        Wait,
        Ready,
        Expired,
    };

    // Tracks HOS native lock-screen unlock completion: 3 consecutive presses of
    // the same physical button or 3 consecutive touchscreen taps after wake.
    struct WakeUnlockTracker {
        uint64_t wakeNs = 0;
        uint64_t lastToken = 0;
        uint64_t lastPressNs = 0;
        uint64_t completedNs = 0;
        uint8_t pressCount = 0;
        bool prevTouchActive = false;

        constexpr void resetForWake(uint64_t newWakeNs) {
            wakeNs = newWakeNs;
            lastToken = 0;
            lastPressNs = 0;
            completedNs = 0;
            pressCount = 0;
            prevTouchActive = false;
        }

        constexpr bool observeSample(uint64_t nowNs, uint64_t keysDown, bool touchActive) {
            const bool touchEdge = touchActive && !prevTouchActive;
            prevTouchActive = touchActive;
            if (wakeNs == 0 || nowNs <= wakeNs || completedNs > wakeNs) return false;
            const uint64_t physDown = keysDown & WAKE_UNLOCK_BUTTON_MASK;
            uint64_t token = 0;
            if (physDown != 0 && touchEdge) {
                lastToken = 0;
                pressCount = 0;
                return false;
            }
            if (physDown != 0) {
                if ((physDown & (physDown - 1)) != 0) {
                    lastToken = 0;
                    pressCount = 0;
                    return false;
                }
                token = physDown;
            } else if (touchEdge) {
                token = WAKE_UNLOCK_TOKEN_TOUCH;
            } else {
                return false;
            }
            if (token == lastToken && lastPressNs > wakeNs && nowNs >= lastPressNs &&
                nowNs - lastPressNs <= WAKE_UNLOCK_STEP_TIMEOUT_NS) {
                ++pressCount;
            } else {
                lastToken = token;
                pressCount = 1;
            }
            lastPressNs = nowNs;
            if (pressCount >= WAKE_UNLOCK_PRESSES_REQUIRED) {
                completedNs = nowNs;
                return true;
            }
            return false;
        }
    };

    // Held buttons or active touch retain fast sampling so chord completion and
    // lock-screen taps remain responsive. Hidden idle avoids 50Hz wakes.
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

    // Anchor the active wake window to the latest wake-window POWER press when
    // the console auto-slept briefly (<3s kernel suspend) before the user woke
    // it again within the same 45s wake recheck window.
    constexpr uint64_t wakePowerAnchorNs(uint64_t wakeNs, uint64_t lastPowerEventNs) {
        if (wakeNs == 0) return 0;
        if (lastPowerEventNs > wakeNs && lastPowerEventNs - wakeNs < WAKE_RECHECK_WINDOW_NS) {
            return lastPowerEventNs;
        }
        return wakeNs;
    }

    // Distinguish user-initiated wakes from background screen-off dark wakes.
    constexpr bool activeWakeSignalPresent(
        uint64_t wakeNs, uint64_t wakeStartNs,
        uint64_t lastUserInputNs, uint64_t lastPowerEventNs, uint64_t lastHomeEventNs) {
        if (wakeNs == 0) return false;
        const bool sawUserInput = lastUserInputNs > wakeNs;
        const uint64_t postSleepFloor = saturatingAddNs(wakeStartNs, WAKE_BUTTON_SKEW_NS);
        const uint64_t wakeLowerBound = wakeNs > WAKE_BUTTON_SKEW_NS ? wakeNs - WAKE_BUTTON_SKEW_NS : 0;
        const bool powerSignal = lastPowerEventNs > postSleepFloor && lastPowerEventNs >= wakeLowerBound;
        const bool homeSignal = lastHomeEventNs > postSleepFloor && lastHomeEventNs >= wakeLowerBound;
        return sawUserInput || powerSignal || homeSignal;
    }

    // Release the 8s POWER cooldown early after a genuine sleep wake:
    // - LockScreen: only after 3 identical presses / 3 taps complete after wakeAnchor,
    //   >=2s post-anchor & 0.8s quiet.
    // - NoLockScreen: when woken via POWER, HOS still requires 1 press/tap to enter HOME
    //   (otherwise auto-sleeps in ~9s); once that post-anchor input occurs, release at
    //   max(anchor + 2s, lastUserInputNs + 0.8s). If woken via HOME (no POWER press),
    //   allow release at wakeNs + 3s (matching HOME_COOLDOWN_NS).
    // Never extends beyond powerCooldownUntilNs, never shortens HOME cooldown, and
    // never shortens dark wakes or non-wake POWER presses (Mode::None).
    constexpr uint64_t effectivePowerCooldownUntilNs(
        uint64_t wakeNs, uint64_t wakeStartNs, uint64_t powerCooldownUntilNs,
        WakeEarlyMode mode, uint64_t unlockCompletedNs,
        uint64_t lastUserInputNs, uint64_t lastPowerEventNs, uint64_t lastHomeEventNs) {
        if (mode == WakeEarlyMode::None || wakeNs == 0 || powerCooldownUntilNs == 0) {
            return powerCooldownUntilNs;
        }
        const uint64_t anchorNs = wakePowerAnchorNs(wakeNs, lastPowerEventNs);
        if (powerCooldownUntilNs > saturatingAddNs(anchorNs, POWER_COOLDOWN_NS + WAKE_POWER_COOLDOWN_SKEW_NS)) {
            return powerCooldownUntilNs;
        }
        if (mode == WakeEarlyMode::LockScreen) {
            if (unlockCompletedNs <= anchorNs) return powerCooldownUntilNs;
            uint64_t minDeadline = saturatingAddNs(anchorNs, WAKE_EARLY_MIN_NS);
            uint64_t quietBase = maxCooldownUntilNs(unlockCompletedNs, lastUserInputNs);
            uint64_t quietDeadline = saturatingAddNs(quietBase, WAKE_UNLOCK_QUIET_NS);
            uint64_t earlyDeadline = maxCooldownUntilNs(minDeadline, quietDeadline);
            return earlyDeadline < powerCooldownUntilNs ? earlyDeadline : powerCooldownUntilNs;
        }
        if (lastUserInputNs > anchorNs) {
            uint64_t minDeadline = saturatingAddNs(anchorNs, WAKE_EARLY_MIN_NS);
            uint64_t quietDeadline = saturatingAddNs(lastUserInputNs, WAKE_UNLOCK_QUIET_NS);
            uint64_t earlyDeadline = maxCooldownUntilNs(minDeadline, quietDeadline);
            return earlyDeadline < powerCooldownUntilNs ? earlyDeadline : powerCooldownUntilNs;
        }
        const uint64_t postSleepFloor = saturatingAddNs(wakeStartNs, WAKE_BUTTON_SKEW_NS);
        const uint64_t wakeLowerBound = wakeNs > WAKE_BUTTON_SKEW_NS ? wakeNs - WAKE_BUTTON_SKEW_NS : 0;
        const bool homeWake = lastHomeEventNs > postSleepFloor && lastHomeEventNs >= wakeLowerBound &&
            lastHomeEventNs >= anchorNs;
        if (!homeWake) return powerCooldownUntilNs;
        uint64_t minDeadline = saturatingAddNs(wakeNs, WAKE_NO_LOCK_EARLY_MIN_NS);
        return minDeadline < powerCooldownUntilNs ? minDeadline : powerCooldownUntilNs;
    }

    // Gate post-sleep wake rechecks so screen-off dark wakes, incomplete
    // lock-screen sequences (1-2 presses), and untouched pre-wake screens never pop up.
    constexpr WakeRecheckGate evaluateWakeRecheck(
        bool pending, uint64_t wakeNs, uint64_t wakeStartNs, uint64_t nowNs,
        bool lockScreenEnabled, bool neutral, uint64_t unlockCompletedNs,
        uint64_t lastUserInputNs, uint64_t lastPowerEventNs, uint64_t lastHomeEventNs) {
        if (!pending || wakeNs == 0) return WakeRecheckGate::Inactive;
        if (nowNs < wakeNs) return WakeRecheckGate::Expired;
        const uint64_t anchorNs = wakePowerAnchorNs(wakeNs, lastPowerEventNs);
        if (nowNs < anchorNs) return WakeRecheckGate::Expired;
        if (nowNs - anchorNs >= WAKE_RECHECK_WINDOW_NS) return WakeRecheckGate::Expired;
        if (lockScreenEnabled) {
            if (unlockCompletedNs <= anchorNs || !neutral) return WakeRecheckGate::Wait;
            const uint64_t quietBase = maxCooldownUntilNs(
                unlockCompletedNs, lastUserInputNs > anchorNs ? lastUserInputNs : 0);
            if (nowNs < quietBase) return WakeRecheckGate::Expired;
            if (nowNs - anchorNs < WAKE_EARLY_MIN_NS ||
                nowNs - quietBase < WAKE_UNLOCK_QUIET_NS) {
                return WakeRecheckGate::Wait;
            }
            return WakeRecheckGate::Ready;
        }
        if (lastUserInputNs > anchorNs) {
            if (!neutral) return WakeRecheckGate::Wait;
            if (nowNs < lastUserInputNs) return WakeRecheckGate::Expired;
            if (nowNs - anchorNs < WAKE_EARLY_MIN_NS ||
                nowNs - lastUserInputNs < WAKE_UNLOCK_QUIET_NS) {
                return WakeRecheckGate::Wait;
            }
            return WakeRecheckGate::Ready;
        }
        const uint64_t postSleepFloor = saturatingAddNs(wakeStartNs, WAKE_BUTTON_SKEW_NS);
        const uint64_t wakeLowerBound = wakeNs > WAKE_BUTTON_SKEW_NS ? wakeNs - WAKE_BUTTON_SKEW_NS : 0;
        const bool homeWake = lastHomeEventNs > postSleepFloor && lastHomeEventNs >= wakeLowerBound &&
            lastHomeEventNs >= anchorNs;
        if (!homeWake || !neutral || nowNs - wakeNs < WAKE_NO_LOCK_EARLY_MIN_NS) {
            return WakeRecheckGate::Wait;
        }
        return WakeRecheckGate::Ready;
    }
}
