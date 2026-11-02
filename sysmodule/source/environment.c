#include "environment.h"
#include <switch.h>
#include <stdio.h>
#include <string.h>
#include "../../common/protocol/atmosphere_version.h"

static const char *product_model_name(SetSysProductModel model)
{
    switch (model) {
    case SetSysProductModel_Nx: return "erista";
    case SetSysProductModel_Copper: return "erista-simulation";
    case SetSysProductModel_Iowa: return "mariko";
    case SetSysProductModel_Hoag: return "mariko-lite";
    case SetSysProductModel_Calcio: return "mariko-simulation";
    case SetSysProductModel_Aula: return "mariko-oled";
    default: return "unknown";
    }
}

bool ptc_switch_write_environment(PtcStorage *storage, const char *path)
{
    SetSysFirmwareVersion firmware;
    SetSysFirmwareVersionDigest digest;
    SetSysProductModel model = SetSysProductModel_Invalid;
    PtcAtmosphereVersion atmosphere_version;
    uint64_t atmosphere_raw = 0;
    Result atmosphere_result = MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    Result setsys_result;
    Result firmware_result = MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    Result digest_result = firmware_result;
    Result model_result = firmware_result;
    char json[1200];
    char atmosphere_version_json[32];
    bool read_ok = false;
    bool firmware_read_ok = false;
    bool atmosphere = false;
    memset(&firmware, 0, sizeof(firmware));
    memset(&digest, 0, sizeof(digest));
    setsys_result = setsysInitialize();
    if (R_SUCCEEDED(setsys_result)) {
        firmware_result = setsysGetFirmwareVersion(&firmware);
        firmware_read_ok = R_SUCCEEDED(firmware_result);
        /* Independent optional queries: a missing digest must not prevent the
           model query or hide the HOS/AMS versions that were actually read. */
        if (firmware_read_ok && hosversionGet() == 0)
            hosversionSet(MAKEHOSVERSION(firmware.major, firmware.minor, firmware.micro));
        digest_result = setsysGetFirmwareVersionDigest(&digest);
        model_result = setsysGetProductModel(&model);
        read_ok = firmware_read_ok && R_SUCCEEDED(model_result) &&
            strcmp(product_model_name(model), "unknown") != 0;
        setsysExit();
    }
    memset(&atmosphere_version, 0, sizeof(atmosphere_version));
    if (R_SUCCEEDED(splInitialize())) {
        atmosphere_result = splGetConfig((SplConfigItem)65000, &atmosphere_raw);
        splExit();
        atmosphere = R_SUCCEEDED(atmosphere_result) &&
            ptc_atmosphere_version_decode(atmosphere_raw, &atmosphere_version);
    }
    if (atmosphere) {
        snprintf(atmosphere_version_json, sizeof(atmosphere_version_json), "\"%u.%u.%u\"",
            (unsigned int)atmosphere_version.major,
            (unsigned int)atmosphere_version.minor,
            (unsigned int)atmosphere_version.micro);
    } else {
        snprintf(atmosphere_version_json, sizeof(atmosphere_version_json), "null");
    }
    snprintf(
        json,
        sizeof(json),
        "{\"version\":1,\"read_ok\":%s,\"hos\":\"%u.%u.%u\",\"firmware_hash\":\"%.64s\","
        "\"firmware_digest\":\"%.64s\",\"firmware_digest_available\":%s,\"model\":\"%s\",\"atmosphere\":%s,"
        "\"environment_detection\":{\"setsys_result\":%u,\"firmware_result\":%u,"
        "\"digest_result\":%u,\"model_result\":%u},"
        "\"atmosphere_version\":%s,\"atmosphere_detection_source\":\"spl:ExosphereApiVersion\","
        "\"atmosphere_detection\":{\"source\":\"spl:ExosphereApiVersion\","
        "\"result\":%u,\"raw\":\"0x%016llx\",\"version\":%s}}\n",
        read_ok ? "true" : "false",
        (unsigned int)firmware.major,
        (unsigned int)firmware.minor,
        (unsigned int)firmware.micro,
        firmware.version_hash,
        digest.digest,
        R_SUCCEEDED(digest_result) ? "true" : "false",
        product_model_name(model),
        atmosphere ? "true" : "false",
        (unsigned int)setsys_result,
        (unsigned int)firmware_result,
        (unsigned int)digest_result,
        (unsigned int)model_result,
        atmosphere_version_json,
        (unsigned int)atmosphere_result,
        (unsigned long long)atmosphere_raw,
        atmosphere_version_json);
    return storage->vtable->write_text_atomic(storage, path, json);
}
