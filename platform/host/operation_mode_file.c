#include "operation_mode_file.h"

#include <stdio.h>
#include <string.h>

PtcOperationModeStatus ptc_file_operation_mode_read(void *ctx)
{
    const PtcFileOperationMode *provider = ctx;
    PtcOperationModeStatus status = {PTC_OPERATION_MODE_UNDOCKED, true, true};
    char path[320], text[64];
    snprintf(path, sizeof(path), "%s/operation-mode.txt", provider->app_root);
    if (provider->storage->vtable->read_text(provider->storage, path, text, sizeof(text))) {
        text[strcspn(text, "\r\n")] = '\0';
        if (!strcmp(text, "docked")) status.mode = PTC_OPERATION_MODE_DOCKED;
        else if (strcmp(text, "undocked")) status.mode = PTC_OPERATION_MODE_UNKNOWN;
    }
    return status;
}
