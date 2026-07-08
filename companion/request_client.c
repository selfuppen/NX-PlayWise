#include "request_client.h"

#include <stdio.h>

int ptc_companion_status_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at)
{
    return snprintf(out, out_size, "{\"version\":1,\"request_id\":\"%s\",\"type\":\"status\",\"created_at\":%lld,\"payload\":{}}\n", request_id, (long long)created_at);
}

int ptc_companion_offline_code_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at, const char *code)
{
    return snprintf(out, out_size, "{\"version\":1,\"request_id\":\"%s\",\"type\":\"offline_code\",\"created_at\":%lld,\"payload\":{\"code\":\"%s\"}}\n", request_id, (long long)created_at, code);
}

int ptc_companion_parent_minutes_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at, const char *type, uint16_t minutes)
{
    return snprintf(out, out_size, "{\"version\":1,\"request_id\":\"%s\",\"type\":\"%s\",\"created_at\":%lld,\"payload\":{\"minutes\":%u}}\n", request_id, type, (long long)created_at, minutes);
}
