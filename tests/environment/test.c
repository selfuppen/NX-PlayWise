#include <switch.h>
#include <stdio.h>
#include <string.h>
#include "../../sysmodule/source/environment.h"
#include "../../platform/host/mem_storage.h"
#include "../../third_party/cjson/cJSON.h"

static int failures, model_calls, setsys_closes, spl_closes;
static Result init_result, firmware_result, digest_result, model_result, spl_result;
static uint32_t hos;
#define CHECK(ok, message) do { if (!(ok)) { ++failures; fprintf(stderr, "FAIL: %s\n", message); } } while (0)
Result setsysInitialize(void) { return init_result; }
void setsysExit(void) { ++setsys_closes; }
Result setsysGetFirmwareVersion(SetSysFirmwareVersion *out) {
    if (firmware_result) return firmware_result;
    out->major = 23; out->minor = 0; out->micro = 1;
    strcpy(out->version_hash, "reported-firmware-hash"); return 0;
}
Result setsysGetFirmwareVersionDigest(SetSysFirmwareVersionDigest *out) {
    CHECK(hos == MAKEHOSVERSION(23,0,1) || firmware_result, "HOS initialized before optional queries");
    if (!digest_result) strcpy(out->digest, "digest");
    return digest_result;
}
Result setsysGetProductModel(SetSysProductModel *out) {
    ++model_calls; if (!model_result) *out = SetSysProductModel_Aula; return model_result;
}
uint32_t hosversionGet(void) { return hos; }
void hosversionSet(uint32_t value) { hos = value; }
Result splInitialize(void) { return spl_result; }
Result splGetConfig(SplConfigItem item, uint64_t *out) {
    CHECK(item == 65000, "read-only Atmosphere config item"); *out = UINT64_C(0x010c011617000100); return 0;
}
void splExit(void) { ++spl_closes; }

int main(void)
{
    static PtcMemStorage mem;
    for (int sample = 0; sample < 6; ++sample) {
        char text[2048];
        init_result = sample == 3 ? 103 : 0;
        firmware_result = sample == 4 ? 104 : 0;
        digest_result = sample == 1 ? 101 : 0;
        model_result = sample == 2 ? 102 : 0;
        spl_result = sample == 5 ? 105 : 0;
        hos = 0; model_calls = setsys_closes = spl_closes = 0;
        ptc_mem_storage_init(&mem);
        CHECK(ptc_switch_write_environment(&mem.storage, "app/environment.json"), "write collected metadata");
        CHECK(mem.storage.vtable->read_text(&mem.storage, "app/environment.json", text, sizeof(text)), "read back metadata");
        cJSON *json = cJSON_Parse(text);
        CHECK(json != NULL, "metadata round trips as JSON");
        if (!json) continue;
        CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "read_ok")) == (sample != 2 && sample != 3 && sample != 4),
            "optional digest and AMS failures do not hide readable HOS/model");
        CHECK(model_calls == (sample == 3 ? 0 : 1), "digest and firmware failures never skip model query");
        CHECK(setsys_closes == (sample == 3 ? 0 : 1) && spl_closes == (sample == 5 ? 0 : 1), "release only initialized services");
        if (sample == 1) {
            cJSON *detection = cJSON_GetObjectItemCaseSensitive(json, "environment_detection");
            CHECK(!strcmp(cJSON_GetObjectItemCaseSensitive(json, "model")->valuestring, "mariko-oled"), "digest failure preserves real model");
            CHECK(cJSON_GetObjectItemCaseSensitive(detection, "digest_result")->valueint == 101, "record exact failing Result");
            CHECK(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(json, "firmware_digest_available")), "missing digest stays unavailable");
        }
        if (sample != 5) CHECK(!strcmp(cJSON_GetObjectItemCaseSensitive(json, "atmosphere_version")->valuestring, "1.12.1"), "decode reported AMS version");
        cJSON_Delete(json);
        mem.fail_writes = true;
        CHECK(!ptc_switch_write_environment(&mem.storage, "app/environment.json"), "metadata write failure reported");
    }
    if (failures) return 1;
    puts("PASS environment sampling: independent queries, partial metadata, exact Results and service cleanup");
    return 0;
}
