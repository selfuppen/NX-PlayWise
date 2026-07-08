#ifndef PTC_PLATFORM_LOGGER_H
#define PTC_PLATFORM_LOGGER_H

typedef struct PtcLogger PtcLogger;

typedef struct {
    void (*info)(PtcLogger *logger, const char *message);
    void (*warn)(PtcLogger *logger, const char *message);
    void (*error)(PtcLogger *logger, const char *message);
} PtcLoggerVTable;

struct PtcLogger {
    const PtcLoggerVTable *vtable;
    void *ctx;
};

#endif
