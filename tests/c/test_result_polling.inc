/* Count real storage calls through the production transport and recovery reader. */
#include <limits.h>

static const PtcStorageVTable *poll_storage_original;
static unsigned poll_reads, poll_exists, poll_writes;
static int poll_event, poll_submits, poll_gets, poll_closes;
static PtcCompanionStatus poll_submit_status, poll_get_status;
static const char POLL_RESULT[] =
    "{\"version\":1,\"request_id\":\"poll-one\",\"type\":\"offline_code\",\"status\":\"ok\","
    "\"state\":{\"day_index\":2380,\"limited_today\":1,\"blocked_today\":0,\"unrestricted_today\":0,"
    "\"remaining_available\":true,\"remaining_minutes\":60,\"play_timer_enabled\":1,\"restricted_now\":0},"
    "\"completed_at\":1}";

static bool poll_read(PtcStorage *s, const char *path, char *out, size_t size)
{ ++poll_reads; return poll_storage_original->read_text(s, path, out, size); }
static bool poll_exists_path(PtcStorage *s, const char *path)
{ ++poll_exists; return poll_storage_original->exists(s, path); }
static bool poll_write(PtcStorage *s, const char *path, const char *text)
{ ++poll_writes; return poll_storage_original->write_text_atomic(s, path, text); }
static bool poll_connect(void *ctx) { (void)ctx; return true; }
static PtcCompanionStatus poll_submit(void *ctx, const char *id, const char *json, void **token)
{
    (void)ctx; (void)id; (void)json; ++poll_submits;
    *token = poll_submit_status == PTC_COMPANION_OK ? &poll_event : NULL;
    return poll_submit_status;
}
static int poll_event_status(void *ctx, void *token) { (void)ctx; (void)token; return poll_event; }
static PtcCompanionStatus poll_get(void *ctx, const char *id, char *out, size_t size)
{
    (void)ctx; (void)id; ++poll_gets;
    snprintf(out, size, "%s", POLL_RESULT);
    return poll_get_status;
}
static void poll_close(void *ctx, void *token) { (void)ctx; (void)token; ++poll_closes; }

static void test_result_polling(void)
{
    PtcMemStorage *mem = malloc(sizeof(*mem));
    check_true(mem != NULL, "poll fixture allocation"); if (!mem) return;
    ptc_mem_storage_init(mem);
    poll_storage_original = mem->storage.vtable;
    PtcStorageVTable counted = *poll_storage_original;
    counted.read_text = poll_read; counted.exists = poll_exists_path; counted.write_text_atomic = poll_write;
    mem->storage.vtable = &counted;
    PtcCompanionTransportClient client;
    char out[2048];
    ptc_companion_transport_init(&client, "app", &mem->storage, NULL, NULL);
    poll_reads = poll_exists = poll_writes = 0;
    check_int(ptc_companion_transport_submit_status(&client, "poll-one", 1), PTC_COMPANION_OK, "SD submit");
    for (int t = 0; t < 30000; t += 20)
        check_int(ptc_companion_transport_poll(&client, t ? 20 : 0, 30000, out, sizeof(out)),
            PTC_COMPANION_PENDING, "pending transport remains pending");
    check_true(poll_reads == 32 && poll_exists == 32 && poll_writes == 1,
        "30s missing receipt uses 32 read/existence pairs instead of 299, with only one submission");
    check_int(ptc_companion_transport_poll(&client, 20, 30000, out, sizeof(out)),
        PTC_COMPANION_TIMEOUT, "deadline is not extended by backoff");
    check_int(poll_reads, 32, "timeout performs no late storage read");
    check_true(!strcmp(client.active_request_id, "poll-one"), "timeout retains original request ID");

    /* A new request resets the wait, even after a long pending request. */
    ptc_companion_transport_submit_status(&client, "poll-one", 1);
    unsigned before = poll_reads;
    check_int(ptc_companion_transport_poll(&client, 99, 30000, out, sizeof(out)), PTC_COMPANION_PENDING,
        "first receipt check waits for 100ms");
    check_int(poll_reads, before, "no SD access before first check");
    poll_storage_original->write_text_atomic(&mem->storage, "app/results/poll-one.json", POLL_RESULT);
    check_int(ptc_companion_transport_poll(&client, 1, 30000, out, sizeof(out)), PTC_COMPANION_OK,
        "fast receipt still arrives at 100ms");
    poll_storage_original->remove_path(&mem->storage, "app/results/poll-one.json");

    /* Late success, persistence failure, malformed and mismatched results keep
       the same classifications and never create a second request. */
    for (int mode = 0; mode < 4; ++mode) {
        ptc_companion_transport_submit_status(&client, "poll-one", 1);
        ptc_companion_transport_poll(&client, 100, 30000, out, sizeof(out));
        before = poll_reads;
        if (mode == 0) poll_storage_original->write_text_atomic(&mem->storage, "app/results/poll-one.json", POLL_RESULT);
        if (mode == 1) poll_storage_original->write_text_atomic(&mem->storage, "app/inbox/done/poll-one.json", "{}");
        if (mode == 2) poll_storage_original->write_text_atomic(&mem->storage, "app/results/poll-one.json", "{}");
        if (mode == 3) {
            char mismatched[sizeof(POLL_RESULT)];
            strcpy(mismatched, POLL_RESULT);
            memcpy(strstr(mismatched, "poll-one"), "poll-two", 8);
            poll_storage_original->write_text_atomic(&mem->storage, "app/results/poll-one.json", mismatched);
        }
        check_int(ptc_companion_transport_poll(&client, 199, 30000, out, sizeof(out)), PTC_COMPANION_PENDING,
            "late receipt is not read before next deadline");
        check_int(poll_reads, before, "backoff avoids storage access");
        const PtcCompanionStatus expected[] = {PTC_COMPANION_OK, PTC_COMPANION_WRITE_FAILED,
            PTC_COMPANION_RESULT_INVALID, PTC_COMPANION_RESULT_MISMATCH};
        check_int(ptc_companion_transport_poll(&client, 1, 30000, out, sizeof(out)), expected[mode],
            "late receipt preserves success/failure classification");
        poll_storage_original->remove_path(&mem->storage, "app/results/poll-one.json");
        poll_storage_original->remove_path(&mem->storage, "app/inbox/done/poll-one.json");
    }
    client.elapsed_ms = INT_MAX - 10; client.next_file_poll_ms = client.elapsed_ms;
    client.file_poll_delay_ms = 1000;
    check_int(ptc_companion_transport_poll(&client, 1, -1, out, sizeof(out)), PTC_COMPANION_PENDING,
        "unbounded activation wait accepts saturated deadline");
    check_true(client.next_file_poll_ms == INT_MAX && client.elapsed_ms == INT_MAX - 9,
        "backoff deadline cannot overflow");

    const PtcCompanionIpcBackend ipc = {poll_connect, poll_submit, poll_event_status, poll_get, poll_close, NULL};
    ptc_companion_transport_init(&client, "app", &mem->storage, &ipc, NULL);
    poll_submit_status = PTC_COMPANION_OK; poll_get_status = PTC_COMPANION_OK;
    poll_event = poll_submits = poll_gets = poll_closes = 0;
    poll_reads = poll_exists = poll_writes = 0;
    ptc_companion_transport_submit_status(&client, "poll-one", 1);
    for (int i = 0; i < 200; ++i)
        check_int(ptc_companion_transport_poll(&client, 20, 30000, out, sizeof(out)), PTC_COMPANION_PENDING,
            "IPC event wait needs no SD polling");
    poll_event = 1;
    check_int(ptc_companion_transport_poll(&client, 0, 30000, out, sizeof(out)), PTC_COMPANION_OK,
        "signaled IPC result has no added backoff");
    check_true(!poll_reads && !poll_exists && !poll_writes && poll_submits == 1 && poll_gets == 1 && poll_closes == 1,
        "IPC event remains immediate and storage-free at the client");

    for (int lost = 0; lost < 3; ++lost) {
        poll_submit_status = lost == 0 ? PTC_COMPANION_PENDING : PTC_COMPANION_OK;
        poll_event = lost == 1 ? -1 : 1;
        poll_get_status = PTC_COMPANION_PENDING;
        ptc_companion_transport_submit_status(&client, "poll-one", 1);
        ptc_companion_transport_poll(&client, 100, 30000, out, sizeof(out));
        check_true(client.active == PTC_TRANSPORT_FILE && client.route == PTC_TRANSPORT_ROUTE_IPC_SD_RESULT,
            "uncertain submit, lost event and missing IPC result fall back to original receipt");
        poll_storage_original->write_text_atomic(&mem->storage, "app/results/poll-one.json", POLL_RESULT);
        check_int(ptc_companion_transport_poll(&client, 1000, 30000, out, sizeof(out)), PTC_COMPANION_OK,
            "same durable result is discovered after IPC failure");
        poll_storage_original->remove_path(&mem->storage, "app/results/poll-one.json");
    }
    check_true(poll_submits == 4 && poll_writes == 0, "no IPC failure resubmits through SD");
    ptc_companion_transport_cancel(&client);

    PtcCompanionResultPoll recovery = {0};
    poll_reads = poll_exists = poll_writes = 0;
    for (uint64_t ms = 0; ms < 30000; ms += 20)
        check_int(ptc_companion_poll_recovered_result(&recovery, &client.file, "poll-one", ms, out, sizeof(out)),
            PTC_COMPANION_PENDING, "recovery retains unknown outcome");
    check_true(poll_reads == 33 && poll_exists == 33 && !poll_writes,
        "recovery reads immediately, then backs off to at most once per second");
    before = poll_reads;
    check_int(ptc_companion_poll_recovered_result(&recovery, &client.file, "poll-two", 29981, out, sizeof(out)),
        PTC_COMPANION_PENDING, "new recovery request is checked immediately");
    check_int(poll_reads, before + 1, "recovery interval does not leak across request IDs");
    poll_storage_original->write_text_atomic(&mem->storage, "app/results/poll-one.json", POLL_RESULT);
    check_int(ptc_companion_poll_recovered_result(&recovery, &client.file, "poll-one", 1, out, sizeof(out)),
        PTC_COMPANION_OK, "clock rollback and original ID still discover late success");
    before = poll_reads;
    check_int(ptc_companion_poll_recovered_result(&recovery, &client.file, "poll-one", 0, out, sizeof(out)),
        PTC_COMPANION_OK, "same-ID monotonic rollback resets the polling interval");
    check_int(poll_reads, before + 1, "rollback does not starve recovery");
    poll_storage_original->write_text_atomic(&mem->storage, "app/results/poll-one.json", "{}");
    check_int(ptc_companion_poll_recovered_result(&recovery, &client.file, "poll-one", 100, out, sizeof(out)),
        PTC_COMPANION_RESULT_INVALID, "corrupt recovery receipt is rejected");
    before = poll_reads;
    check_int(ptc_companion_poll_recovered_result(&recovery, &client.file, "poll-one", 101, out, sizeof(out)),
        PTC_COMPANION_PENDING, "corrupt receipt does not trigger a busy loop");
    check_int(poll_reads, before, "persistent malformed receipt is throttled");
    mem->fail_reads = true;
    check_int(ptc_companion_poll_recovered_result(&recovery, &client.file, "poll-one", 300, out, sizeof(out)),
        PTC_COMPANION_PENDING, "read failure retains the unknown redemption");
    mem->fail_reads = false;
    poll_storage_original->write_text_atomic(&mem->storage, "app/results/poll-one.json", POLL_RESULT);
    check_int(ptc_companion_poll_recovered_result(&recovery, &client.file, "poll-one", 700, out, sizeof(out)),
        PTC_COMPANION_OK, "recovery succeeds after storage failure clears");
    check_true(!poll_writes && !strcmp(recovery.request_id, "poll-one"), "recovery is read-only throughout");
    check_int(ptc_companion_poll_recovered_result(&recovery, &client.file, NULL, 800, out, sizeof(out)),
        PTC_COMPANION_BAD_ARGUMENT, "invalid recovery ID is rejected");
    free(mem);
}
