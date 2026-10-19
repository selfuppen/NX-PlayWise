#ifndef PTC_COMPANION_INPUT_OWNER_H
#define PTC_COMPANION_INPUT_OWNER_H

#include "../platform/storage.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PTC_INPUT_OWNER_LEASE_NS UINT64_C(30000000000)
#define PTC_INPUT_OWNER_RENEW_NS UINT64_C(10000000000)
#define PTC_INPUT_OWNER_POLL_NS UINT64_C(250000000)

typedef struct {
    PtcStorage *storage;
    char root[192];
    char release[128];
    char runtime_path[256];
    char boot[17];
    char paths[2][256];
    bool bound;
} PtcInputOwnerContext;

typedef struct {
    PtcInputOwnerContext context;
    char owner[33];
    char ui_boot[17];
    uint64_t instance_ns;
    uint64_t epoch_ns;
    uint64_t last_ns;
    uint64_t expires_ns;
    uint64_t renew_ns;
    unsigned next_slot;
    bool active;
    bool released;
    bool failed;
} PtcInputOwnerWriter;

typedef struct {
    PtcInputOwnerContext context;
    char acknowledged_owner[33];
    uint64_t last_ns;
    uint64_t next_poll_ns;
    bool blocked;
    bool needs_neutral;
} PtcInputOwnerReader;

void ptc_input_owner_writer_init(PtcInputOwnerWriter *writer, PtcStorage *storage,
    const char *root, const char *release, uint64_t instance_ns);
bool ptc_input_owner_acquire(PtcInputOwnerWriter *writer, uint64_t now_ns);
bool ptc_input_owner_maintain(PtcInputOwnerWriter *writer, uint64_t now_ns);
/* Fresh identity check for a physical input or its release, before dispatch. */
bool ptc_input_owner_accept(PtcInputOwnerWriter *writer, uint64_t now_ns);
bool ptc_input_owner_begin_ui(PtcInputOwnerWriter *writer);
void ptc_input_owner_release(PtcInputOwnerWriter *writer, uint64_t now_ns);
void ptc_input_owner_reader_init(PtcInputOwnerReader *reader, PtcStorage *storage,
    const char *root, const char *release);
/* Call before interpreting input, including internal PIN and release actions. */
bool ptc_input_owner_block(PtcInputOwnerReader *reader, uint64_t now_ns, bool non_neutral);

#ifdef __cplusplus
}
#endif
#endif
