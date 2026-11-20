#pragma once

#include <algorithm>

namespace tsl::gfx::detail {
// Preserve the pinned renderer's rounding and alpha accumulation. The caller
// already applied opacity and clipping before obtaining the framebuffer pixel.
template<typename Color>
inline Color blendDestination(Color src, Color dst) {
    if (dst.a == 0xF) return dst;
    Color end = src;
    const auto blend = [](unsigned s, unsigned d, unsigned a) {
        return (d * a + s * (0xF - a)) / float(0xF);
    };
    end.r = blend(src.r, dst.r, dst.a);
    end.g = blend(src.g, dst.g, dst.a);
    end.b = blend(src.b, dst.b, dst.a);
    end.a = std::min<unsigned>(dst.a + src.a, 0xF);
    return end;
}
}
