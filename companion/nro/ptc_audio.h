#ifndef PTC_AUDIO_H
#define PTC_AUDIO_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PTC_SE_NONE = 0,
    PTC_SE_FOCUS = 1,       /* Cursor move / Tab switch / List navigation */
    PTC_SE_CONFIRM = 2,     /* Button A / Enter / Apply */
    PTC_SE_CANCEL = 3,      /* Button B / Back / Cancel dialog */
    PTC_SE_ERROR = 4,       /* PIN error / Prohibited / Locked action */
    PTC_SE_POPUP = 5,       /* Alert popup / Danger confirm dialog */
    PTC_SE_SUCCESS = 6      /* Time granted / Policy saved / PIN reset */
} PtcSoundEffect;

/**
 * Initialize audio output service (audout).
 * Gracefully handles emulator/unsupported environments.
 * @return true if initialized, false if unavailable.
 */
bool ptc_audio_init(void);

/**
 * Shut down audio output service.
 */
void ptc_audio_exit(void);

/**
 * Play an interaction sound effect non-blockingly.
 * @param se Sound effect identifier.
 */
void ptc_audio_play(PtcSoundEffect se);

/**
 * Enable or disable sound effects.
 */
void ptc_audio_set_enabled(bool enabled);

/**
 * Check if sound effects are enabled.
 */
bool ptc_audio_is_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* PTC_AUDIO_H */
