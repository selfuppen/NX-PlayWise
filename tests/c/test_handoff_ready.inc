#include "../../sysmodule/handoff_ready.h"

static const PtcStorageVTable *handoff_storage_base;
static unsigned handoff_write_calls, handoff_read_calls;
static bool handoff_fail_after_write;

static bool count_handoff_write(PtcStorage *s, const char *path, const char *text)
{
    ++handoff_write_calls;
    return handoff_storage_base->write_text_atomic(s, path, text) && !handoff_fail_after_write;
}

static bool count_handoff_read(PtcStorage *s, const char *path, char *text, size_t size)
{
    ++handoff_read_calls;
    return handoff_storage_base->read_text(s, path, text, size);
}

static void test_handoff_ready(void)
{
    PtcMemStorage *mem = malloc(sizeof(*mem));
    check_true(mem != NULL, "handoff fixture allocation"); if (!mem) return;
    ptc_mem_storage_init(mem);
    PtcStorage *s = &mem->storage;
    handoff_storage_base = s->vtable;
    PtcStorageVTable counted = *s->vtable;
    counted.write_text_atomic = count_handoff_write;
    counted.read_text = count_handoff_read;
    s->vtable = &counted;
    const char *path = "app/handover/ready.json";
    const char *ready = "{\"transaction_id\":\"one\",\"status\":\"ready\"}\n";
    const char *changed = "{\"transaction_id\":\"two\",\"status\":\"ready\"}\n";
    handoff_write_calls = handoff_read_calls = 0;
    bool retry_write = false;
    for (unsigned i = 0; i < 120; ++i)
        check_true(ptc_handoff_ready_write(s, path, ready, &retry_write), "stable handoff remains ready");
    check_int(handoff_write_calls, 1, "120 same handoff polls write once");
    check_int(handoff_read_calls, 120, "handoff confirmation is read fresh every poll");
    check_true(ptc_handoff_ready_write(s, path, changed, &retry_write), "same-length transaction change is written");
    check_int(handoff_write_calls, 2, "transaction change requires a write");
    check_true(s->vtable->remove_path(s, path) && ptc_handoff_ready_write(s, path, changed, &retry_write),
        "removed confirmation is recreated");
    check_int(handoff_write_calls, 3, "missing file is not a memory cache hit");
    char oversized[900]; memset(oversized, 'x', sizeof(oversized) - 1); oversized[899] = 0;
    const char *bad[] = {"{bad", "", oversized};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        check_true(handoff_storage_base->write_text_atomic(s, path, bad[i]), "seed invalid confirmation");
        unsigned writes = handoff_write_calls;
        check_true(ptc_handoff_ready_write(s, path, changed, &retry_write), "repair invalid or oversized confirmation");
        check_int(handoff_write_calls, writes + 1, "repair uses original atomic writer");
    }
    unsigned writes = handoff_write_calls;
    mem->fail_reads = true;
    check_true(ptc_handoff_ready_write(s, path, changed, &retry_write), "read failure preserves write attempt");
    check_int(handoff_write_calls, writes + 1, "failed read never skips write");
    mem->fail_reads = false;
    mem->fail_writes = true;
    check_true(!ptc_handoff_ready_write(s, path, ready, &retry_write), "write failure remains failure");
    char observed[128];
    check_true(s->vtable->read_text(s, path, observed, sizeof(observed)) && !strcmp(observed, changed),
        "failed write preserves prior confirmation");
    mem->fail_writes = false;
    check_true(ptc_handoff_ready_write(s, path, ready, &retry_write), "next poll retries failed write");
    check_true(s->vtable->read_text(s, path, observed, sizeof(observed)) && !strcmp(observed, ready),
        "retry persists current transaction");
    handoff_fail_after_write = true;
    check_true(!ptc_handoff_ready_write(s, path, changed, &retry_write) && retry_write &&
        s->vtable->read_text(s, path, observed, sizeof(observed)) && !strcmp(observed, changed),
        "failed commit may leave the new bytes readable");
    writes = handoff_write_calls;
    check_true(!ptc_handoff_ready_write(s, path, changed, &retry_write), "same bytes cannot hide repeated commit failure");
    check_int(handoff_write_calls, writes + 1, "readable failed write still retries persistence");
    handoff_fail_after_write = false;
    check_true(ptc_handoff_ready_write(s, path, changed, &retry_write) && !retry_write,
        "successful persistence clears retry state");
    writes = handoff_write_calls;
    check_true(ptc_handoff_ready_write(s, path, changed, &retry_write), "successful retry can be deduplicated");
    check_int(handoff_write_calls, writes, "only confirmed write success enables deduplication");
    free(mem);
}
