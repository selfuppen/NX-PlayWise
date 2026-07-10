#ifndef PTC_TOKEN_PAYLOAD_H
#define PTC_TOKEN_PAYLOAD_H

#include <stdint.h>

#define PTC_TOKEN_ACTION_ADD_TODAY_MINUTES 1
#define PTC_TOKEN_PAYLOAD_BITS 60
#define PTC_TOKEN_MAC_BITS 20
#define PTC_TOKEN_TOTAL_BITS 80
#define PTC_TOKEN_SYMBOLS 16
#define PTC_TOKEN_GROUPS 4
#define PTC_TOKEN_GROUP_SYMBOLS 4
#define PTC_TOKEN_MAX_MINUTES 1440
#define PTC_TOKEN_MAX_NONCE 0x1ffffffu
#define PTC_TOKEN_DOMAIN "PTC1"

typedef struct {
    uint8_t version;
    uint8_t action;
    uint16_t minutes;
    uint16_t day_index_since_2020;
    uint32_t nonce;
} PtcTokenPayload;

#endif

