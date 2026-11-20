#include <assert.h>
#include <stdio.h>
#include "switch.h"
#include "../../companion/nro/ptc_audio.h"
#include "../../companion/nro/ptc_audio_data.h"

static unsigned starts, stops, appends, flushes, queries, exits;
static bool started, fail_start, fail_stop, fail_flush, fail_append, fail_query, fail_init;
static AudioOutBuffer *queued[3];
static void drain(void) { for (int i = 0; i < 3; ++i) queued[i] = NULL; }
Result audoutInitialize(void) { return fail_init; }
void audoutExit(void) { ++exits; drain(); started = false; }
Result audoutStartAudioOut(void) { ++starts; if (fail_start) return 1; started = true; return 0; }
Result audoutStopAudioOut(void) { ++stops; if (fail_stop) return 1; started = false; return 0; }
Result audoutFlushAudioOutBuffers(bool *flushed) {
    ++flushes; if (fail_flush) return 1; drain(); *flushed = true; return 0;
}
Result audoutGetReleasedAudioOutBuffer(AudioOutBuffer **buffer, u32 *count) {
    ++queries; *buffer = NULL; *count = 0; return fail_query;
}
Result audoutContainsAudioOutBuffer(AudioOutBuffer *buffer, bool *contained) {
    ++queries; if (fail_query) return 1;
    *contained = false;
    for (int i = 0; i < 3; ++i) if (queued[i] == buffer) *contained = true;
    return 0;
}
Result audoutAppendAudioOutBuffer(AudioOutBuffer *buffer) {
    assert(started); if (fail_append) return 1;
    for (int i = 0; i < 3; ++i) assert(queued[i] != buffer);
    for (int i = 0; i < 3; ++i) if (!queued[i]) { queued[i] = buffer; ++appends; return 0; }
    assert(false); return 1;
}
void armDCacheFlush(void *buffer, size_t size) { assert(buffer && size); }
const PtcAudioPcmClip *ptc_audio_get_clip(int sound) {
    static const int16_t samples[] = {1, -1, 8, -8};
    static const PtcAudioPcmClip clip = {samples, 4};
    return sound == PTC_SE_NONE ? NULL : &clip;
}

int main(void)
{
    fail_init = true; assert(!ptc_audio_init()); fail_init = false;
    assert(ptc_audio_init() && ptc_audio_init() && !starts);
    ptc_audio_poll(); assert(!queries && !stops);
    ptc_audio_set_enabled(false); ptc_audio_play(PTC_SE_FOCUS); assert(!starts && !appends);
    ptc_audio_set_enabled(true);
    fail_start = true; ptc_audio_play(PTC_SE_FOCUS); assert(!appends); fail_start = false;
    ptc_audio_play(PTC_SE_FOCUS); assert(started && appends == 1);
    unsigned count = stops;
    ptc_audio_poll(); assert(stops == count);
    ptc_audio_play(PTC_SE_STEP); ptc_audio_play(PTC_SE_CONFIRM); ptc_audio_play(PTC_SE_CANCEL);
    assert(appends == 3); /* The fourth sound never overwrites a busy buffer. */
    fail_query = true; drain(); ptc_audio_poll(); assert(started); fail_query = false;
    ptc_audio_poll(); assert(!started);
    unsigned idle_queries = queries;
    for (int i = 0; i < 100; ++i) ptc_audio_poll();
    assert(queries == idle_queries);
    ptc_audio_play(PTC_SE_HOLD_CHARGE); assert(started && appends == 4);
    fail_stop = true; ptc_audio_stop();
    ptc_audio_play(PTC_SE_CONFIRM); assert(appends == 4);
    fail_stop = false; fail_flush = true; ptc_audio_stop();
    ptc_audio_play(PTC_SE_CONFIRM); assert(!started && appends == 4);
    fail_flush = false; ptc_audio_poll();
    ptc_audio_play(PTC_SE_CONFIRM); assert(started && appends == 5);
    ptc_audio_set_enabled(false); assert(!started);
    ptc_audio_play(PTC_SE_FOCUS); assert(appends == 5);
    ptc_audio_set_enabled(true); fail_append = true;
    ptc_audio_play(PTC_SE_FOCUS); assert(!started); fail_append = false;
    ptc_audio_play(PTC_SE_FOCUS); assert(started && appends == 6);
    ptc_audio_exit(); ptc_audio_poll(); assert(exits == 1 && !started && flushes);
    puts("PASS: demand audio start, drain/disable/abort stop, busy buffers and service failures");
    return 0;
}
