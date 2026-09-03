#include "handoff_guard.h"

bool ptc_lab_handoff_can_commit(const PtcLabHandoffGuard *guard)
{
    return guard && guard->source_confirmed && guard->target_absent && guard->quiesce_ready;
}

bool ptc_lab_handoff_can_launch(const PtcLabHandoffGuard *guard)
{
    return ptc_lab_handoff_can_commit(guard) && guard->journal_committed && guard->source_absent;
}
