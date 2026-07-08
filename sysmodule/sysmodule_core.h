#ifndef PTC_SYSMODULE_CORE_H
#define PTC_SYSMODULE_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "../platform/pctl.h"
#include "../platform/storage.h"
#include "../platform/time_provider.h"

typedef struct {
    char app_root[96];
    PtcStorage *storage;
    PtcPctl *pctl;
    PtcTimeProvider *time_provider;
} PtcSysmodule;

void ptc_sysmodule_init(
    PtcSysmodule *sysmodule,
    const char *app_root,
    PtcStorage *storage,
    PtcPctl *pctl,
    PtcTimeProvider *time_provider);
int ptc_sysmodule_recover_processing(PtcSysmodule *sysmodule);
int ptc_sysmodule_process_all(PtcSysmodule *sysmodule);

#endif
