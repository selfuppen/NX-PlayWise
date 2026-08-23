#ifndef PTC_REQUEST_SCHEMA_H
#define PTC_REQUEST_SCHEMA_H

#include <stdbool.h>
#include <stdint.h>

#include "../rules/rules.h"
#include "error_code.h"
#include "request_type.h"

typedef struct {
    PtcRequestType type;
    char request_id[80];
    char type_text[40];
    int64_t created_at;
    char code[80];
    uint16_t minutes;
    PtcDayRule week[7];
    bool holiday_enabled;
    PtcDayRule holiday_rule;
    PtcDayRule makeup_workday_rule;
#ifdef PLAYWISE_DEVICE_LAB
    bool start_timer;
    bool wait_for_expiry;
    char phase[32];
    char observation[32];
#endif
} PtcRequest;

PtcErrorCode ptc_request_parse(const char *text, PtcRequest *out);
bool ptc_request_id_is_valid(const char *request_id);

#endif
