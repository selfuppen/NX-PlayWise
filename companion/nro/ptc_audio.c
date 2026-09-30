#include "ptc_audio.h"
#include "ptc_audio_data.h"

#include <string.h>
#include <stdlib.h>

#if defined(__SWITCH__)
#include <switch.h>
#endif

#define PTC_AUDIO_BUFFER_COUNT 3
#define PTC_AUDIO_BUFFER_SIZE  0x10000 /* 64 KiB, page aligned */

static bool g_audio_initialized = false;
static bool g_audio_enabled = true;

#if defined(__SWITCH__)
static u8 g_audio_pcm_pools[PTC_AUDIO_BUFFER_COUNT][PTC_AUDIO_BUFFER_SIZE] __attribute__((aligned(0x1000)));
static AudioOutBuffer g_audio_buffers[PTC_AUDIO_BUFFER_COUNT];
static bool g_audio_buffer_busy[PTC_AUDIO_BUFFER_COUNT];
static int g_current_buffer_idx = 0;
#endif

bool ptc_audio_init(void)
{
#if defined(__SWITCH__)
    Result rc = audoutInitialize();
    if (R_FAILED(rc)) {
        g_audio_initialized = false;
        return false;
    }

    rc = audoutStartAudioOut();
    if (R_FAILED(rc)) {
        audoutExit();
        g_audio_initialized = false;
        return false;
    }

    memset(g_audio_pcm_pools, 0, sizeof(g_audio_pcm_pools));
    memset(g_audio_buffers, 0, sizeof(g_audio_buffers));
    memset(g_audio_buffer_busy, 0, sizeof(g_audio_buffer_busy));
    g_current_buffer_idx = 0;
    g_audio_initialized = true;
    return true;
#else
    g_audio_initialized = false;
    return false;
#endif
}

void ptc_audio_exit(void)
{
#if defined(__SWITCH__)
    if (g_audio_initialized) {
        audoutStopAudioOut();
        audoutExit();
        g_audio_initialized = false;
    }
#else
    g_audio_initialized = false;
#endif
}

void ptc_audio_set_enabled(bool enabled)
{
    g_audio_enabled = enabled;
}

bool ptc_audio_is_enabled(void)
{
    return g_audio_enabled;
}

void ptc_audio_play(PtcSoundEffect se)
{
    if (!g_audio_initialized || !g_audio_enabled || se == PTC_SE_NONE) {
        return;
    }

#if defined(__SWITCH__)
    const PtcAudioPcmClip *clip = ptc_audio_get_clip((int)se);
    if (!clip || !clip->samples || clip->sample_count == 0) {
        return;
    }

    /* Reap finished buffers */
    AudioOutBuffer *released = NULL;
    u32 released_count = 0;
    while (R_SUCCEEDED(audoutGetReleasedAudioOutBuffer(&released, &released_count)) && released_count > 0 && released != NULL) {
        for (int i = 0; i < PTC_AUDIO_BUFFER_COUNT; ++i) {
            if (released == &g_audio_buffers[i]) {
                g_audio_buffer_busy[i] = false;
                break;
            }
        }
    }

    /* Select next buffer slot */
    int buf_idx = -1;
    for (int i = 0; i < PTC_AUDIO_BUFFER_COUNT; ++i) {
        int candidate = (g_current_buffer_idx + i) % PTC_AUDIO_BUFFER_COUNT;
        if (!g_audio_buffer_busy[candidate]) {
            buf_idx = candidate;
            break;
        }
    }
    if (buf_idx < 0) {
        buf_idx = g_current_buffer_idx;
    }
    g_current_buffer_idx = (buf_idx + 1) % PTC_AUDIO_BUFFER_COUNT;

    int16_t *dst = (int16_t *)g_audio_pcm_pools[buf_idx];
    size_t sample_count = clip->sample_count;
    if (sample_count * 4 > PTC_AUDIO_BUFFER_SIZE) {
        sample_count = PTC_AUDIO_BUFFER_SIZE / 4;
    }

    /* Expand mono samples into stereo channel buffer */
    for (size_t i = 0; i < sample_count; ++i) {
        int16_t s = clip->samples[i];
        dst[i * 2 + 0] = s;
        dst[i * 2 + 1] = s;
    }

    u64 data_size = (u64)(sample_count * 4);
    u64 buffer_size = (data_size + 0xFFFULL) & ~0xFFFULL;

    memset(&g_audio_buffers[buf_idx], 0, sizeof(AudioOutBuffer));
    g_audio_buffers[buf_idx].buffer = g_audio_pcm_pools[buf_idx];
    g_audio_buffers[buf_idx].buffer_size = buffer_size;
    g_audio_buffers[buf_idx].data_size = data_size;
    g_audio_buffers[buf_idx].data_offset = 0;
    g_audio_buffers[buf_idx].next = NULL;

    Result rc = audoutAppendAudioOutBuffer(&g_audio_buffers[buf_idx]);
    if (R_SUCCEEDED(rc)) {
        g_audio_buffer_busy[buf_idx] = true;
    }
#endif
}
