#ifndef PLAYWISE_PREVIEW_FONT_H
#define PLAYWISE_PREVIEW_FONT_H
/* Preview substitutes stb's rasterizer for FreeType, using an operator-supplied
 * local font. Geometry and UI drawing are the production implementation;
 * glyph hinting and the font are not Switch qualification evidence. */
#include <stdlib.h>
#include <string.h>
#ifdef PTC_UI_PREVIEW_FONT_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION
#endif
#include "../../companion/overlay/vendor/libtesla/include/stb_truetype.h"
typedef unsigned char FT_Byte;
typedef long FT_Long;
typedef unsigned int FT_UInt;
typedef void *FT_Library;
typedef struct {
    struct { unsigned int rows, width; int pitch; unsigned char *buffer; unsigned int pixel_mode; } bitmap;
    int bitmap_left, bitmap_top;
    struct { long x; } advance;
} PreviewGlyph;
typedef PreviewGlyph *FT_GlyphSlot;
typedef struct {
    stbtt_fontinfo info;
    float scale;
    PreviewGlyph slot;
    FT_GlyphSlot glyph;
} *FT_Face;
#define FT_LOAD_DEFAULT 0
#define FT_LOAD_RENDER 1
#define FT_PIXEL_MODE_GRAY 2
#define FT_PIXEL_MODE_BGRA 7
static inline int FT_Init_FreeType(FT_Library *lib) { *lib = (void *)1; return 0; }
static inline int FT_New_Memory_Face(FT_Library lib, const FT_Byte *data, FT_Long len, long index, FT_Face *out)
{
    (void)lib; (void)len;
    *out = calloc(1, sizeof(**out));
    if (!*out) return 1;
    (*out)->glyph = &(*out)->slot;
    return !stbtt_InitFont(&(*out)->info, data, stbtt_GetFontOffsetForIndex(data, (int)index));
}
static inline int FT_Set_Pixel_Sizes(FT_Face face, unsigned int w, unsigned int h)
{ (void)w; face->scale = stbtt_ScaleForMappingEmToPixels(&face->info, (float)h); return 0; }
static inline unsigned long preview_map_pua(stbtt_fontinfo *info, unsigned long code)
{
    if (stbtt_FindGlyphIndex(info, (int)code) != 0) return code;
    switch (code) {
    case 0xE0E0: return 'A';
    case 0xE0E1: return 'B';
    case 0xE0E2: return 'X';
    case 0xE0E3: return 'Y';
    case 0xE0E4: return 'L';
    case 0xE0E5: return 'R';
    case 0xE0E6: return 'Z';
    case 0xE0E7: return 'Z';
    case 0xE0EB: return '+';
    case 0xE0EC: return '-';
    default: return code;
    }
}
static inline FT_UInt FT_Get_Char_Index(FT_Face face, unsigned long code)
{ return (FT_UInt)stbtt_FindGlyphIndex(&face->info, (int)preview_map_pua(&face->info, code)); }
static inline int FT_Load_Char(FT_Face face, unsigned long code, int flags)
{
    unsigned long mapped = preview_map_pua(&face->info, code);
    int advance, bearing, w, h, x, y;
    free(face->slot.bitmap.buffer);
    memset(&face->slot, 0, sizeof(face->slot));
    stbtt_GetCodepointHMetrics(&face->info, (int)mapped, &advance, &bearing);
    face->slot.advance.x = (long)(advance * face->scale + 0.5f) * 64;
    if (flags == FT_LOAD_RENDER) {
        face->slot.bitmap.buffer = stbtt_GetCodepointBitmap(&face->info, face->scale, face->scale, (int)mapped, &w, &h, &x, &y);
        face->slot.bitmap.width = w;
        face->slot.bitmap.rows = h;
        face->slot.bitmap.pitch = w;
        face->slot.bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;
        face->slot.bitmap_left = x;
        face->slot.bitmap_top = -y;
    }
    return 0;
}
static inline void FT_Done_Face(FT_Face face) { free(face->slot.bitmap.buffer); free(face); }
static inline void FT_Done_FreeType(FT_Library lib) { (void)lib; }
#endif
