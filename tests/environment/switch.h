#ifndef TEST_ENVIRONMENT_SWITCH_H
#define TEST_ENVIRONMENT_SWITCH_H
#include <stdint.h>
#include <stdbool.h>
typedef uint32_t Result;
typedef enum {
    SetSysProductModel_Invalid, SetSysProductModel_Nx, SetSysProductModel_Copper,
    SetSysProductModel_Iowa, SetSysProductModel_Hoag, SetSysProductModel_Calcio,
    SetSysProductModel_Aula
} SetSysProductModel;
typedef struct { uint8_t major, minor, micro; char version_hash[65]; } SetSysFirmwareVersion;
typedef struct { char digest[65]; } SetSysFirmwareVersionDigest;
typedef int SplConfigItem;
#define R_SUCCEEDED(rc) ((rc) == 0)
#define Module_Libnx 345
#define LibnxError_NotInitialized 1
#define MAKERESULT(module, description) ((Result)((module) | ((description) << 9)))
#define MAKEHOSVERSION(a,b,c) (((a) << 16) | ((b) << 8) | (c))
Result setsysInitialize(void);
void setsysExit(void);
Result setsysGetFirmwareVersion(SetSysFirmwareVersion *out);
Result setsysGetFirmwareVersionDigest(SetSysFirmwareVersionDigest *out);
Result setsysGetProductModel(SetSysProductModel *out);
uint32_t hosversionGet(void);
void hosversionSet(uint32_t value);
Result splInitialize(void);
Result splGetConfig(SplConfigItem item, uint64_t *out);
void splExit(void);
#endif
