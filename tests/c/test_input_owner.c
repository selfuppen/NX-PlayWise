#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../companion/input_owner.h"
#include "../../platform/host/mem_storage.h"
#include "../../third_party/cjson/cJSON.h"

static PtcMemStorage mem;
static PtcInputOwnerWriter writer;
static PtcInputOwnerReader reader;
static PtcStorageVTable hooked;
static const PtcStorageVTable *original;
static int failures, writes;
static bool interleave, blocked_during_gap;
static const char *RELEASE = "playwise-test+aaaaaaaaaaaa.1234567890abcdef";
static const char *SLOT0 = "app/support/overlay-input-0123456789abcdef-0.json";
static const char *SLOT1 = "app/support/overlay-input-0123456789abcdef-1.json";
static const uint64_t START = UINT64_C(2000000000);

static void check(bool ok, const char *label)
{
    if (!ok) { fprintf(stderr, "FAIL input owner: %s\n", label); ++failures; }
}

static bool write_hook(PtcStorage *storage, const char *path, const char *text)
{
    ++writes;
    if (interleave && !strcmp(path, SLOT0)) {
        interleave = false;
        (void)original->remove_path(storage, path);
        blocked_during_gap = ptc_input_owner_block(&reader, START + PTC_INPUT_OWNER_RENEW_NS, true);
    }
    return original->write_text_atomic(storage, path, text);
}

static void put(const char *path, const char *value)
{
    check(mem.storage.vtable->write_text_atomic(&mem.storage, path, value), "fixture write");
}

static void runtime(const char *release, const char *boot)
{
    char text[384];
    snprintf(text, sizeof(text), "{\"profile\":\"release\",\"release_id\":\"%s\",\"boot_id\":\"%s\"}", release, boot);
    put("app/handover/runtime-ready.json", text);
}

static void fixture(void)
{
    ptc_mem_storage_init(&mem);
    original = mem.storage.vtable;
    hooked = *original;
    hooked.write_text_atomic = write_hook;
    mem.storage.vtable = &hooked;
    writes = 0;
    interleave = blocked_during_gap = false;
    runtime(RELEASE, "0123456789abcdef");
    put("app/state.json", "business-state-remains-unchanged");
    ptc_input_owner_writer_init(&writer, &mem.storage, "app", RELEASE, UINT64_C(1000000000));
    ptc_input_owner_reader_init(&reader, &mem.storage, "app", RELEASE);
}

static void change(const char *path, const char *key, cJSON *value)
{
    char text[512];
    cJSON *root;
    char *encoded;
    check(mem.storage.vtable->read_text(&mem.storage, path, text, sizeof(text)), "read mutation fixture");
    root = cJSON_Parse(text);
    cJSON_DeleteItemFromObjectCaseSensitive(root, key);
    cJSON_AddItemToObject(root, key, value);
    encoded = cJSON_PrintUnformatted(root);
    put(path, encoded);
    free(encoded);
    cJSON_Delete(root);
}

int main(void)
{
    char text[512], foreign[512];
    int count;
    fixture();
    check(!ptc_input_owner_block(&reader, START, true), "no owner leaves normal NRO input enabled");
    check(!ptc_input_owner_begin_ui(&writer), "no ready handshake before ownership");
    check(ptc_input_owner_acquire(&writer, START), "acquire both visible slots");
    check(ptc_input_owner_begin_ui(&writer) && !ptc_input_owner_begin_ui(&writer), "ready handshake starts once after ownership");
    check(ptc_input_owner_block(&reader, START + 1, true), "even first leaked key is blocked before any prior observation");
    check(ptc_input_owner_block(&reader, START + 2, false), "neutral input does not release a visible owner");
    ptc_input_owner_release(&writer, START + 3);
    check(ptc_input_owner_block(&reader, START + 4, true), "held key is drained after hide");
    check(!ptc_input_owner_block(&reader, START + PTC_INPUT_OWNER_POLL_NS + 4, false), "neutral frame acknowledges release");
    check(!ptc_input_owner_block(&reader, START + PTC_INPUT_OWNER_POLL_NS + 5, true), "new key works after release");
    count = writes;
    ptc_input_owner_release(&writer, START + PTC_INPUT_OWNER_RENEW_NS);
    check(writes == count, "exit after hide does not extend released lease");
    check(ptc_input_owner_acquire(&writer, START + PTC_INPUT_OWNER_RENEW_NS + 1), "re-show has a new visible epoch");
    check(!ptc_input_owner_begin_ui(&writer), "normal re-show does not repeat the recovery handshake");
    check(ptc_input_owner_block(&reader, START + PTC_INPUT_OWNER_RENEW_NS + 2, true), "new visible epoch blocks again");

    fixture();
    check(ptc_input_owner_acquire(&writer, START), "lease for renewal gap");
    interleave = true;
    check(ptc_input_owner_maintain(&writer, START + PTC_INPUT_OWNER_RENEW_NS), "renew while one slot is being replaced");
    check(blocked_during_gap, "other slot prevents an input gap during remove/rename");
    count = writes;
    check(ptc_input_owner_maintain(&writer, START + PTC_INPUT_OWNER_RENEW_NS + 1) && writes == count,
          "heartbeat is limited, not one SD write per frame");
    ptc_input_owner_release(&writer, START + PTC_INPUT_OWNER_RENEW_NS + 2);
    ptc_input_owner_reader_init(&reader, &mem.storage, "app", RELEASE);
    check(ptc_input_owner_block(&reader, START + PTC_INPUT_OWNER_RENEW_NS + 3, true), "late reader drains release even if it missed visibility");
    check(!ptc_input_owner_block(&reader, START + PTC_INPUT_OWNER_RENEW_NS + PTC_INPUT_OWNER_POLL_NS + 3, false), "late reader recovers on neutral");

    fixture();
    check(ptc_input_owner_acquire(&writer, START), "lease before simulated crash");
    check(ptc_input_owner_block(&reader, START + PTC_INPUT_OWNER_LEASE_NS - 1, true), "crashed owner blocks until absolute expiry");
    check(!ptc_input_owner_maintain(&writer, START + PTC_INPUT_OWNER_LEASE_NS), "late producer cannot revive expired lease");
    check(ptc_input_owner_block(&reader, START + PTC_INPUT_OWNER_LEASE_NS + 1, true), "expiry still drains held input");
    check(!ptc_input_owner_block(&reader, START + PTC_INPUT_OWNER_LEASE_NS + PTC_INPUT_OWNER_POLL_NS + 1, false), "expired owner does not permanently trap NRO");
    check(!ptc_input_owner_block(&reader, START + PTC_INPUT_OWNER_LEASE_NS + PTC_INPUT_OWNER_POLL_NS + 2, true), "fresh key after crash expiry works");

    fixture();
    mem.fail_write_path_contains = "-1.json";
    check(!ptc_input_owner_acquire(&writer, START) && !writer.active, "partial initial write never enables Overlay actions");
    check(!ptc_input_owner_begin_ui(&writer), "failed acquisition cannot falsely verify Overlay readiness");
    check(ptc_input_owner_block(&reader, START + 1, true), "first published slot still protects NRO on partial failure");
    mem.fail_write_path_contains = NULL;
    ptc_input_owner_release(&writer, START + 2);
    check(!ptc_input_owner_block(&reader, START + PTC_INPUT_OWNER_POLL_NS + 2, false), "partial acquisition can release its owned record");

    fixture();
    check(ptc_input_owner_acquire(&writer, START), "lease before failed renew");
    mem.fail_writes = true;
    check(!ptc_input_owner_maintain(&writer, START + PTC_INPUT_OWNER_RENEW_NS) && writer.failed, "renew failure disables producer");
    count = writes;
    check(!ptc_input_owner_maintain(&writer, START + PTC_INPUT_OWNER_RENEW_NS + 1) && writes == count,
          "failed producer does not retry indefinitely");
    ptc_input_owner_release(&writer, START + PTC_INPUT_OWNER_RENEW_NS + 2);
    mem.fail_writes = false;
    check(!ptc_input_owner_block(&reader, START + PTC_INPUT_OWNER_LEASE_NS + 1, false), "failed release recovers at last absolute deadline");

    fixture();
    check(ptc_input_owner_acquire(&writer, START), "lease before ownership conflict");
    change(SLOT1, "owner_id", cJSON_CreateString("ffffffffffffffffffffffffffffffff"));
    check(mem.storage.vtable->read_text(&mem.storage, SLOT1, foreign, sizeof(foreign)), "read foreign owner");
    check(!ptc_input_owner_maintain(&writer, START + PTC_INPUT_OWNER_RENEW_NS), "another owner is not silently overwritten");
    ptc_input_owner_release(&writer, START + PTC_INPUT_OWNER_RENEW_NS + 1);
    check(mem.storage.vtable->read_text(&mem.storage, SLOT1, text, sizeof(text)) && !strcmp(text, foreign), "cleanup preserves foreign record");

    fixture();
    check(ptc_input_owner_acquire(&writer, START), "lease before malformed data");
    change(SLOT0, "visible", cJSON_CreateNumber(0));
    check(ptc_input_owner_block(&reader, START + 1, true), "malformed bool does not unlock NRO");
    check(!ptc_input_owner_maintain(&writer, START + PTC_INPUT_OWNER_RENEW_NS), "malformed lease disables producer at renewal");

    fixture();
    check(ptc_input_owner_acquire(&writer, START), "lease before identity change");
    runtime(RELEASE, "fedcba9876543210");
    check(!ptc_input_owner_maintain(&writer, START + PTC_INPUT_OWNER_RENEW_NS), "backend boot change stops old producer");
    check(!ptc_input_owner_block(&reader, START + PTC_INPUT_OWNER_RENEW_NS + 1, false), "old boot files do not own new boot input");
    runtime("older-release", "fedcba9876543210");
    check(!ptc_input_owner_block(&reader, START + PTC_INPUT_OWNER_RENEW_NS + 2, true), "normal hot update remains usable against known older backend");

    fixture();
    mem.fail_read_path_contains = "overlay-input-";
    check(ptc_input_owner_block(&reader, START, true), "unknown slot stat is not proof of no owner");
    check(!ptc_input_owner_acquire(&writer, START), "producer refuses unknown slot metadata");
    mem.fail_read_path_contains = NULL;
    check(!ptc_input_owner_block(&reader, START + PTC_INPUT_OWNER_POLL_NS, false), "neutral input recovers once absence is known");
    mem.fail_read_path_contains = "runtime-ready";
    check(ptc_input_owner_block(&reader, START + PTC_INPUT_OWNER_POLL_NS + 1, true), "unknown runtime cannot unlock a bound reader");
    mem.fail_read_path_contains = NULL;
    check(ptc_input_owner_block(&reader, START - 1, true), "clock rollback does not unlock input");
    check(!ptc_input_owner_block(&reader, START + 2 * PTC_INPUT_OWNER_POLL_NS + 2, false), "clock and neutral recovery");
    check(mem.storage.vtable->read_text(&mem.storage, "app/state.json", text, sizeof(text)) &&
          !strcmp(text, "business-state-remains-unchanged"), "lease never rewrites business state");

    fixture();
    check(ptc_input_owner_acquire(&writer, START), "lease before strict parsing checks");
    change(SLOT0, "expires_mono_ns", cJSON_CreateString("18446744073709551616"));
    check(ptc_input_owner_block(&reader, START + 1, true), "overflow deadline is unknown, not expired");
    change(SLOT0, "expires_mono_ns", cJSON_CreateString("032000000000"));
    check(ptc_input_owner_block(&reader, START + 2, true), "noncanonical time is rejected");
    put(SLOT0, "{\"schema_version\":1,\"schema_version\":1}");
    check(ptc_input_owner_block(&reader, START + 3, true), "duplicate keys are rejected");
    fixture();
    check(!ptc_input_owner_acquire(&writer, UINT64_MAX), "overflowing lease cannot enable Overlay");
    fixture();
    check(mem.storage.vtable->remove_path(&mem.storage, "app/handover/runtime-ready.json"), "remove initial runtime");
    check(!ptc_input_owner_block(&reader, START, true), "known absent initial backend preserves recovery UI");
    check(!ptc_input_owner_acquire(&writer, START), "Overlay cannot own input without a bound backend");
    fixture();
    mem.fail_reads = true;
    check(ptc_input_owner_block(&reader, START, true), "unknown initial storage is fail closed, unlike known absence");
    check(!ptc_input_owner_acquire(&writer, START), "unknown initial storage cannot activate Overlay");
    fixture();
    check(ptc_input_owner_acquire(&writer, START), "lease before future record");
    check(ptc_input_owner_block(&reader, START - 1, true), "future lease timestamp is unknown, not inactive");
    change(SLOT0, "backend_boot_id", cJSON_CreateString("fedcba9876543210"));
    check(ptc_input_owner_block(&reader, START + 1, true), "wrong-boot lease in current path is rejected");
    check(!ptc_input_owner_maintain(&writer, START + PTC_INPUT_OWNER_RENEW_NS), "wrong-boot slot stops renewal");
    fixture();
    check(ptc_input_owner_acquire(&writer, START), "lease before an immediate backend change");
    runtime(RELEASE, "fedcba9876543210");
    check(!ptc_input_owner_accept(&writer, START + 1) && !writer.active,
          "physical input checks the backend before the next heartbeat");
    check(!ptc_input_owner_begin_ui(&writer), "changed backend cannot start a stale handshake");
    fixture();
    check(ptc_input_owner_acquire(&writer, START), "lease before unexpected release record");
    change(SLOT0, "visible", cJSON_CreateFalse());
    check(!ptc_input_owner_maintain(&writer, START + PTC_INPUT_OWNER_RENEW_NS),
          "producer cannot silently renew an unexpected released record");
    if (failures) return 1;
    puts("PASS: bounded Overlay ownership, atomic-gap protection, failure closure and NRO neutral drain");
    return 0;
}
