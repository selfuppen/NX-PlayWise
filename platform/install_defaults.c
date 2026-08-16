#include "install_defaults.h"

#include <stdio.h>

#define PTC_INSTALL_DEFAULT_TEXT_SIZE 8192
#define PTC_INSTALL_PATH_SIZE 320

static const char *const PTC_INSTALL_DEFAULT_FILES[] = {
    "config.json",
    "auth.json",
    "rules.json",
    "state.json",
    "compatibility.json",
    "setup.json",
};

bool ptc_install_materialize_defaults(PtcStorage *storage, const char *app_root)
{
    char source_path[PTC_INSTALL_PATH_SIZE];
    char destination_path[PTC_INSTALL_PATH_SIZE];
    char text[PTC_INSTALL_DEFAULT_TEXT_SIZE];
    size_t index;
    int source_length;
    int destination_length;

    if (!storage || !storage->vtable || !storage->vtable->exists ||
        !storage->vtable->read_text || !storage->vtable->write_text_atomic ||
        !app_root || app_root[0] == '\0') {
        return false;
    }

    for (index = 0; index < sizeof(PTC_INSTALL_DEFAULT_FILES) / sizeof(PTC_INSTALL_DEFAULT_FILES[0]); ++index) {
        destination_length = snprintf(destination_path, sizeof(destination_path), "%s/%s",
            app_root, PTC_INSTALL_DEFAULT_FILES[index]);
        if (destination_length < 0 || (size_t)destination_length >= sizeof(destination_path)) {
            return false;
        }
        /* An existing live file is user/runtime data, even when malformed. */
        if (storage->vtable->exists(storage, destination_path)) {
            continue;
        }

        source_length = snprintf(source_path, sizeof(source_path), "%s/%s/%s",
            app_root, PTC_INSTALL_DEFAULTS_DIRECTORY, PTC_INSTALL_DEFAULT_FILES[index]);
        if (source_length < 0 || (size_t)source_length >= sizeof(source_path) ||
            !storage->vtable->read_text(storage, source_path, text, sizeof(text)) ||
            !storage->vtable->write_text_atomic(storage, destination_path, text)) {
            return false;
        }
    }
    return true;
}
