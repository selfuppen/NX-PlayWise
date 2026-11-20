#ifndef PTC_AUDIO_TEST_SWITCH_H
#define PTC_AUDIO_TEST_SWITCH_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int Result;
#define R_SUCCEEDED(rc) ((rc) == 0)
#define R_FAILED(rc) ((rc) != 0)
typedef struct AudioOutBuffer {
    struct AudioOutBuffer *next;
    void *buffer;
    u64 buffer_size, data_size, data_offset;
} AudioOutBuffer;
Result audoutInitialize(void);
void audoutExit(void);
Result audoutStartAudioOut(void);
Result audoutStopAudioOut(void);
Result audoutFlushAudioOutBuffers(bool *flushed);
Result audoutContainsAudioOutBuffer(AudioOutBuffer *buffer, bool *contained);
Result audoutGetReleasedAudioOutBuffer(AudioOutBuffer **buffer, u32 *count);
Result audoutAppendAudioOutBuffer(AudioOutBuffer *buffer);
void armDCacheFlush(void *buffer, size_t size);
#endif
