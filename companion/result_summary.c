#include "result_summary.h"

#include <stdio.h>
#include <string.h>

#include "../common/protocol/result_builder.h"
#include "../third_party/cjson/cJSON.h"

static const char *string_value(const cJSON *object, const char *key)
{
    const cJSON *item = object ? cJSON_GetObjectItemCaseSensitive(object, key) : NULL;
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}

static int number_value(const cJSON *object, const char *key, int fallback)
{
    const cJSON *item = object ? cJSON_GetObjectItemCaseSensitive(object, key) : NULL;
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

static bool bool_value(const cJSON *object, const char *key, bool fallback)
{
    const cJSON *item = object ? cJSON_GetObjectItemCaseSensitive(object, key) : NULL;
    return cJSON_IsBool(item) ? cJSON_IsTrue(item) : fallback;
}

bool ptc_companion_result_summary_parse(const char *result_json, PtcCompanionResultSummary *out)
{
    cJSON *root;
    const cJSON *state;
    const cJSON *error;
    const cJSON *preview;
    const char *status;
    if (!out || !result_json || ptc_result_validate(result_json) != PTC_ERR_OK) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    root = cJSON_Parse(result_json);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    status = string_value(root, "status");
    snprintf(out->status, sizeof(out->status), "%s", status);
    snprintf(out->type, sizeof(out->type), "%s", string_value(root, "type"));
    out->ok = strcmp(status, "ok") == 0;
    state = cJSON_GetObjectItemCaseSensitive(root, "state");
    out->remaining_available = bool_value(state, "remaining_available", false);
    out->remaining_minutes = number_value(state, "remaining_minutes", -1);
    out->played_minutes_available = bool_value(state, "played_minutes_available", false);
    out->played_minutes = number_value(state, "played_minutes", -1);
    out->play_timer_enabled = number_value(state, "play_timer_enabled", -1);
    out->restricted_now = number_value(state, "restricted_now", -1);
    out->calendar_covered = bool_value(state, "calendar_covered", false);
    out->calendar_update_warning = bool_value(state, "calendar_update_warning", false);
    snprintf(out->rule_source, sizeof(out->rule_source), "%s", string_value(state, "rule_source"));
    preview = cJSON_GetObjectItemCaseSensitive(root, "preview");
    out->preview_available = cJSON_IsObject(preview);
    out->grant_minutes = number_value(preview, "grant_minutes", 0);
    out->remaining_after_available = bool_value(preview, "remaining_after_available", false);
    out->remaining_after_minutes = number_value(preview, "remaining_after_minutes", -1);
    out->effective_add_minutes = number_value(preview, "effective_add_minutes", 0);
    out->preview_capped = bool_value(preview, "capped", false);
    out->converts_unlimited_to_limited = bool_value(preview, "converts_unlimited_to_limited", false);
    error = cJSON_GetObjectItemCaseSensitive(root, "error");
    out->error_code = number_value(error, "code", 0);
    snprintf(out->reason, sizeof(out->reason), "%s", string_value(error, "reason"));
    snprintf(out->message, sizeof(out->message), "%s", string_value(error, "message"));
    out->unlock_observed = out->ok && strcmp(out->type, "offline_code") == 0 &&
        out->play_timer_enabled == 1 && out->restricted_now == 0;
    out->valid = true;
    cJSON_Delete(root);
    return true;
}

bool ptc_companion_result_summary_format(const PtcCompanionResultSummary *summary, char *out, size_t out_size)
{
    int written;
    if (!summary || !out || out_size == 0 || !summary->valid) {
        return false;
    }
    written = snprintf(out, out_size, "%s  %s\n剩余：%d 分钟  已玩：%s%d%s\n计时器：%s  限制：%s",
        summary->ok ? "成功" : "失败",
        summary->ok ? "" : (summary->reason[0] ? summary->reason : "后台拒绝"),
        summary->remaining_minutes,
        summary->played_minutes_available ? "约 " : "",
        summary->played_minutes_available ? summary->played_minutes : -1,
        summary->played_minutes_available ? " 分钟" : "（不可用）",
        summary->play_timer_enabled == 1 ? "已启动" : "未确认",
        summary->restricted_now == 0 ? "已解除" : "仍受限");
    return written >= 0 && (size_t)written < out_size;
}
