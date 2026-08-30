#ifndef PTC_ATMOSPHERE_VERSION_H
#define PTC_ATMOSPHERE_VERSION_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t major;
    uint8_t minor;
    uint8_t micro;
} PtcAtmosphereVersion;

/* Atmosphere ApiInfo stores major/minor/micro in bits 56/48/40. */
bool ptc_atmosphere_version_decode(uint64_t raw, PtcAtmosphereVersion *out);

#endif
