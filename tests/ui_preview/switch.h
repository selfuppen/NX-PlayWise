#ifndef PLAYWISE_PREVIEW_SWITCH_H
#define PLAYWISE_PREVIEW_SWITCH_H
/* Host-only framebuffer adapter; never included in Switch builds. */
#include <stdint.h>
#include <stddef.h>
typedef int Result;
typedef struct { int unused; } Framebuffer;
typedef struct { const void *address; size_t size; } PlFontData;
#define PlServiceType_User 0
#define PlSharedFontType_ChineseSimplified 0
#define PIXEL_FORMAT_RGBA_8888 0
#define R_FAILED(value) ((value) != 0)
#define RGBA8_MAXALPHA(r,g,b) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | 0xFF000000u)
extern uint32_t preview_pixels[1280 * 720];
static inline int plInitialize(int type) { (void)type; return 0; }
static inline int plGetSharedFontByType(PlFontData *data, int type) { (void)data; (void)type; return 1; }
static inline void plExit(void) {}
static inline void *nwindowGetDefault(void) { return NULL; }
static inline int framebufferCreate(Framebuffer *fb, void *window, int w, int h, int format, int count)
{ (void)fb; (void)window; (void)w; (void)h; (void)format; (void)count; return 0; }
static inline int framebufferMakeLinear(Framebuffer *fb) { (void)fb; return 0; }
static inline void framebufferClose(Framebuffer *fb) { (void)fb; }
static inline void *framebufferBegin(Framebuffer *fb, uint32_t *stride)
{ (void)fb; *stride = 1280 * 4; return preview_pixels; }
static inline void framebufferEnd(Framebuffer *fb) { (void)fb; }
#endif
