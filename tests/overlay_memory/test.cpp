#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "companion/overlay/vendor/libtesla/include/playwise_glyph_cache.hpp"
#include "companion/overlay/vendor/libtesla/include/playwise_poll_policy.hpp"

static_assert(tsl::detail::inputPollNs(true, false) == 20'000'000ULL);
static_assert(tsl::detail::inputPollNs(false, true) == 20'000'000ULL);
static_assert(tsl::detail::inputPollNs(false, false) == 50'000'000ULL);
static_assert(tsl::detail::hiddenPollNs(true, 250'000'000ULL) == UINT64_MAX);
static_assert(tsl::detail::hiddenPollNs(false, 250'000'000ULL) == 250'000'000ULL);
static_assert(tsl::detail::hiddenPollNs(false, UINT64_MAX) == UINT64_MAX);
static_assert(tsl::detail::HOME_COOLDOWN_NS == 3'000'000'000ULL);
static_assert(tsl::detail::POWER_COOLDOWN_NS == 8'000'000'000ULL);
static_assert(tsl::detail::WAKE_JUMP_THRESHOLD_NS == 3'000'000'000ULL);
static_assert(tsl::detail::WAKE_EARLY_MIN_NS == 2'000'000'000ULL);
static_assert(tsl::detail::WAKE_NO_LOCK_EARLY_MIN_NS == 3'000'000'000ULL);
static_assert(tsl::detail::WAKE_UNLOCK_QUIET_NS == 800'000'000ULL);
static_assert(tsl::detail::WAKE_RECHECK_WINDOW_NS == 45'000'000'000ULL);
static_assert(tsl::detail::WAKE_POWER_COOLDOWN_SKEW_NS == 2'000'000'000ULL);
static_assert(tsl::detail::WAKE_UNLOCK_STEP_TIMEOUT_NS == 10'000'000'000ULL);
static_assert(tsl::detail::WAKE_UNLOCK_PRESSES_REQUIRED == 3);
static_assert(tsl::detail::saturatingAddNs(1000, 500) == 1500);
static_assert(tsl::detail::saturatingAddNs(UINT64_MAX - 10, 50) == UINT64_MAX);
static_assert(!tsl::detail::cooldownActive(1000, 0));
static_assert(tsl::detail::cooldownActive(1000, 4000));
static_assert(!tsl::detail::cooldownActive(4000, 4000));
static_assert(!tsl::detail::homeCooldownActive(1000, 0));
static_assert(tsl::detail::homeCooldownActive(1000, 4000));
static_assert(!tsl::detail::homeCooldownActive(4000, 4000));
static_assert(tsl::detail::maxCooldownUntilNs(1000, 4000) == 4000);
static_assert(tsl::detail::maxCooldownUntilNs(5000, 4000) == 5000);
static_assert(!tsl::detail::sleepJumpDetected(0, 10'000'000'000ULL, 250'000'000ULL));
static_assert(!tsl::detail::sleepJumpDetected(5000, 4000, 250'000'000ULL));
static_assert(!tsl::detail::sleepJumpDetected(1000, 20'000'000'000ULL, UINT64_MAX));
static_assert(!tsl::detail::sleepJumpDetected(1000, 1000 + 250'000'000ULL, 250'000'000ULL));
static_assert(!tsl::detail::sleepJumpDetected(1000, 1000 + 5'000'000'000ULL, 5'000'000'000ULL));
static_assert(tsl::detail::sleepJumpDetected(1000, 1000 + 3'250'000'000ULL, 250'000'000ULL));
static_assert(tsl::detail::sleepJumpDetected(1000, 1000 + 12'000'000'000ULL, 8'000'000'000ULL));
static_assert(!tsl::detail::sleepJumpDetected(1000, 1000 + 8'500'000'000ULL, 8'000'000'000ULL));
static_assert(tsl::detail::hiddenWaitNs(false, 1000, 4000, UINT64_MAX) == UINT64_MAX);
static_assert(tsl::detail::hiddenWaitNs(false, 1000, 4000, 250'000'000ULL) == 3000ULL);
static_assert(tsl::detail::hiddenWaitNs(false, 1000, 1000 + 8'000'000'000ULL, 250'000'000ULL) == 8'000'000'000ULL);
static_assert(tsl::detail::hiddenWaitNs(true, 1000, 1000 + 8'000'000'000ULL, 250'000'000ULL) == 250'000'000ULL);
static_assert(tsl::detail::hiddenWaitNs(true, 1000, 4000, 250'000'000ULL) == 3000ULL);
static_assert(tsl::detail::hiddenWaitNs(false, 5000, 4000, 250'000'000ULL) == 250'000'000ULL);
// Unlock-aware early cooldown release:
// - Mode::None keeps full 8s cooldown.
static_assert(tsl::detail::effectivePowerCooldownUntilNs(
    100'000'000'000ULL, 80'000'000'000ULL, 108'000'000'000ULL,
    tsl::detail::WakeEarlyMode::None, 101'000'000'000ULL,
    101'000'000'000ULL, 100'200'000'000ULL, 0) == 108'000'000'000ULL);
// - LockScreen: 1-2 presses (unlockCompletedNs <= wakeNs) keep full 8s cooldown even if user input occurred.
static_assert(tsl::detail::effectivePowerCooldownUntilNs(
    100'000'000'000ULL, 80'000'000'000ULL, 108'000'000'000ULL,
    tsl::detail::WakeEarlyMode::LockScreen, 99'000'000'000ULL,
    101'000'000'000ULL, 100'200'000'000ULL, 0) == 108'000'000'000ULL);
// - LockScreen: 3-press unlock completed releases at max(wakeNs + 2s, quietBase + 0.8s).
static_assert(tsl::detail::effectivePowerCooldownUntilNs(
    100'000'000'000ULL, 80'000'000'000ULL, 108'000'000'000ULL,
    tsl::detail::WakeEarlyMode::LockScreen, 101'000'000'000ULL,
    101'000'000'000ULL, 100'200'000'000ULL, 0) == 102'000'000'000ULL);
// - LockScreen: wake POWER button event arriving ~0.5s after suspend resume still allows early release.
static_assert(tsl::detail::effectivePowerCooldownUntilNs(
    100'000'000'000ULL, 80'000'000'000ULL, 108'500'000'000ULL,
    tsl::detail::WakeEarlyMode::LockScreen, 101'000'000'000ULL,
    101'000'000'000ULL, 100'500'000'000ULL, 0) == 102'000'000'000ULL);
static_assert(tsl::detail::effectivePowerCooldownUntilNs(
    100'000'000'000ULL, 80'000'000'000ULL, 108'000'000'000ULL,
    tsl::detail::WakeEarlyMode::LockScreen, 102'400'000'000ULL,
    102'500'000'000ULL, 100'200'000'000ULL, 0) == 103'300'000'000ULL);
static_assert(tsl::detail::effectivePowerCooldownUntilNs(
    100'000'000'000ULL, 80'000'000'000ULL, 108'000'000'000ULL,
    tsl::detail::WakeEarlyMode::LockScreen, 107'500'000'000ULL,
    107'500'000'000ULL, 100'200'000'000ULL, 0) == 108'000'000'000ULL);
// - A newer post-wake POWER press extends beyond wakeNs + 8s + 2s skew and must not be shortened.
static_assert(tsl::detail::effectivePowerCooldownUntilNs(
    100'000'000'000ULL, 80'000'000'000ULL, 112'000'000'000ULL,
    tsl::detail::WakeEarlyMode::LockScreen, 101'000'000'000ULL,
    101'000'000'000ULL, 104'000'000'000ULL, 0) == 112'000'000'000ULL);
// - NoLockScreen: dark wake (stale POWER event from sleep entry) stays full 8s.
static_assert(tsl::detail::effectivePowerCooldownUntilNs(
    100'000'000'000ULL, 80'000'000'000ULL, 108'000'000'000ULL,
    tsl::detail::WakeEarlyMode::NoLockScreen, 0,
    70'000'000'000ULL, 79'500'000'000ULL, 0) == 108'000'000'000ULL);
// - NoLockScreen: active POWER wake releases at wakeNs + 3s (including <=2s late POWER event skew).
static_assert(tsl::detail::effectivePowerCooldownUntilNs(
    100'000'000'000ULL, 80'000'000'000ULL, 108'500'000'000ULL,
    tsl::detail::WakeEarlyMode::NoLockScreen, 0,
    70'000'000'000ULL, 100'500'000'000ULL, 0) == 103'000'000'000ULL);
// - NoLockScreen: post-wake user input adds 0.8s quiet settle past 3s if needed.
static_assert(tsl::detail::effectivePowerCooldownUntilNs(
    100'000'000'000ULL, 80'000'000'000ULL, 108'500'000'000ULL,
    tsl::detail::WakeEarlyMode::NoLockScreen, 0,
    102'600'000'000ULL, 100'500'000'000ULL, 0) == 103'400'000'000ULL);
// - NoLockScreen: genuinely later post-wake POWER press (e.g. power menu) keeps full 8s.
static_assert(tsl::detail::effectivePowerCooldownUntilNs(
    100'000'000'000ULL, 80'000'000'000ULL, 112'000'000'000ULL,
    tsl::detail::WakeEarlyMode::NoLockScreen, 0,
    70'000'000'000ULL, 104'000'000'000ULL, 0) == 112'000'000'000ULL);

// Wake recheck gating: lock screen ON (requires 3-press/3-tap completion), lock screen OFF (3s + active wake), dark wake & expiration.
static_assert(tsl::detail::evaluateWakeRecheck(
    false, 100'000'000'000ULL, 80'000'000'000ULL, 102'000'000'000ULL,
    true, true, 101'000'000'000ULL, 101'000'000'000ULL, 0, 0) == tsl::detail::WakeRecheckGate::Inactive);
static_assert(tsl::detail::evaluateWakeRecheck(
    true, 100'000'000'000ULL, 80'000'000'000ULL, 99'000'000'000ULL,
    true, true, 101'000'000'000ULL, 101'000'000'000ULL, 0, 0) == tsl::detail::WakeRecheckGate::Expired);
static_assert(tsl::detail::evaluateWakeRecheck(
    true, 100'000'000'000ULL, 80'000'000'000ULL, 145'000'000'000ULL,
    true, true, 101'000'000'000ULL, 101'000'000'000ULL, 0, 0) == tsl::detail::WakeRecheckGate::Expired);
// Lock screen ON with only 1-2 presses (lastUserInputNs > wakeNs, but unlockCompletedNs == 0) must keep waiting!
static_assert(tsl::detail::evaluateWakeRecheck(
    true, 100'000'000'000ULL, 80'000'000'000ULL, 110'000'000'000ULL,
    true, true, 0, 101'000'000'000ULL, 100'200'000'000ULL, 0) == tsl::detail::WakeRecheckGate::Wait);
static_assert(tsl::detail::evaluateWakeRecheck(
    true, 100'000'000'000ULL, 80'000'000'000ULL, 101'900'000'000ULL,
    true, true, 101'000'000'000ULL, 101'000'000'000ULL, 0, 0) == tsl::detail::WakeRecheckGate::Wait);
static_assert(tsl::detail::evaluateWakeRecheck(
    true, 100'000'000'000ULL, 80'000'000'000ULL, 102'500'000'000ULL,
    true, true, 102'000'000'000ULL, 102'000'000'000ULL, 0, 0) == tsl::detail::WakeRecheckGate::Wait);
static_assert(tsl::detail::evaluateWakeRecheck(
    true, 100'000'000'000ULL, 80'000'000'000ULL, 102'000'000'000ULL,
    true, false, 101'000'000'000ULL, 101'000'000'000ULL, 0, 0) == tsl::detail::WakeRecheckGate::Wait);
static_assert(tsl::detail::evaluateWakeRecheck(
    true, 100'000'000'000ULL, 80'000'000'000ULL, 102'000'000'000ULL,
    true, true, 101'000'000'000ULL, 101'000'000'000ULL, 0, 0) == tsl::detail::WakeRecheckGate::Ready);
// Lock screen OFF: dark wake blocked, <3s waits, >=3s with active POWER/HOME signal is ready.
static_assert(tsl::detail::evaluateWakeRecheck(
    true, 100'000'000'000ULL, 80'000'000'000ULL, 108'000'000'000ULL,
    false, true, 0, 70'000'000'000ULL, 79'500'000'000ULL, 0) == tsl::detail::WakeRecheckGate::Wait);
static_assert(tsl::detail::evaluateWakeRecheck(
    true, 100'000'000'000ULL, 80'000'000'000ULL, 102'900'000'000ULL,
    false, true, 0, 70'000'000'000ULL, 100'200'000'000ULL, 0) == tsl::detail::WakeRecheckGate::Wait);
static_assert(tsl::detail::evaluateWakeRecheck(
    true, 100'000'000'000ULL, 80'000'000'000ULL, 103'000'000'000ULL,
    false, true, 0, 70'000'000'000ULL, 100'200'000'000ULL, 0) == tsl::detail::WakeRecheckGate::Ready);
static_assert(tsl::detail::evaluateWakeRecheck(
    true, 100'000'000'000ULL, 80'000'000'000ULL, 103'000'000'000ULL,
    false, true, 0, 70'000'000'000ULL, 0, 100'000'000'000ULL) == tsl::detail::WakeRecheckGate::Ready);
static_assert(tsl::detail::evaluateWakeRecheck(
    true, 100'000'000'000ULL, 80'000'000'000ULL, 103'200'000'000ULL,
    false, true, 0, 102'800'000'000ULL, 100'200'000'000ULL, 0) == tsl::detail::WakeRecheckGate::Wait);
static_assert(tsl::detail::evaluateWakeRecheck(
    true, 100'000'000'000ULL, 80'000'000'000ULL, 103'600'000'000ULL,
    false, true, 0, 102'800'000'000ULL, 100'200'000'000ULL, 0) == tsl::detail::WakeRecheckGate::Ready);

namespace {
std::size_t live_bytes = 0;
std::size_t live_bitmaps = 0;
std::size_t peak_bytes = 0;

struct BitmapDeleter {
    std::size_t bytes = 0;
    void operator()(unsigned char *bitmap) const {
        assert(live_bytes >= bytes && live_bitmaps > 0);
        live_bytes -= bytes;
        --live_bitmaps;
        std::free(bitmap);
    }
};

struct Glyph {
    std::unique_ptr<unsigned char, BitmapDeleter> bitmap;
};

template<typename Cache>
void rasterize(Cache &cache, std::uint64_t key, std::size_t bytes, bool fail = false) {
    auto *glyph = cache.insert(key, bytes);
    assert(glyph);
    if (fail) {
        cache.erase(key);
        assert(!cache.find(key));
        return;
    }
    auto *bitmap = static_cast<unsigned char *>(std::malloc(bytes));
    assert(bitmap);
    glyph->bitmap = std::unique_ptr<unsigned char, BitmapDeleter>(bitmap, BitmapDeleter{bytes});
    live_bytes += bytes;
    ++live_bitmaps;
    if (live_bytes > peak_bytes) peak_bytes = live_bytes;
    assert(live_bytes == cache.bytes());
    assert(live_bitmaps == cache.size());
}

void test_lru_and_limits() {
    tsl::gfx::detail::GlyphCache<Glyph, 4, 64> cache;
    for (std::uint64_t key = 1; key <= 4; ++key) rasterize(cache, key, 8);
    assert(cache.find(1)); // Touching a glyph protects it from the next eviction.
    rasterize(cache, 5, 8);
    assert(cache.find(1) && !cache.find(2));
    assert(cache.size() == 4 && cache.bytes() == 32);
    assert(!cache.insert(6, 65)); // Oversized bitmaps leave existing owners intact.
    assert(cache.size() == 4 && live_bytes == 32);
    rasterize(cache, 7, 48); // Evict before allocation, including byte-budget pressure.
    assert(cache.bytes() <= 64 && live_bytes <= 64);
    cache.erase(999);
    cache.clear();
    cache.clear();
    assert(cache.size() == 0 && cache.bytes() == 0);
    assert(live_bitmaps == 0 && live_bytes == 0);
}

void test_failed_bitmap_and_replacement() {
    tsl::gfx::detail::GlyphCache<Glyph, 4, 64> cache;
    rasterize(cache, 1, 16);
    rasterize(cache, 1, 24); // Replacing a key releases its previous allocation.
    assert(live_bitmaps == 1 && live_bytes == 24);
    rasterize(cache, 2, 40, true);
    assert(live_bitmaps == 1 && live_bytes == 24);
    rasterize(cache, 2, 40); // A failed allocation may be retried.
    assert(cache.size() == 2 && live_bytes == 64);
}

void test_sustained_language_size_churn() {
    constexpr std::size_t budget = 256 * 1024;
    for (int reopen = 0; reopen < 10; ++reopen) {
        tsl::gfx::detail::GlyphCache<Glyph, 512, budget> cache;
        for (std::uint64_t i = 0; i < 10000; ++i) {
            const auto character = 0x4e00 + i % 2000;
            const auto key = (character << 32) | ((i % 2) << 30) | ((14 + i % 29) * 16);
            if (!cache.find(key)) rasterize(cache, key, (14 + i % 29) * (14 + i % 29));
            assert(cache.size() <= 512 && cache.bytes() <= budget);
        }
        // Hiding retains a bounded working set; reopening hits it without allocation.
        assert(cache.find((UINT64_C(0x4e00) + 9999 % 2000) << 32 |
            (UINT64_C(9999 % 2) << 30) | ((14 + 9999 % 29) * 16)));
    }
    assert(live_bytes == 0 && live_bitmaps == 0);
    assert(peak_bytes <= budget);
}
void test_wake_unlock_tracker() {
    constexpr uint64_t BTN_A = UINT64_C(1) << 0;
    constexpr uint64_t BTN_B = UINT64_C(1) << 1;
    constexpr uint64_t STICK_LEFT = UINT64_C(1) << 16; // Analog stick pseudo-button
    tsl::detail::WakeUnlockTracker t;
    t.resetForWake(100'000'000'000ULL);
    // 1 press and 2 presses do not complete unlock.
    assert(!t.observeSample(100'500'000'000ULL, BTN_A, false));
    assert(!t.observeSample(100'800'000'000ULL, BTN_A, false));
    assert(t.completedNs == 0);
    // Different button resets streak to 1.
    assert(!t.observeSample(101'100'000'000ULL, BTN_B, false));
    assert(t.pressCount == 1);
    // Analog stick pseudo-button is ignored and does not complete unlock.
    assert(!t.observeSample(101'300'000'000ULL, STICK_LEFT, false));
    assert(t.pressCount == 1 && t.completedNs == 0);
    // Multi-button chord resets streak.
    assert(!t.observeSample(101'400'000'000ULL, BTN_A | BTN_B, false));
    assert(t.pressCount == 0);
    // Step timeout (>10s) resets streak.
    assert(!t.observeSample(102'000'000'000ULL, BTN_A, false));
    assert(!t.observeSample(102'300'000'000ULL, BTN_A, false));
    assert(!t.observeSample(113'000'000'000ULL, BTN_A, false));
    assert(t.pressCount == 1 && t.completedNs == 0);
    // 3 consecutive identical presses complete unlock on press #3 only.
    assert(!t.observeSample(113'300'000'000ULL, BTN_A, false));
    assert(t.observeSample(113'600'000'000ULL, BTN_A, false));
    assert(t.completedNs == 113'600'000'000ULL);
    assert(!t.observeSample(113'900'000'000ULL, BTN_A, false)); // single-shot per wake

    // New wake supports 3 consecutive touch screen taps (edge-triggered).
    t.resetForWake(200'000'000'000ULL);
    assert(!t.observeSample(200'500'000'000ULL, 0, true));  // tap 1 down
    assert(!t.observeSample(200'550'000'000ULL, 0, true));  // tap 1 held (ignored)
    assert(!t.observeSample(200'600'000'000ULL, 0, false)); // tap 1 up
    assert(!t.observeSample(200'800'000'000ULL, 0, true));  // tap 2 down
    assert(!t.observeSample(200'900'000'000ULL, 0, false)); // tap 2 up
    assert(t.observeSample(201'100'000'000ULL, 0, true));   // tap 3 down -> unlocked!
    assert(t.completedNs == 201'100'000'000ULL);

    // Mixing button and touch resets streak.
    t.resetForWake(300'000'000'000ULL);
    assert(!t.observeSample(300'500'000'000ULL, BTN_A, false));
    assert(!t.observeSample(300'800'000'000ULL, 0, true));
    assert(!t.observeSample(300'900'000'000ULL, 0, false));
    assert(!t.observeSample(301'100'000'000ULL, 0, true));
    assert(t.pressCount == 2 && t.completedNs == 0);
}
} // namespace

int main() {
    test_wake_unlock_tracker();
    test_lru_and_limits();
    test_failed_bitmap_and_replacement();
    assert(live_bytes == 0 && live_bitmaps == 0);
    test_sustained_language_size_churn();
    std::printf("overlay cache: 100000 inserts, peak=%zu bytes, live=%zu, bitmaps=%zu\n",
        peak_bytes, live_bytes, live_bitmaps);
}
