#ifndef PTC_ULTRAHAND_CONFIG_H
#define PTC_ULTRAHAND_CONFIG_H
#include "storage.h"
typedef struct {
    int notifications; /* -1 unknown, 0 off, 1 on; never means receiver present. */
    char shortcut[96];
} PtcUltrahandConfig;
void ptc_ultrahand_config_read(PtcStorage *storage, PtcUltrahandConfig *out);
bool ptc_ultrahand_combo(const char *text, char *out, size_t size);
typedef enum {
    PTC_ULTRAHAND_PREPARE_UNAVAILABLE, PTC_ULTRAHAND_PREPARE_ALREADY_ON,
    PTC_ULTRAHAND_PREPARE_SAVED, PTC_ULTRAHAND_PREPARE_FAILED
} PtcUltrahandPrepare;
/* User enable action only. Changes one existing, unambiguous key and reads back.
   SAVED/ALREADY_ON prove SD configuration, never a resident receiver ACK. */
PtcUltrahandPrepare ptc_ultrahand_prepare_notifications(PtcStorage *storage);
#endif
