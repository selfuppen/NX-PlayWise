#ifndef PTC_COMMON_PROTOCOL_CAPABILITY_BACKEND_H
#define PTC_COMMON_PROTOCOL_CAPABILITY_BACKEND_H

#define PTC_PLAY_TIMER_WRITE_BACKEND "pctl-s-v2"
#define PTC_PLAY_TIMER_EFFECT_BACKEND "pctl-s-runtime-v2"
/* raw block reuses the verified play timer settings write path with minutes=0. */
#define PTC_RAW_BLOCK_BACKEND "pctl-s-rawblock-v1"
/* Legacy Lab-only name: this proves only that the auxiliary 1457 event is accessible. */
#define PTC_SUSPEND_BACKEND "pctl-s-suspend-v1"

#endif
