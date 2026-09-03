#ifndef PLAYWISE_DEVICE_LAB_HANDOFF_GUARD_H
#define PLAYWISE_DEVICE_LAB_HANDOFF_GUARD_H

#include <stdbool.h>

typedef struct {
    bool source_confirmed;
    bool target_absent;
    bool quiesce_ready;
    bool journal_committed;
    bool source_absent;
} PtcLabHandoffGuard;

bool ptc_lab_handoff_can_commit(const PtcLabHandoffGuard *guard);
bool ptc_lab_handoff_can_launch(const PtcLabHandoffGuard *guard);

#endif
