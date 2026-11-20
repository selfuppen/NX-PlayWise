/* Frozen allocation-based writer payload, independent of the production path. */
static char *reference_reason_detail_json(const char *kind, const PtcReasonDetail *v)
{
    cJSON *j = cJSON_CreateObject();
    if (!j) return NULL;
    bool ok = cJSON_AddNumberToObject(j, "version", 1) && cJSON_AddStringToObject(j, "kind", kind) &&
        cJSON_AddStringToObject(j, "release", v->release) && cJSON_AddStringToObject(j, "boot", v->boot) &&
        cJSON_AddStringToObject(j, "owner", v->owner);
    const char *keys[] = {"ui_instance", "sequence", "issued_ms", "expires_ms", "instance", "day", "reason"};
    uint64_t values[] = {v->ui_instance, v->sequence, v->issued_ms, v->expires_ms, v->instance, v->day, v->reason};
    for (unsigned i = 0; i < 7; ++i) {
        char value[32]; snprintf(value, sizeof(value), "%llu", (unsigned long long)values[i]);
        ok = ok && cJSON_AddStringToObject(j, keys[i], value);
    }
    char *json = ok ? cJSON_PrintUnformatted(j) : NULL;
    cJSON_Delete(j);
    return json;
}

static void check_reason_detail_serialization(PtcStorage *s, const char *kind, const PtcReasonDetail *value)
{
    char *expected = reference_reason_detail_json(kind, value);
    check_true(expected != NULL, "reference detail serialization"); if (!expected) return;
    cJSON_Hooks hooks = {count_preference_allocation, free};
    preference_allocations = 0;
    cJSON_InitHooks(&hooks);
    bool written = ptc_reason_detail_write(s, "app", kind, value);
    cJSON_InitHooks(NULL);
    check_int(preference_allocations, 0, "detail writer never allocates JSON nodes or strings");
    check_true(written == (strlen(expected) < 1024), "detail writer retains exact wire capacity");
    if (written) {
        char path[256], actual[1024];
        snprintf(path, sizeof(path), "app/support/reason-detail-%s.json", kind);
        check_true(s->vtable->read_text(s, path, actual, sizeof(actual)) && !strcmp(actual, expected),
            "detail JSON matches old field order, numbers and escaping byte for byte");
    }
    cJSON_free(expected);
}

static void test_reason_detail_serialization(void)
{
    PtcMemStorage *mem = malloc(sizeof(*mem));
    check_true(mem != NULL, "detail serialization fixture"); if (!mem) return;
    ptc_mem_storage_init(mem);
    PtcStorage *s = &mem->storage;
    const char *kinds[] = {"ready", "request", "ack"};
    PtcReasonDetail value = {0}, observed;
    snprintf(value.release, sizeof(value.release), "release1");
    snprintf(value.boot, sizeof(value.boot), "0123456789abcdef");
    value.ui_instance = UINT64_MAX;
    value.issued_ms = UINT64_MAX - 1500;
    value.expires_ms = UINT64_MAX;
    value.day = UINT16_MAX;
    for (unsigned i = 0; i < 3; ++i) {
        value.sequence = i ? UINT64_MAX : 0;
        value.instance = i ? UINT64_MAX : 0;
        value.reason = i ? PTC_REASON_DOCK : PTC_REASON_NONE;
        snprintf(value.owner, sizeof(value.owner), "%s", i == 2 ? "0123456789abcdef0123456789abcdef" : "");
        check_reason_detail_serialization(s, kinds[i], &value);
        check_true(ptc_reason_detail_read(s, "app", kinds[i], &observed) &&
            ptc_reason_detail_same(&value, &observed) && !strcmp(value.owner, observed.owner),
            "ready request and ACK survive JSON roundtrip with maximum numeric values");
    }
    snprintf(value.release, sizeof(value.release), "quotes\" slash\\ \b\f\n\r\t \x01 中文");
    check_reason_detail_serialization(s, "ack", &value);
    check_true(ptc_reason_detail_read(s, "app", "ack", &observed) && !strcmp(value.release, observed.release),
        "escaped release roundtrip");
    value.release[0] = 0;
    char *base = reference_reason_detail_json("ack", &value);
    check_true(base != NULL, "capacity reference allocation");
    if (base) {
        size_t fixed = strlen(base); cJSON_free(base);
        for (size_t target = 1023; target <= 1025; ++target) {
            size_t extra = target - fixed, controls = extra / 6, plain = extra % 6;
            check_true(controls + plain < sizeof(value.release), "capacity fixture fits release storage");
            if (controls + plain >= sizeof(value.release)) continue;
            memset(value.release, 1, controls);
            memset(value.release + controls, 'x', plain);
            value.release[controls + plain] = 0;
            char *json = reference_reason_detail_json("ack", &value);
            check_true(json && strlen(json) == target, "capacity fixture has exact serialized size");
            cJSON_free(json);
            check_reason_detail_serialization(s, "ack", &value);
        }
    }
    snprintf(value.release, sizeof(value.release), "release1");
    mem->fail_writes = true;
    check_true(!ptc_reason_detail_write(s, "app", "ack", &value), "detail persistence failure is returned");
    mem->fail_writes = false;
    check_true(ptc_reason_detail_write(s, "app", "ack", &value), "detail persistence retries after failure");
    free(mem);
}
