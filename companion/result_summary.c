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

static unsigned long long u64_value(const cJSON *object, const char *key)
{
    const cJSON *item = object ? cJSON_GetObjectItemCaseSensitive(object, key) : NULL;
    return cJSON_IsNumber(item) && item->valuedouble >= 0
        ? (unsigned long long)item->valuedouble : 0ULL;
}

bool ptc_companion_result_summary_parse(const char *result_json, PtcCompanionResultSummary *out)
{
    cJSON *root;
    const cJSON *state;
    const cJSON *error;
    const cJSON *preview;
    const cJSON *bedtime;
    const cJSON *restriction_reasons;
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
    out->day_index = number_value(state, "day_index", -1);
    out->remaining_available = bool_value(state, "remaining_available", false);
    out->remaining_minutes = number_value(state, "remaining_minutes", -1);
    out->played_minutes_available = bool_value(state, "played_minutes_available", false);
    out->played_minutes = number_value(state, "played_minutes", -1);
    out->play_timer_enabled = number_value(state, "play_timer_enabled", -1);
    out->restricted_now = number_value(state, "restricted_now", -1);
    out->unrestricted_today = number_value(state, "unrestricted_today", -1);
    out->temporary_unlocked_available = bool_value(state, "temporary_unlocked_available", false);
    out->temporary_unlocked = bool_value(state, "temporary_unlocked", false);
    out->calendar_covered = bool_value(state, "calendar_covered", false);
    out->calendar_update_warning = bool_value(state, "calendar_update_warning", false);
    snprintf(out->rule_source, sizeof(out->rule_source), "%s", string_value(state, "rule_source"));
    bedtime = cJSON_GetObjectItemCaseSensitive(state, "bedtime");
    out->bedtime_enabled = bool_value(bedtime, "enabled", false);
    out->bedtime_active = bool_value(bedtime, "active", false);
    out->bedtime_skipped = bool_value(bedtime, "skipped", false);
    out->bedtime_window_instance_id = u64_value(bedtime, "window_instance_id");
    out->bedtime_start_day_index = number_value(bedtime, "start_day_index", 0);
    out->bedtime_start_minute = number_value(bedtime, "start_minute", 0);
    out->bedtime_end_minute = number_value(bedtime, "end_minute", 0);
    out->bedtime_next_available = bool_value(bedtime, "next_available", false);
    out->bedtime_next_start_day_index = number_value(bedtime, "next_start_day_index", 0);
    out->bedtime_next_start_minute = number_value(bedtime, "next_start_minute", 0);
    out->bedtime_next_end_minute = number_value(bedtime, "next_end_minute", 0);
    out->bedtime_next_window_instance_id = u64_value(bedtime, "next_window_instance_id");
    out->bedtime_official_setting_confirmed = bool_value(bedtime, "official_setting_confirmed", false);
    out->bedtime_overlay_verified = bool_value(bedtime, "overlay_verified", false);
    snprintf(out->bedtime_source, sizeof(out->bedtime_source), "%s", string_value(bedtime, "source"));
    snprintf(out->bedtime_recovery_phase, sizeof(out->bedtime_recovery_phase), "%s",
        string_value(bedtime, "recovery_phase"));
    restriction_reasons = cJSON_GetObjectItemCaseSensitive(state, "restriction_reasons");
    out->daily_restriction_active = bool_value(restriction_reasons, "daily_allowance", false);
    out->access_recovery_required =
        (out->daily_restriction_active || (out->bedtime_active && !out->bedtime_skipped)) &&
        !(out->temporary_unlocked_available && out->temporary_unlocked);
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
    char remaining[32];
    const char *timer;
    const char *restriction;
    if (!summary || !out || out_size == 0 || !summary->valid) {
        return false;
    }
    if (summary->unrestricted_today == 1) {
        snprintf(remaining, sizeof(remaining), "不限时");
    } else if (summary->remaining_available && summary->remaining_minutes >= 0) {
        snprintf(remaining, sizeof(remaining), "%d 分钟", summary->remaining_minutes);
    } else {
        snprintf(remaining, sizeof(remaining), "暂不可用");
    }
    timer = summary->play_timer_enabled == 1 ? "已启动" :
        (summary->play_timer_enabled == 0 ? "未启动" : "未确认");
    restriction = summary->restricted_now == 1 ? "已报告" :
        (summary->restricted_now == 0 ? "未报告" : "未确认");
    written = snprintf(out, out_size, "%s  %s\n额度剩余：%s  额度已耗（估算）：%s%d%s\n计时器：%s  系统瞬时限制：%s",
        summary->ok ? "成功" : "失败",
        summary->ok ? "" : (summary->reason[0] ? summary->reason : "后台拒绝"),
        remaining,
        summary->played_minutes_available ? "约 " : "",
        summary->played_minutes_available ? summary->played_minutes : -1,
        summary->played_minutes_available ? " 分钟" : "（不可用）",
        timer,
        restriction);
    return written >= 0 && (size_t)written < out_size;
}
