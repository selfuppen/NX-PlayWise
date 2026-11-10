#ifndef PTC_ULTRAHAND_CONFIG_H
#define PTC_ULTRAHAND_CONFIG_H
#include "storage.h"
typedef struct {
    int notifications; /* -1 unknown, 0 off, 1 on; never means receiver present. */
    char shortcut[96];
} PtcUltrahandConfig;
void ptc_ultrahand_config_read(PtcStorage *storage, PtcUltrahandConfig *out);
bool ptc_ultrahand_combo(const char *text, char *out, size_t size);
#endif
