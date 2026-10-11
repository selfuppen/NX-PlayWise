#ifndef PTC_LAB_SELFDRIVE_H
#define PTC_LAB_SELFDRIVE_H

#ifdef PLAYWISE_DEVICE_LAB

#include <stdbool.h>
#include <stdint.h>

#include "sysmodule_core.h"
#include "../common/protocol/error_code.h"
#include "../common/protocol/request_schema.h"

/* Autonomous lab self-driver.
 *
 * Replays the operator's session_start -> phase_start* -> observation ->
 * session_restore steps for machine-decidable scenarios, honouring the real
 * phase deadlines, and writes an advisory evidence stream under
 * lab/selfdrive/. It NEVER fabricates a human observation (restriction
 * visibility / runtime effect are recorded as "self_driven_unobserved") and
 * never emits an authoritative campaign manifest -- authoritative
 * qualification stays human-attested. Lab-only (compiled out of every
 * standard/release/eden build via PLAYWISE_DEVICE_LAB). */

/* Arms the self-driver from a lab_selfdrive_start request. Refuses if a run is
 * already being driven, an authoritative campaign is active, or a prior lab
 * session has not finished and restored. Returns a PtcErrorCode. */
PtcErrorCode ptc_lab_selfdrive_arm(PtcSysmodule *sysmodule, const PtcRequest *request);

/* Advances the driver by at most a few lab-state transitions. Call once per
 * scheduler tick, right after ptc_lab_scheduler_tick. Returns 1 if it acted. */
int ptc_lab_selfdrive_tick(PtcSysmodule *sysmodule);

/* Shortens the scheduler wait while a self-driven run has a step pending
 * (i.e. is between phases rather than inside a timed sampling window). */
uint32_t ptc_lab_selfdrive_next_wait_ms(PtcSysmodule *sysmodule, uint32_t current_wait_ms);

#endif

#endif
