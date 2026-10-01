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
    PTC_SE_FOCUS = 1,          /* Cursor move / Grid navigation / List selection */
    PTC_SE_STEP = 2,           /* Minute / Date step / Dial wheel adjust */
    PTC_SE_TOGGLE = 3,         /* Master switch / Mode toggle (limit/unlimited) */
    PTC_SE_KEYSTROKE = 4,      /* PIN key tap / Numpad key / Backspace */
    PTC_SE_TAB = 5,            /* Page tab switch / History page turn / Section switch */
    PTC_SE_CONFIRM = 6,        /* Button A / Enter submenu / Apply */
    PTC_SE_CANCEL = 7,         /* Button B / Back / Discard draft / Close overlay */
    PTC_SE_POPUP = 8,          /* Notice popup / Details dialog / Info overlay */
    PTC_SE_DANGER = 9,         /* Danger confirm dialog / High-risk alert */
    PTC_SE_SUCCESS = 10,       /* Policy saved / Time granted / PIN updated */
    PTC_SE_CLAIM_BUFFER = 11,  /* Daily buffer claimed */
    PTC_SE_ERROR = 12,         /* PIN error / Prohibited / Locked action / Blocked input */
    PTC_SE_HOLD_CONFIRM = 13   /* Hold to confirm completed / High-risk action executed */
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
