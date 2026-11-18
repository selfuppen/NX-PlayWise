#include "../../companion/overlay/brief_notice.h"
#include "../../companion/notification_test.h"
#include "../../companion/overlay/reason_wake.h"

typedef struct {
    PtcStorage *storage;
    unsigned reason_reads;
    unsigned test_reads;
    unsigned payload_reads;
} PtcBriefReadCounts;

static bool count_brief_reads(PtcStorage *storage, const char *path, char *out, size_t size)
{
    PtcBriefReadCounts *counts = storage->ctx;
    if (!strcmp(path, "app/support/reason-notification-owned.json")) ++counts->reason_reads;
    if (!strcmp(path, "app/support/notification-test-owned.json")) ++counts->test_reads;
    if (strstr(path, ".notify")) ++counts->payload_reads;
    return counts->storage->vtable->read_text(counts->storage, path, out, size);
}

static void test_overlay_brief(void)
{
    PtcMemStorage *mem = malloc(sizeof(*mem));
    check_true(mem != NULL, "brief fixture allocation");
    if (!mem) return;
    ptc_mem_storage_init(mem);
    PtcStorage *storage = &mem->storage;
    PtcReasonNotification publisher;
    PtcOverlayBriefNotice receiver = {0};
    PtcReasonObservation observation = reason_sample(1, PTC_REASON_EYE, 5, 100);
    storage->vtable->write_text_atomic(storage, "sdmc:/config/ultrahand/config.ini", "[ultrahand]\nnotifications=true\n");
    ptc_reason_notification_init(&publisher, storage, "app", "release1", "0123456789abcdef");
    notify_tick(&publisher, &observation, 1000, 100, true);
    check_true(publisher.pending, "real reason publisher creates brief fixture");
    check_true(ptc_overlay_brief_poll(&receiver, storage, "app", "release1", "0123456789abcdef", 1100, true, false, true),
        "first accepted notice requests a frame");
    check_true(receiver.active && receiver.expires_ms == 11100 && receiver.text[0] &&
        storage->vtable->exists(storage, publisher.path), "brief caches exact published text and leaves cleanup ownership");
    notify_tick(&publisher, &observation, 3000, 102, true);
    check_true(!ptc_overlay_brief_poll(&receiver, storage, "app", "release1", "0123456789abcdef", 5000, true, false, true),
        "cached notice requires no redraw after publisher cleanup");
    check_true(receiver.active && !storage->vtable->exists(storage, publisher.path), "publisher cleanup does not erase cached pixels early");
    check_true(ptc_overlay_brief_poll(&receiver, NULL, "app", "release1", "0123456789abcdef", 11100, true, false, true) &&
        !ptc_overlay_brief_poll(&receiver, NULL, "app", "release1", "0123456789abcdef", 11150, true, false, true),
        "expiry clears the frame exactly once without a storage read");
    check_true(!receiver.active && !receiver.text[0], "brief lifetime clears cached text at ten seconds");

    observation = reason_sample(2, PTC_REASON_DAILY, 6, 112);
    notify_tick(&publisher, &observation, 12000, 112, true);
    PtcOverlayBriefNotice other = {0};
    ptc_overlay_brief_poll(&other, storage, "app", "wrong-release", "0123456789abcdef", 12100, true, false, true);
    check_true(!other.active, "wrong release cannot display a notice");
    ptc_overlay_brief_poll(&other, storage, "app", "release1", "fedcba9876543210", 12100, true, false, true);
    check_true(!other.active, "wrong backend boot cannot display a notice");
    storage->vtable->write_text_atomic(storage, publisher.path, "{}");
    ptc_overlay_brief_poll(&other, storage, "app", "release1", "0123456789abcdef", 12100, true, false, true);
    check_true(!other.active && !other.seen[0][0], "changed live bytes reject consumption");
    storage->vtable->write_text_atomic(storage, publisher.path, publisher.payload);
    ptc_overlay_brief_poll(&other, storage, "app", "release1", "0123456789abcdef", 14000, true, false, true);
    check_true(!other.active, "expired file cannot be replayed after publisher cleanup failure");
    ptc_overlay_brief_poll(&receiver, storage, "app", "release1", "0123456789abcdef", 12200, true, false, true);
    check_true(receiver.active, "next unique instance can display");
    check_true(ptc_overlay_brief_poll(&receiver, storage, "app", "release1", "0123456789abcdef", 12300, true, true, true),
        "dismissal requests a clear frame");
    ptc_overlay_brief_poll(&receiver, storage, "app", "release1", "0123456789abcdef", 12400, true, false, true);
    check_true(!receiver.active, "short minus dismisses cached notice without replay");
    receiver.active = true;
    receiver.expires_ms = 20000;
    ptc_overlay_brief_poll(&receiver, storage, "app", "release1", "0123456789abcdef", 12000, true, false, true);
    check_true(!receiver.active, "monotonic regression clears the active slot");
    PtcNotificationTest test = {0};
    check_true(ptc_notification_test_send(&test, storage, "app", 15000, 123,
        "Test", "Rules unchanged"), "real test publisher creates timestamped notice");
    other = (PtcOverlayBriefNotice){0};
    other.active = true;
    other.expires_ms = 15100;
    check_true(ptc_overlay_brief_poll(&other, storage, "app", "release1", "0123456789abcdef", 15100, true, false, true),
        "replacing an expired notice in one poll requests a new frame");
    check_true(other.active && !strcmp(other.title, "Test"), "explicit test notice uses the read-only receiver");
    ptc_overlay_brief_poll(&other, NULL, "app", "release1", "0123456789abcdef", 25100, true, false, true);
    check_true(!other.active, "expiry does not require additional storage reads");

    const PtcStorageVTable read_counter = {.read_text = count_brief_reads};
    PtcBriefReadCounts counts = {.storage = storage};
    PtcStorage counted = {.vtable = &read_counter, .ctx = &counts};
    PtcOverlayBriefNotice idle = {0};
    ptc_overlay_brief_poll(&idle, &counted, "app", "release1", "0123456789abcdef", 14900, true, false, true);
    ptc_overlay_brief_poll(&idle, &counted, "app", "release1", "0123456789abcdef", 15150, true, false, true);
    check_true(counts.reason_reads == 2 && counts.test_reads == 1 && !idle.active,
        "idle checks the real journal every poll and skips the intervening test journal read");
    ptc_overlay_brief_poll(&idle, &counted, "app", "release1", "0123456789abcdef", 15400, true, false, true);
    check_true(counts.reason_reads == 3 && counts.test_reads == 2 && idle.active && !strcmp(idle.title, "Test"),
        "the next test poll consumes an unexpired test notice");

    PtcOverlayBriefNotice no_runtime = {0};
    counts.reason_reads = counts.test_reads = 0;
    ptc_overlay_brief_poll(&no_runtime, &counted, "app", "release1", "", 14900, true, false, true);
    check_true(counts.reason_reads == 0 && counts.test_reads == 1 && !no_runtime.active,
        "without a backend boot skip the reason journal");
    PtcOverlayBriefNotice reason_ready = {0};
    ptc_overlay_brief_poll(&reason_ready, &counted, "app", "release1", "0123456789abcdef", 11900, true, false, true);
    check_true(ptc_overlay_brief_poll(&reason_ready, &counted, "app", "release1", "0123456789abcdef", 12150, true, false, true) &&
        reason_ready.active && reason_ready.seen[0][0], "a real notice is accepted before the next idle test check");
    check_true(storage->vtable->exists(storage, publisher.path) &&
        storage->vtable->exists(storage, publisher.journal), "all receiver paths preserve publisher files");
    free(mem);
}

static void test_overlay_brief_suppression(void)
{
    static const char boot[] = "0123456789abcdef", next_boot[] = "fedcba9876543210";
    PtcMemStorage *mem = malloc(sizeof(*mem));
    check_true(mem != NULL, "brief suppression fixture allocation");
    if (!mem) return;
    ptc_mem_storage_init(mem);
    PtcStorage *storage = &mem->storage;
    const PtcStorageVTable read_counter = {.read_text = count_brief_reads};
    PtcBriefReadCounts counts = {.storage = storage};
    PtcStorage counted = {.vtable = &read_counter, .ctx = &counts};
    PtcOverlayBriefNotice receiver = {.active = true, .expires_ms = 10000, .title = "Previous"};
    PtcReasonNotification reason;
    PtcNotificationTest test = {0};
    PtcReasonObservation observation = reason_sample(1, PTC_REASON_EYE, 5, 100);
    char reason_stage[280], test_stage[280];

    check_true(ptc_overlay_brief_poll(&receiver, &counted, "app", "release1", boot, 1000, false, false, true) &&
        !receiver.active && !receiver.title[0], "entering suppression clears the frame without a journal read");
    storage->vtable->write_text_atomic(storage, "sdmc:/config/ultrahand/config.ini", "[ultrahand]\nnotifications=true\n");
    ptc_reason_notification_init(&reason, storage, "app", "release1", boot);
    notify_tick(&reason, &observation, 1499, 100, true);
    check_true(reason.pending && ptc_notification_test_send(&test, storage, "app", 1500, 1, "Old test", "Rules unchanged"),
        "publish both notices at or before the resume boundary");
    snprintf(reason_stage, sizeof(reason_stage), "%s.stage", reason.path);
    snprintf(test_stage, sizeof(test_stage), "%s.stage", test.path);
    check_true(storage->vtable->rename_path(storage, reason.path, reason_stage) &&
        storage->vtable->rename_path(storage, test.path, test_stage), "keep payloads staged until after resume");
    for (uint64_t ms = 1100; ms < 1500; ms += 100)
        ptc_overlay_brief_poll(&receiver, &counted, "app", "release1", boot, ms, false, false, true);
    ptc_overlay_brief_poll(&receiver, &counted, "app", "release1", boot, 1500, true, false, true);
    check_true(counts.reason_reads == 0 && counts.test_reads == 0 && counts.payload_reads == 0,
        "stable suppression and the resume boundary never probe either journal");

    check_true(storage->vtable->rename_path(storage, reason_stage, reason.path) &&
        storage->vtable->rename_path(storage, test_stage, test.path), "finish the delayed renames");
    ptc_overlay_brief_poll(&receiver, &counted, "app", "release1", boot, 1750, true, false, true);
    check_true(!receiver.active && !receiver.seen[0][0] && !receiver.seen[1][0] &&
        counts.reason_reads == 1 && counts.test_reads == 1 && counts.payload_reads == 0,
        "discard still-live prior and same-millisecond notices without opening delayed payloads");
    ptc_overlay_brief_poll(&receiver, &counted, "app", "release1", next_boot, 2250, true, false, true);
    check_true(!receiver.active && counts.reason_reads == 2 && counts.test_reads == 2 && counts.payload_reads == 0,
        "a new backend identity does not reopen the old suppression window");

    PtcOverlayBriefNotice rollback = {0};
    ptc_overlay_brief_poll(&rollback, NULL, "app", "release1", boot, 1800, true, false, true);
    ptc_overlay_brief_poll(&rollback, &counted, "app", "release1", boot, 1700, true, false, true);
    ptc_overlay_brief_poll(&rollback, &counted, "app", "release1", boot, 1720, true, false, true);
    ptc_overlay_brief_poll(&rollback, &counted, "app", "release1", boot, 1800, true, false, true);
    check_true(!rollback.active && counts.reason_reads == 2 && counts.test_reads == 2,
        "clock rollback resumes only after reaching the previous high water mark");
    ptc_overlay_brief_poll(&rollback, &counted, "app", "release1", boot, 1801, true, false, true);
    check_true(!rollback.active && counts.reason_reads == 3 && counts.test_reads == 3 && counts.payload_reads == 0,
        "clock recovery does not replay notices signed before the new boundary");

    notify_tick(&reason, &observation, 3500, 102, true);
    ptc_notification_test_poll(&test, storage, "app", 3500);
    check_true(ptc_notification_test_send(&test, storage, "app", 13000, 2, "New test", "Rules unchanged"),
        "publish a test after resuming");
    PtcOverlayBriefNotice reason_receiver = receiver;
    PtcOverlayBriefNotice subscribed_receiver = receiver;
    check_true(ptc_overlay_brief_poll(&receiver, &counted, "app", "release1", next_boot, 13250, true, false, true) &&
        receiver.active && !strcmp(receiver.title, "New test"), "a new test can display after resuming");
    ptc_reason_notification_init(&reason, storage, "app", "release1", next_boot);
    observation = reason_sample(2, PTC_REASON_DAILY, 6, 110);
    notify_tick(&reason, &observation, 13000, 110, true);
    check_true(reason.pending && ptc_overlay_brief_poll(&reason_receiver, &counted, "app", "release1", next_boot, 13250, true, false, true) &&
        reason_receiver.active && reason_receiver.seen[0][0] && reason_receiver.seen[1][0] &&
        strcmp(reason_receiver.title, "New test"), "a new reason still wins over a simultaneous test");
    check_true(ptc_overlay_brief_poll(&subscribed_receiver, &counted, "app", "release1", next_boot, 13250, true, false, false) &&
        subscribed_receiver.active && subscribed_receiver.seen[0][0] && subscribed_receiver.seen[1][0] &&
        !strcmp(subscribed_receiver.title, reason_receiver.title),
        "a newly accepted test checks for a conflicting reason even before its wake arrives");
    free(mem);
}

typedef struct {
    PtcReasonNotification *publisher;
    unsigned pulses;
    bool published_at_wake;
} PtcWakeFixture;

static void capture_reason_wake(void *ctx)
{
    PtcWakeFixture *fixture = ctx;
    PtcReasonNotification *n = fixture->publisher;
    const char *path = n->detail_pending ? "app/support/reason-detail-request.json" : n->path;
    fixture->published_at_wake = n->storage->vtable->exists(n->storage, path);
    ++fixture->pulses;
}

static void test_overlay_reason_wake(void)
{
    PtcMemStorage *mem = malloc(sizeof(*mem));
    check_true(mem != NULL, "wake fixture allocation");
    if (!mem) return;
    ptc_mem_storage_init(mem);
    PtcStorage *storage = &mem->storage;
    const PtcStorageVTable read_counter = {.read_text = count_brief_reads};
    PtcBriefReadCounts counts = {.storage = storage};
    PtcStorage counted = {.vtable = &read_counter, .ctx = &counts};
    PtcOverlayReasonWake wake = {0};
    PtcOverlayBriefNotice receiver = {0};
    uint64_t reason_audit = 0, request_audit = 0;
    unsigned request_reads = 0;
    for (uint64_t ms = 1000; ms < 2000; ms += 250) {
        bool reason = ptc_overlay_reason_scan(&wake, true, ms, &reason_audit);
        request_reads += ptc_overlay_reason_scan(&wake, true, ms, &request_audit);
        ptc_overlay_brief_poll(&receiver, &counted, "app", "release1", "0123456789abcdef", ms, true, false, reason);
    }
    check_true(counts.reason_reads == 1 && request_reads == 1 && counts.test_reads == 2,
        "healthy idle audits both backend files once per second and preserves the test journal cadence");
    check_true(ptc_overlay_reason_scan(&wake, false, 1800, &reason_audit) &&
        ptc_overlay_reason_scan(&wake, false, 1850, &reason_audit), "subscription loss resumes file polling immediately");
    ptc_overlay_reason_wake(&wake, 1850);
    check_true(ptc_overlay_reason_scan(&wake, true, 1850, &request_audit) &&
        ptc_overlay_reason_scan(&wake, true, 2100, &request_audit),
        "a wake bypasses the audit and retries while an in-flight publication may still be valid");
    ptc_overlay_reason_wake(&wake, 2100);
    check_true(ptc_overlay_reason_scan(&wake, true, 4099, &request_audit) &&
        !ptc_overlay_reason_scan(&wake, true, 4100, &request_audit) &&
        ptc_overlay_reason_scan(&wake, true, 5099, &request_audit),
        "coalesced wake extends the bounded scan, then the lost-wake audit resumes");
    check_true(ptc_overlay_reason_scan(&wake, true, 2000, &request_audit),
        "clock rollback does not suppress the audit indefinitely");

    PtcReasonNotification publisher;
    PtcWakeFixture fixture = {.publisher = &publisher};
    static const char boot[] = "0123456789abcdef";
    PtcReasonObservation observation = reason_sample(1, PTC_REASON_EYE, 5, 100);
    ptc_reason_notification_init(&publisher, storage, "app", "release1", boot);
    publisher.wake = capture_reason_wake;
    publisher.wake_ctx = &fixture;
    storage->vtable->write_text_atomic(storage, "sdmc:/config/ultrahand/config.ini", "[ultrahand]\nnotifications=true\n");
    PtcReasonDetail ready = {.release = "release1", .boot = "0123456789abcdef", .ui_instance = 1,
        .issued_ms = 1900, .expires_ms = 3400};
    check_true(ptc_reason_detail_write(storage, "app", "ready", &ready), "ready for the real detail producer");
    notify_tick(&publisher, &observation, 2000, 100, true);
    check_true(publisher.detail_pending && fixture.pulses == 1 && !fixture.published_at_wake,
        "detail producer signals before publishing the request");
    notify_tick(&publisher, &observation, 4000, 102, true);
    check_true(fixture.pulses == 1 && !publisher.detail_pending, "expiry and cleanup do not signal");
    check_true(storage->vtable->remove_path(storage, "app/support/reason-detail-ready.json"),
        "remove expired ready to exercise immediate brief fallback");
    observation = reason_sample(2, PTC_REASON_DAILY, 6, 110);
    mem->fail_renames = true;
    notify_tick(&publisher, &observation, 12000, 110, true);
    check_true(fixture.pulses == 2 && !fixture.published_at_wake && !publisher.pending && publisher.failures == 1,
        "brief producer signals before rename and unknown publication still obeys existing failure handling");
    check_true(!ptc_overlay_brief_poll(&receiver, &counted, "app", "release1", boot, 12100, true, false, true) &&
        !receiver.active, "an event and journal cannot display an unrenamed stage payload");
    free(mem);
}
