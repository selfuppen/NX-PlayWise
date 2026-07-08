#ifndef PTC_COMPANION_REQUEST_CLIENT_H
#define PTC_COMPANION_REQUEST_CLIENT_H

#include <stddef.h>
#include <stdint.h>

int ptc_companion_status_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at);
int ptc_companion_offline_code_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at, const char *code);
int ptc_companion_parent_minutes_request_json(char *out, size_t out_size, const char *request_id, int64_t created_at, const char *type, uint16_t minutes);

#endif
