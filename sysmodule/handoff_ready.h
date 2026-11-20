#ifndef PTC_SYSMODULE_HANDOFF_READY_H
#define PTC_SYSMODULE_HANDOFF_READY_H

#include <string.h>
#include "../platform/storage.h"

/* This acknowledgement identifies a frozen transaction, not a renewable lease.
   Read each time so deletion, replacement and failed reads still trigger repair. */
static inline bool ptc_handoff_ready_write(PtcStorage *storage, const char *path, const char *text,
    bool *retry_write)
{
    char observed[768];
    size_t length = strlen(text);
    bool same = length < sizeof(observed) &&
        storage->vtable->read_text(storage, path, observed, length + 1) &&
        !strcmp(observed, text);
    if (same && !*retry_write) return true;
    /* A failed commit can leave readable bytes: retry the durable writer even
       if the next read matches. Clear this only after confirmed write success. */
    bool written = storage->vtable->write_text_atomic(storage, path, text);
    *retry_write = !written;
    return written;
}

#endif
