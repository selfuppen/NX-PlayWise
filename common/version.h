#ifndef PLAYWISE_VERSION_H
#define PLAYWISE_VERSION_H

#define PLAYWISE_VERSION "2.0.2-alpha"
#define PLAYWISE_REPOSITORY_URL "https://github.com/selfuppen/NX-PlayWise"
#define PLAYWISE_RELEASE_TITLE_ID "4200000000BD2300"
#define PLAYWISE_RELEASE_IPC_SERVICE "pctc:u"
#define PLAYWISE_RELEASE_SD_ROOT "sdmc:/switch/playwise"

#define PLAYWISE_DEVICE_LAB_TITLE_ID "4200000000BD23F0"
#define PLAYWISE_DEVICE_LAB_IPC_SERVICE "pwtl:u"
#define PLAYWISE_DEVICE_LAB_SD_ROOT "sdmc:/switch/playwise-device-lab"

/* Emulator-only quick test build. It has no sysmodule and no Title ID because it
   runs the control core inside the NRO process against a simulated PCTL. */
#define PLAYWISE_EDEN_SD_ROOT "sdmc:/switch/playwise-eden"

#ifdef PLAYWISE_EDEN
#define PLAYWISE_PROFILE_NAME "eden-test"
#define PLAYWISE_TITLE_ID "nro-only"
#define PLAYWISE_IPC_SERVICE "disabled"
#define PLAYWISE_SD_ROOT PLAYWISE_EDEN_SD_ROOT
#elif defined(PLAYWISE_DEVICE_LAB)
#define PLAYWISE_PROFILE_NAME "device-lab"
#define PLAYWISE_TITLE_ID PLAYWISE_DEVICE_LAB_TITLE_ID
#define PLAYWISE_IPC_SERVICE PLAYWISE_DEVICE_LAB_IPC_SERVICE
#define PLAYWISE_SD_ROOT PLAYWISE_DEVICE_LAB_SD_ROOT
#else
#define PLAYWISE_PROFILE_NAME "release"
#define PLAYWISE_TITLE_ID PLAYWISE_RELEASE_TITLE_ID
#define PLAYWISE_IPC_SERVICE PLAYWISE_RELEASE_IPC_SERVICE
#define PLAYWISE_SD_ROOT PLAYWISE_RELEASE_SD_ROOT
#endif

#define PLAYWISE_PROTOCOL_VERSION 1
#define PLAYWISE_RECOVERY_VERSION 1
#define PLAYWISE_PCTL_LAYOUT_VERSION 1

#endif
