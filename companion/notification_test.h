#ifndef PTC_NOTIFICATION_TEST_H
#define PTC_NOTIFICATION_TEST_H
#include "../platform/storage.h"

typedef struct {
    char path[256], payload[1024];
    uint64_t expires_ms, retry_ms, last_send_ms;
    unsigned failures;
    bool initialized, pending, blocked, sent;
} PtcNotificationTest;

/* Independent ownership journal; never touches production reason messages. */
bool ptc_notification_test_cleanup(PtcNotificationTest *test, PtcStorage *s, const char *root);
void ptc_notification_test_poll(PtcNotificationTest *test, PtcStorage *s, const char *root, uint64_t ms);
bool ptc_notification_test_send(PtcNotificationTest *test, PtcStorage *s, const char *root,
    uint64_t ms, uint64_t random_id, const char *title, const char *body);
#endif
