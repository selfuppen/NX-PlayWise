#include "sysmodule_internal.h"

void append_event(PtcSysmodule *sysmodule, const PtcRequest *request, const char *event, PtcErrorCode error, const char *detail)
{
    char path[320];
    char line[512];
    PtcClockSnapshot now = sysmodule->time_provider->vtable->now(sysmodule->time_provider);
    if (!daily_log_path(sysmodule, "events.jsonl", path, sizeof(path))) return;
    snprintf(
        line,
        sizeof(line),
        "{\"ts\":%lld,\"request_id\":\"%s\",\"type\":\"%s\",\"event\":\"%s\",\"error\":\"%s\",\"detail\":\"%s\"}",
        (long long)now.unix_seconds,
        request ? request->request_id : "unknown",
        request ? request->type_text : "unknown",
        event,
        ptc_error_reason(error),
        detail ? detail : "");
    (void)sysmodule->storage->vtable->append_line(sysmodule->storage, path, line);
#ifndef __SWITCH__
    snprintf(path, sizeof(path), "%s/logs/events.jsonl", sysmodule->app_root);
    (void)sysmodule->storage->vtable->append_line(sysmodule->storage, path, line);
#endif
    /* A small, independently disposable summary feeds Support without parsing arbitrary logs. */
    if (strcmp(event, "result_error") == 0 || strcmp(event, "pctl_apply_failed") == 0 ||
        strcmp(event, "effect_restore") == 0 || strcmp(event, "effect_restore_failed") == 0 ||
        strcmp(event, "handover_preserved") == 0 || strcmp(event, "handover_restore") == 0 ||
        (strcmp(event, "result_ok") == 0 && request && strcmp(request->type_text, "status") != 0)) {
        char old[4096] = "";
        char summary_path[320];
        char output[4096] = "";
        const char *lines[20];
        size_t count = 0;
        char *cursor;
        join_path(summary_path, sizeof(summary_path), sysmodule->app_root, "support/recent-events.jsonl");
        (void)sysmodule->storage->vtable->read_text(sysmodule->storage, summary_path, old, sizeof(old));
        cursor = old;
        while (*cursor) {
            char *newline = strchr(cursor, '\n');
            if (count == 20) { memmove(lines, lines + 1, 19 * sizeof(lines[0])); count = 19; }
            lines[count++] = cursor;
            if (!newline) break;
            *newline = '\0';
            cursor = newline + 1;
        }
        if (count == 20) { memmove(lines, lines + 1, 19 * sizeof(lines[0])); count = 19; }
        for (size_t i = 0; i < count; ++i) {
            if (lines[i][0]) { strncat(output, lines[i], sizeof(output) - strlen(output) - 1); strncat(output, "\n", sizeof(output) - strlen(output) - 1); }
        }
        strncat(output, line, sizeof(output) - strlen(output) - 1);
        strncat(output, "\n", sizeof(output) - strlen(output) - 1);
        (void)sysmodule->storage->vtable->write_text_atomic(sysmodule->storage, summary_path, output);
    }
    /* Keep the legacy root log readable for older desktop/self-check tooling during migration. */
}

static void json_safe_copy(char *out, size_t out_size, const char *value)
{
    size_t used = 0;
    if (out_size == 0) {
        return;
    }
    if (!value) {
        out[0] = '\0';
        return;
    }
    while (*value && used + 1 < out_size) {
        unsigned char ch = (unsigned char)*value++;
        out[used++] = (ch >= 32U && ch != '"' && ch != '\\') ? (char)ch : '_';
    }
    out[used] = '\0';
}

static void empty_pctl_debug_snapshot(PtcPctlDebugSnapshot *out)
{
    memset(out, 0, sizeof(*out));
    out->available = false;
    out->error = PTC_ERR_PCTL_READ_FAILED;
}

void take_pctl_debug_snapshot(PtcSysmodule *sysmodule, PtcPctlDebugSnapshot *out)
{
    empty_pctl_debug_snapshot(out);
    if (!sysmodule->pctl || !sysmodule->pctl->vtable || !sysmodule->pctl->vtable->debug_snapshot) {
        return;
    }
    (void)sysmodule->pctl->vtable->debug_snapshot(sysmodule->pctl, out);
}

uint32_t last_pctl_ipc_result(PtcSysmodule *sysmodule)
{
    if (!sysmodule->pctl || !sysmodule->pctl->vtable || !sysmodule->pctl->vtable->last_ipc_result) {
        return 0;
    }
    return sysmodule->pctl->vtable->last_ipc_result(sysmodule->pctl);
}

void append_pctl_debug(
    PtcSysmodule *sysmodule,
    const PtcRequest *request,
    const char *stage,
    const char *mode,
    const PtcPctlTarget *target,
    PtcErrorCode error,
    uint32_t ipc_result,
    const PtcPctlDebugSnapshot *before,
    const PtcPctlDebugSnapshot *after)
{
    char path[320];
    char line[2048];
    char request_id[80];
    char type[80];
    char stage_safe[80];
    char mode_safe[32];
    char before_raw[160];
    char before_slots[320];
    char after_raw[160];
    char after_slots[320];
    const PtcPctlDebugSnapshot *before_snapshot = before;
    const PtcPctlDebugSnapshot *after_snapshot = after;
    PtcPctlDebugSnapshot empty_before;
    PtcPctlDebugSnapshot empty_after;

    if (!before_snapshot) {
        empty_pctl_debug_snapshot(&empty_before);
        before_snapshot = &empty_before;
    }
    if (!after_snapshot) {
        empty_pctl_debug_snapshot(&empty_after);
        after_snapshot = &empty_after;
    }
    json_safe_copy(request_id, sizeof(request_id), request ? request->request_id : "unknown");
    json_safe_copy(type, sizeof(type), request ? request->type_text : "unknown");
    json_safe_copy(stage_safe, sizeof(stage_safe), stage);
    json_safe_copy(mode_safe, sizeof(mode_safe), mode ? mode : "unknown");
    json_safe_copy(before_raw, sizeof(before_raw), before_snapshot->raw_hex);
    json_safe_copy(before_slots, sizeof(before_slots), before_snapshot->decoded_slots);
    json_safe_copy(after_raw, sizeof(after_raw), after_snapshot->raw_hex);
    json_safe_copy(after_slots, sizeof(after_slots), after_snapshot->decoded_slots);
    if (!daily_log_path(sysmodule, "pctl_debug.jsonl", path, sizeof(path))) return;
    snprintf(
        line,
        sizeof(line),
        "{\"ts\":%lld,\"request_id\":\"%s\",\"type\":\"%s\",\"stage\":\"%s\","
        "\"mode\":\"%s\",\"target_mode\":\"%s\",\"target_minutes\":%u,\"weekday\":%u,"
        "\"error\":\"%s\",\"ipc_result\":\"0x%08x\","
        "\"before_available\":%s,\"before_error\":\"%s\",\"before_ipc_result\":\"0x%08x\","
        "\"before_raw_hex\":\"%s\",\"before_slots\":\"%s\","
        "\"after_available\":%s,\"after_error\":\"%s\",\"after_ipc_result\":\"0x%08x\","
        "\"after_raw_hex\":\"%s\",\"after_slots\":\"%s\"}",
        (long long)sysmodule->time_provider->vtable->now(sysmodule->time_provider).unix_seconds,
        request_id,
        type,
        stage_safe,
        mode_safe,
        target ? pctl_target_mode_name(target->mode) : "none",
        target ? (unsigned int)target->minutes : 0U,
        target ? (unsigned int)target->weekday : 0U,
        ptc_error_reason(error),
        (unsigned int)ipc_result,
        before_snapshot->available ? "true" : "false",
        ptc_error_reason(before_snapshot->error),
        (unsigned int)before_snapshot->ipc_result,
        before_raw,
        before_slots,
        after_snapshot->available ? "true" : "false",
        ptc_error_reason(after_snapshot->error),
        (unsigned int)after_snapshot->ipc_result,
        after_raw,
        after_slots);
    (void)sysmodule->storage->vtable->append_line(sysmodule->storage, path, line);
#ifndef __SWITCH__
    snprintf(path, sizeof(path), "%s/logs/pctl_debug.jsonl", sysmodule->app_root);
    (void)sysmodule->storage->vtable->append_line(sysmodule->storage, path, line);
#endif
}
