#ifndef PLAYWISE_SUPPORT_EXPORT_H
#define PLAYWISE_SUPPORT_EXPORT_H

#include <stdbool.h>
#include <stddef.h>

size_t ptc_support_export_file_count(void);
const char *ptc_support_export_file(size_t index);
bool ptc_support_export_text_safe(const char *text);

#endif
