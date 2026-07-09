#ifndef PTC_SWITCH_TIME_PROVIDER_H
#define PTC_SWITCH_TIME_PROVIDER_H

#include "../time_provider.h"

typedef struct {
    PtcTimeProvider provider;
} PtcSwitchTimeProvider;

void ptc_switch_time_provider_init(PtcSwitchTimeProvider *provider);
PtcTimeProvider *ptc_switch_time_provider_as_provider(PtcSwitchTimeProvider *provider);

#endif
