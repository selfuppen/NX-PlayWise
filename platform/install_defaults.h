#ifndef PTC_PLATFORM_INSTALL_DEFAULTS_H
#define PTC_PLATFORM_INSTALL_DEFAULTS_H

#include <stdbool.h>

#include "storage.h"

#define PTC_INSTALL_DEFAULTS_DIRECTORY "defaults"

bool ptc_install_materialize_defaults(PtcStorage *storage, const char *app_root);

#endif
