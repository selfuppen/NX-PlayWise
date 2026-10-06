#ifndef PTC_CONFIG_BACKUP_H
#define PTC_CONFIG_BACKUP_H

#include "storage.h"
#include "../third_party/cjson/cJSON.h"
#include "../common/rules/rules.h"

#define PTC_CONFIG_BACKUP_GROUPS 11u
#define PTC_CONFIG_BACKUP_ALL 2047u
#define PTC_CONFIG_BACKUP_DEFAULT 511u
#define PTC_CONFIG_BACKUP_MAX_FILES 134u
#define PTC_CONFIG_BACKUP_FILE_SIZE 16385u
#define PTC_CONFIG_BACKUP_MANIFEST_SIZE 49152u
#define PTC_CONFIG_BACKUP_MAX_BYTES (4u * 1024u * 1024u)

enum {
    PTC_CONFIG_WEEK = 1u, PTC_CONFIG_SCHEDULED = 2u, PTC_CONFIG_TODAY = 4u,
    PTC_CONFIG_BUFFER = 8u, PTC_CONFIG_HOLIDAY = 16u, PTC_CONFIG_BEDTIME = 32u,
    PTC_CONFIG_EYE = 64u, PTC_CONFIG_DOCK = 128u, PTC_CONFIG_PREFS = 256u,
    PTC_CONFIG_PIN = 512u, PTC_CONFIG_PAIRING = 1024u
};

/* Stages are request-owned. The large archive is assembled only by the NRO. */
bool ptc_config_stage_path(char *out, size_t size, const char *root, const char *id);
cJSON *ptc_config_read_json(PtcStorage *storage, const char *root, const char *relative, size_t limit);
bool ptc_config_write_json(PtcStorage *storage, const char *root, const char *relative, const cJSON *json);
bool ptc_config_json_valid(const cJSON *json, unsigned depth);
bool ptc_config_rules_valid(const cJSON *rules);
bool ptc_config_rules_decode(const cJSON *json, PtcRules *rules);
bool ptc_config_preferences_valid(const cJSON *config);
bool ptc_config_pin_valid(const cJSON *auth);
bool ptc_config_stage_create(PtcStorage *storage, const char *root, const char *stage, int64_t now);
bool ptc_config_stage_validate(PtcStorage *storage, const char *stage, const char *digest);
bool ptc_config_archive_save(PtcStorage *storage, const char *stage, const char *path);
bool ptc_config_archive_load(PtcStorage *storage, const char *path, const char *stage, char digest[65]);
bool ptc_config_manifest_digest(PtcStorage *storage, const char *stage, char digest[65]);
cJSON *ptc_config_merge_rules(const cJSON *current, const cJSON *source, unsigned groups, uint16_t day);
cJSON *ptc_config_merge_preferences(const cJSON *current, const cJSON *source, unsigned groups);
unsigned ptc_config_rule_group(const char *key);

#endif
