#include "atmosphere_version.h"

bool ptc_atmosphere_version_decode(uint64_t raw, PtcAtmosphereVersion *out)
{
    PtcAtmosphereVersion value;
    if (!out) return false;
    value.major = (uint8_t)(raw >> 56);
    value.minor = (uint8_t)(raw >> 48);
    value.micro = (uint8_t)(raw >> 40);
    if (value.major == 0U) return false;
    *out = value;
    return true;
}
