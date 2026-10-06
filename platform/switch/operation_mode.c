#include "operation_mode.h"
#ifdef __SWITCH__
#include <switch.h>
#include <string.h>

#if __has_include(<switch/services/omm.h>)
#include <switch/services/omm.h>
#else
/* libnx 4.12 lacks the public OMM wrapper. Keep its read-only command-0
   contract, verified against local libnx dbcc1beafc6b47b5ffbeb8ba82463a7d45da40bb.
   No mode policy command is exposed by this compatibility adapter. */
typedef enum {
    OmmOperationMode_Handheld = 0,
    OmmOperationMode_Console = 1
} OmmOperationMode;
static Service operation_mode_service;
static Result ommInitialize(void)
{
    return smGetService(&operation_mode_service, "omm");
}
static void ommExit(void)
{
    serviceClose(&operation_mode_service);
}
static Result ommGetOperationMode(OmmOperationMode *mode)
{
    u8 value;
    SfDispatchParams params;
    memset(&params, 0, sizeof(params));
    Result result = serviceDispatchImpl(&operation_mode_service, 0, NULL, 0, &value, sizeof(value), params);
    if (R_SUCCEEDED(result)) *mode = (OmmOperationMode)value;
    return result;
}
#endif

static bool initialized;
static bool model_known;
static bool dock_supported;

static PtcOperationModeStatus read_mode(void *ctx)
{
    PtcOperationModeStatus status = { PTC_OPERATION_MODE_UNKNOWN, model_known, dock_supported };
    OmmOperationMode mode;
    (void)ctx;
    if (!initialized) initialized = R_SUCCEEDED(ommInitialize());
    if (initialized && R_SUCCEEDED(ommGetOperationMode(&mode))) {
        if (mode == OmmOperationMode_Console) status.mode = PTC_OPERATION_MODE_DOCKED;
        else if (mode == OmmOperationMode_Handheld) status.mode = PTC_OPERATION_MODE_UNDOCKED;
    }
    return status;
}

void ptc_switch_operation_mode_init(PtcOperationModeProvider *provider)
{
    SetSysProductModel model;
    if (R_SUCCEEDED(setsysInitialize())) {
        model_known = R_SUCCEEDED(setsysGetProductModel(&model));
        dock_supported = model_known && model != SetSysProductModel_Hoag;
        setsysExit();
    }
    provider->read = read_mode;
    provider->ctx = NULL;
}

void ptc_switch_operation_mode_exit(void)
{
    if (initialized) ommExit();
    initialized = false;
}
#endif
