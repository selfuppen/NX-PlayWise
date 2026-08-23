#ifndef PTC_LAB_SESSION_H
#define PTC_LAB_SESSION_H

#ifdef PLAYWISE_DEVICE_LAB

#include <stdbool.h>

#include "sysmodule_core.h"
#include "../common/protocol/request_schema.h"

bool ptc_lab_request_type(PtcRequestType type);
bool ptc_lab_process_request(PtcSysmodule *sysmodule, const PtcRequest *request);
int ptc_lab_scheduler_tick(PtcSysmodule *sysmodule);
uint32_t ptc_lab_next_wait_ms(PtcSysmodule *sysmodule, uint32_t current_wait_ms);

#endif

#endif
