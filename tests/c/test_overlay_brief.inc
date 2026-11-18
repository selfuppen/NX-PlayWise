#include "../../companion/overlay/brief_notice.h"
#include "../../companion/notification_test.h"

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
    check_true(ptc_overlay_brief_poll(&receiver, storage, "app", "release1", "0123456789abcdef", 1100, true, false),
        "first accepted notice requests a frame");
    check_true(receiver.active && receiver.expires_ms == 11100 && receiver.text[0] &&
        storage->vtable->exists(storage, publisher.path), "brief caches exact published text and leaves cleanup ownership");
    notify_tick(&publisher, &observation, 3000, 102, true);
    check_true(!ptc_overlay_brief_poll(&receiver, storage, "app", "release1", "0123456789abcdef", 5000, true, false),
        "cached notice requires no redraw after publisher cleanup");
    check_true(receiver.active && !storage->vtable->exists(storage, publisher.path), "publisher cleanup does not erase cached pixels early");
    check_true(ptc_overlay_brief_poll(&receiver, NULL, "app", "release1", "0123456789abcdef", 11100, true, false) &&
        !ptc_overlay_brief_poll(&receiver, NULL, "app", "release1", "0123456789abcdef", 11150, true, false),
        "expiry clears the frame exactly once without a storage read");
    check_true(!receiver.active && !receiver.text[0], "brief lifetime clears cached text at ten seconds");

    observation = reason_sample(2, PTC_REASON_DAILY, 6, 112);
    notify_tick(&publisher, &observation, 12000, 112, true);
    PtcOverlayBriefNotice other = {0};
    ptc_overlay_brief_poll(&other, storage, "app", "wrong-release", "0123456789abcdef", 12100, true, false);
    check_true(!other.active, "wrong release cannot display a notice");
    ptc_overlay_brief_poll(&other, storage, "app", "release1", "fedcba9876543210", 12100, true, false);
    check_true(!other.active, "wrong backend boot cannot display a notice");
    storage->vtable->write_text_atomic(storage, publisher.path, "{}");
    ptc_overlay_brief_poll(&other, storage, "app", "release1", "0123456789abcdef", 12100, true, false);
    check_true(!other.active && !other.seen[0][0], "changed live bytes reject consumption");
    storage->vtable->write_text_atomic(storage, publisher.path, publisher.payload);
    ptc_overlay_brief_poll(&other, storage, "app", "release1", "0123456789abcdef", 14000, true, false);
    check_true(!other.active, "expired file cannot be replayed after publisher cleanup failure");
    ptc_overlay_brief_poll(&other, storage, "app", "release1", "0123456789abcdef", 12100, false, false);
    ptc_overlay_brief_poll(&other, storage, "app", "release1", "0123456789abcdef", 12200, true, false);
    check_true(!other.active && other.seen[0][0], "suppressed live notice never accumulates for replay");
    ptc_overlay_brief_poll(&receiver, storage, "app", "release1", "0123456789abcdef", 12200, true, false);
    check_true(receiver.active, "next unique instance can display");
    check_true(ptc_overlay_brief_poll(&receiver, storage, "app", "release1", "0123456789abcdef", 12300, true, true),
        "dismissal requests a clear frame");
    ptc_overlay_brief_poll(&receiver, storage, "app", "release1", "0123456789abcdef", 12400, true, false);
    check_true(!receiver.active, "short minus dismisses cached notice without replay");
    receiver.active = true;
    receiver.expires_ms = 20000;
    ptc_overlay_brief_poll(&receiver, storage, "app", "release1", "0123456789abcdef", 12000, true, false);
    check_true(!receiver.active, "monotonic regression clears the active slot");
    PtcNotificationTest test = {0};
    check_true(ptc_notification_test_send(&test, storage, "app", 15000, 123,
        "Test", "Rules unchanged"), "real test publisher creates timestamped notice");
    other.active = true;
    other.expires_ms = 15100;
    check_true(ptc_overlay_brief_poll(&other, storage, "app", "release1", "0123456789abcdef", 15100, true, false),
        "replacing an expired notice in one poll requests a new frame");
    check_true(other.active && !strcmp(other.title, "Test"), "explicit test notice uses the read-only receiver");
    ptc_overlay_brief_poll(&other, NULL, "app", "release1", "0123456789abcdef", 25100, true, false);
    check_true(!other.active, "expiry does not require additional storage reads");
    check_true(storage->vtable->exists(storage, publisher.path) &&
        storage->vtable->exists(storage, publisher.journal), "all receiver paths preserve publisher files");
    free(mem);
}
