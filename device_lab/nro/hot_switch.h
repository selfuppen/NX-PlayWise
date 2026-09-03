#ifndef PLAYWISE_DEVICE_LAB_HOT_SWITCH_H
#define PLAYWISE_DEVICE_LAB_HOT_SWITCH_H

#include <stdbool.h>
#include <stddef.h>

#include "../boot_flags.h"

typedef enum {
    PTC_HOT_SWITCH_COMPLETE = 0,
    PTC_HOT_SWITCH_REBOOT_REQUIRED = 1,
    PTC_HOT_SWITCH_REFUSED = 2
} PtcHotSwitchResult;

PtcHotSwitchResult ptc_hot_switch_enter(const PtcLabBootFlagPaths *paths,
    char *message, size_t message_size, char *technical, size_t technical_size);
PtcHotSwitchResult ptc_hot_switch_leave(const PtcLabBootFlagPaths *paths,
    bool standard_was_enabled, char *message, size_t message_size,
    char *technical, size_t technical_size);

#endif
