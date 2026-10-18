/* Minimal host-only libnx surface; production builds never use this include path. */
#ifndef PTC_SETUP_TEST_SWITCH_H
#define PTC_SETUP_TEST_SWITCH_H
#include <stddef.h>
#include <stdint.h>
typedef uint64_t u64;
typedef int Result;
typedef struct { int unused; } PadState;
typedef struct { int unused; } PtcSwitchIpcClient;
typedef struct { int x, y; } HidAnalogStickState;
typedef enum { AppletHookType_OnResume, AppletHookType_OnFocusState } AppletHookType;
typedef enum { ColorSetId_Light, ColorSetId_Dark } ColorSetId;
typedef enum { SetLanguage_ZHCN, SetLanguage_ZHHANS, SetLanguage_ZHTW, SetLanguage_ZHHANT,
    SetLanguage_ENUS, SetLanguage_ENGB, SetLanguage_JA } SetLanguage;
enum { HidNpadButton_A=1, HidNpadButton_B=2, HidNpadButton_X=4, HidNpadButton_Y=8,
    HidNpadButton_L=16, HidNpadButton_R=32, HidNpadButton_ZL=64, HidNpadButton_ZR=128,
    HidNpadButton_Plus=256, HidNpadButton_Minus=512, HidNpadButton_Up=1024,
    HidNpadButton_Down=2048, HidNpadButton_Left=4096, HidNpadButton_Right=8192 };
#define R_FAILED(rc) ((rc) != 0)
#define R_SUCCEEDED(rc) ((rc) == 0)
#define AppletFocusState_InFocus 1
Result setsysInitialize(void);
Result setsysGetColorSetId(ColorSetId *out);
void setsysExit(void);
Result setInitialize(void);
Result setGetSystemLanguage(u64 *out);
Result setMakeLanguage(u64 code, SetLanguage *out);
void setExit(void);
int appletGetFocusState(void);
void randomGet(void *out, size_t size);
#endif
