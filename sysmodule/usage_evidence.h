#ifndef PTC_SYSMODULE_USAGE_EVIDENCE_H
#define PTC_SYSMODULE_USAGE_EVIDENCE_H

#include "../platform/storage.h"
#include "../platform/usage_evidence.h"

typedef struct {
    PtcStorage *storage;
    PtcUsageEvidenceReader reader;
    void *reader_ctx;
    bool active;
    uint64_t started_ns, last_ns, duration_ns;
    unsigned failures, samples;
    char config_path[320], output_path[320], config[512];
    char run_id[33], boot_id[65], commit[41], release_id[128];
} PtcUsageEvidence;

bool ptc_usage_evidence_init(PtcUsageEvidence *capture, PtcStorage *storage,
    const char *root, const char *boot_id, const char *commit, const char *release_id,
    uint64_t now_ns, PtcUsageEvidenceReader reader, void *ctx);
void ptc_usage_evidence_tick(PtcUsageEvidence *capture, uint64_t now_ns);
void ptc_usage_evidence_stop(PtcUsageEvidence *capture, const char *reason);

#endif
