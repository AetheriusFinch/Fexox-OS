/* Fexox OS Project, EULA GPLv2 */
 


#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "Framebuffer.h"
#include "png.h"
#include "debug_out.h"
#include "AcpiControl.h"
#include "Resources/fxapp.h"
#include "USBHID.h"

 


extern void *kmalloc(uint64_t size);
extern void  kfree(void *ptr);

 

extern size_t strlcpy(char *dst, const char *src, size_t n);
extern char  *strncat(char *dst, const char *src, size_t n);
extern size_t strlen(const char *s);
extern int    strcmp(const char *a, const char *b);
extern char  *strrchr(const char *s, int c);

 

#define DIRECT_MAP_OFFSET 0xFFFF880000000000ULL

 
extern void *memset(void *dst, int c, size_t n);
extern void *memcpy(void *dst, const void *src, size_t n);

 

static const uint8_t g_font8x16[96][16] = {
     
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x66,0x66,0x66,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x36,0x36,0x7F,0x36,0x36,0x36,0x7F,0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00},
     
    {0x18,0x18,0x7C,0xC6,0xC2,0xC0,0x7C,0x06,0x06,0x86,0xC6,0x7C,0x18,0x18,0x00,0x00},
     
    {0x00,0x00,0xC2,0xC6,0x0C,0x18,0x30,0x60,0xC6,0x86,0x00,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x38,0x6C,0x6C,0x38,0x76,0xDC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x30,0x30,0x30,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x0C,0x18,0x30,0x60,0x60,0x60,0x60,0x60,0x30,0x18,0x0C,0x00,0x00,0x00,0x00},
     
    {0x00,0x60,0x30,0x18,0x0C,0x0C,0x0C,0x0C,0x0C,0x18,0x30,0x60,0x00,0x00,0x00,0x00},
     
    {0x00,0x00,0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x00,0x00,0x18,0x18,0x18,0xFF,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x30,0x00,0x00,0x00},
     
    {0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x00,0x02,0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x3C,0x66,0xC3,0xC3,0xDB,0xDB,0xC3,0xC3,0x66,0x3C,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x18,0x38,0x58,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x7C,0xC6,0x06,0x0C,0x18,0x30,0x60,0xC0,0xC6,0xFE,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x7C,0xC6,0x06,0x06,0x3C,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x0C,0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x0C,0x1E,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0xFE,0xC0,0xC0,0xC0,0xFC,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x38,0x60,0xC0,0xC0,0xFC,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0xFE,0xC6,0x06,0x06,0x0C,0x18,0x30,0x30,0x30,0x30,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x7C,0xC6,0xC6,0xC6,0x7E,0x06,0x06,0x06,0x0C,0x78,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x18,0x30,0x00,0x00,0x00,0x00},
     
    {0x00,0x00,0x06,0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x06,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x00,0x60,0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x60,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x7C,0xC6,0xC6,0x0C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x7C,0xC6,0xC6,0xDE,0xDE,0xDE,0xDC,0xC0,0xC4,0x7C,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x10,0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0xFC,0x66,0x66,0x66,0x7C,0x66,0x66,0x66,0x66,0xFC,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xC0,0xC0,0xC2,0x66,0x3C,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0xF8,0x6C,0x66,0x66,0x66,0x66,0x66,0x66,0x6C,0xF8,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xDE,0xC6,0xC6,0x66,0x3A,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0xC6,0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x3C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0xCC,0xCC,0xCC,0x78,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0xE6,0x66,0x6C,0x6C,0x78,0x78,0x6C,0x66,0x66,0xE6,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0xF0,0x60,0x60,0x60,0x60,0x60,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0xC6,0xE6,0xF6,0xFE,0xDE,0xCE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0xFC,0x66,0x66,0x66,0x7C,0x60,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x0C,0x0E,0x00,0x00,0x00},
     
    {0x00,0xFC,0x66,0x66,0x66,0x7C,0x6C,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x7C,0xC6,0xC6,0x60,0x38,0x0C,0x06,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0xFF,0xDB,0x99,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0xC6,0xC6,0xC6,0xC6,0xD6,0xD6,0xD6,0xFE,0xEE,0xC6,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0xC6,0xC6,0x6C,0x6C,0x38,0x38,0x6C,0x6C,0xC6,0xC6,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0xCC,0xCC,0xCC,0xCC,0x78,0x30,0x30,0x30,0x30,0x78,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0xFE,0xC6,0x86,0x0C,0x18,0x30,0x60,0xC2,0xC6,0xFE,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x3C,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x3C,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x00,0x80,0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00,0x00,0x00,0x00,0x00},
     
    {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0x00,0x00},
     
    {0x00,0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x00,0x00,0x00,0x78,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0xE0,0x60,0x60,0x78,0x6C,0x66,0x66,0x66,0x66,0x7C,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x00,0x00,0x00,0x7C,0xC6,0xC0,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x1C,0x0C,0x0C,0x3C,0x6C,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x00,0x00,0x00,0x7C,0xC6,0xFE,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x1C,0x36,0x32,0x30,0x78,0x30,0x30,0x30,0x30,0x78,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0xCC,0x78,0x00,0x00,0x00},
     
    {0x00,0xE0,0x60,0x60,0x6C,0x76,0x66,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x18,0x18,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x06,0x06,0x00,0x0E,0x06,0x06,0x06,0x06,0x06,0x66,0x66,0x3C,0x00,0x00,0x00},
     
    {0x00,0xE0,0x60,0x60,0x66,0x6C,0x78,0x78,0x6C,0x66,0xE6,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x00,0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xD6,0xD6,0xC6,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x00,0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00,0x00,0x00},
     
    {0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0x0C,0x1E,0x00,0x00,0x00},
     
    {0x00,0x00,0x00,0x00,0xDC,0x76,0x66,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x00,0x00,0x00,0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x10,0x30,0x30,0xFC,0x30,0x30,0x30,0x30,0x36,0x1C,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x00,0x00,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x00,0x00,0x00,0xC6,0xC6,0xC6,0xD6,0xD6,0xFE,0x6C,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x00,0x00,0x00,0xC6,0x6C,0x38,0x38,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x00,0x00,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0xCC,0x78,0x00,0x00,0x00},
     
    {0x00,0x00,0x00,0x00,0xFE,0xCC,0x18,0x30,0x60,0xC6,0xFE,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x0E,0x18,0x18,0x18,0x70,0x18,0x18,0x18,0x18,0x0E,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x18,0x18,0x18,0x18,0x00,0x00,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x70,0x18,0x18,0x18,0x0E,0x18,0x18,0x18,0x18,0x70,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
     
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

 


typedef struct {
    int      mode;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t  bpp;
    uint8_t  bytes_pp;

     





    uint32_t native_width;
    uint32_t native_height;

    uint8_t *front;
    uint8_t *back;
    uint64_t phys_addr;    

    uint8_t  r_shift;
    uint8_t  g_shift;
    uint8_t  b_shift;
} fb_state_t;

static fb_state_t g_fb;

static uint8_t mask_to_shift(uint32_t mask) {
    if (!mask) return 0;
    uint8_t s = 0;
    while ((mask & 1) == 0) { mask >>= 1; s++; }
    return s;
}

static inline uint32_t rgb_to_native(fb_color_t c) {
    uint8_t r = (c >> 16) & 0xFF;
    uint8_t g = (c >>  8) & 0xFF;
    uint8_t b = (c      ) & 0xFF;
    return ((uint32_t)r << g_fb.r_shift) |
           ((uint32_t)g << g_fb.g_shift) |
           ((uint32_t)b << g_fb.b_shift);
}

static inline uint8_t *draw_buf(void) {
    return g_fb.back ? g_fb.back : g_fb.front;
}

 


int fb_init(const fb_info_t *info) {
    for (int i = 0; i < (int)sizeof(fb_state_t); i++)
        ((uint8_t *)&g_fb)[i] = 0;

    if (!info || info->mode == FB_MODE_VGA || info->mode == FB_MODE_NONE) {
        g_fb.mode = FB_MODE_VGA;
        DBG_MSG("FB", "VGA text mode fallback");
        return FB_MODE_VGA;
    }

    if (info->mode == FB_MODE_LINEAR && info->phys_addr && info->width && info->height) {
        g_fb.mode      = FB_MODE_LINEAR;
        g_fb.width     = info->width;
        g_fb.height    = info->height;
        g_fb.native_width  = info->width;
        g_fb.native_height = info->height;
        g_fb.pitch     = info->pitch  ? info->pitch  : info->width * (info->bpp / 8);
        g_fb.bpp       = info->bpp    ? info->bpp    : 32;
        g_fb.bytes_pp  = g_fb.bpp / 8;

        g_fb.front      = (uint8_t *)(uintptr_t)(info->phys_addr + DIRECT_MAP_OFFSET);
        g_fb.phys_addr  = info->phys_addr;

        if (info->red_mask || info->green_mask || info->blue_mask) {
            g_fb.r_shift = mask_to_shift(info->red_mask);
            g_fb.g_shift = mask_to_shift(info->green_mask);
            g_fb.b_shift = mask_to_shift(info->blue_mask);
        } else {
            g_fb.r_shift = 16;
            g_fb.g_shift = 8;
            g_fb.b_shift = 0;
        }

        DBG_MSG("FB", "linear framebuffer init");
        DBG_VAL("FB", "width",  g_fb.width);
        DBG_VAL("FB", "height", g_fb.height);
        DBG_VAL("FB", "pitch",  g_fb.pitch);
        DBG_VAL("FB", "front",  (uint64_t)(uintptr_t)g_fb.front);

        return FB_MODE_LINEAR;
    }

    g_fb.mode = FB_MODE_VGA;
    return FB_MODE_VGA;
}

int      fb_get_mode(void)   { return g_fb.mode; }
uint32_t fb_width(void)      { return g_fb.width; }
uint32_t fb_height(void)     { return g_fb.height; }

 



uint64_t fb_phys_addr(void)  { return (g_fb.mode == FB_MODE_LINEAR) ? g_fb.phys_addr : 0; }
uint32_t fb_pitch(void)      { return (g_fb.mode == FB_MODE_LINEAR) ? g_fb.pitch     : 0; }
uint8_t  fb_bpp(void)        { return (g_fb.mode == FB_MODE_LINEAR) ? g_fb.bpp       : 0; }

 







void fb_get_channel_shifts(uint8_t *r_shift, uint8_t *g_shift, uint8_t *b_shift) {
    if (r_shift) *r_shift = g_fb.r_shift;
    if (g_shift) *g_shift = g_fb.g_shift;
    if (b_shift) *b_shift = g_fb.b_shift;
}

 


int fb_enable_double_buffer(void) {
    if (g_fb.mode != FB_MODE_LINEAR) return -1;
    if (g_fb.back) return 0;

    uint64_t size = (uint64_t)g_fb.pitch * g_fb.height;
    g_fb.back = (uint8_t *)kmalloc(size);
    if (!g_fb.back) { DBG_MSG("FB", "double-buffer: kmalloc failed"); return -1; }

     


    memset(g_fb.back, 0, size);
    DBG_MSG("FB", "double-buffer enabled");
    return 0;
}

void fb_disable_double_buffer(void) {
    if (g_fb.back) { kfree(g_fb.back); g_fb.back = 0; }
}

 













static void memcpy_nt(void *dst, const void *src, size_t n) {
    uint64_t       *d = (uint64_t *)dst;
    const uint64_t *s = (const uint64_t *)src;
    size_t count = n / 8;

    for (size_t i = 0; i < count; i++) {
        __asm__ volatile (
            "movnti %1, %0"
            : "=m"(d[i])
            : "r"(s[i])
            :
        );
    }

     
    __asm__ volatile ("sfence" ::: "memory");

     
    size_t tail = n & 7u;
    if (tail) {
        uint8_t       *db = (uint8_t *)(d + count);
        const uint8_t *sb = (const uint8_t *)(s + count);
        for (size_t i = 0; i < tail; i++) db[i] = sb[i];
    }
}

void fb_flip(void) {
    if (!g_fb.back || !g_fb.front) return;
     


    memcpy_nt(g_fb.front, g_fb.back, (size_t)g_fb.pitch * g_fb.height);
}

 















static bool fb_apply_virtual_resolution(uint32_t w, uint32_t h) {
    if (g_fb.mode != FB_MODE_LINEAR) return false;
    if (!w || !h) return false;
    if (w > g_fb.native_width)  w = g_fb.native_width;
    if (h > g_fb.native_height) h = g_fb.native_height;

     



    g_fb.width  = g_fb.native_width;
    g_fb.height = g_fb.native_height;
    fb_clear(FB_BLACK);
    fb_flip();

    g_fb.width  = w;
    g_fb.height = h;
    return true;
}

 


















void fb_flip_rect(int32_t x, int32_t y, uint32_t w, uint32_t h) {
    if (!g_fb.back || !g_fb.front) return;
    if (w == 0 || h == 0) return;

    if (x < 0) { if ((uint32_t)(-x) >= w) return; w += (uint32_t)x; x = 0; }
    if (y < 0) { if ((uint32_t)(-y) >= h) return; h += (uint32_t)y; y = 0; }
    if ((uint32_t)x >= g_fb.width || (uint32_t)y >= g_fb.height) return;
    if ((uint64_t)x + w > g_fb.width)  w = g_fb.width  - (uint32_t)x;
    if ((uint64_t)y + h > g_fb.height) h = g_fb.height - (uint32_t)y;

    size_t row_bytes = (size_t)w * g_fb.bytes_pp;
    for (uint32_t row = 0; row < h; row++) {
        uint8_t *dst = (uint8_t *)g_fb.front + (uint64_t)(y + row) * g_fb.pitch + (uint64_t)x * g_fb.bytes_pp;
        uint8_t *src = (uint8_t *)g_fb.back  + (uint64_t)(y + row) * g_fb.pitch + (uint64_t)x * g_fb.bytes_pp;
        memcpy_nt(dst, src, row_bytes);
    }
}

void *fb_back_ptr(void) { return g_fb.back; }

 



void ui_flip_userspace_windows(void);

 



void fb_put_pixel(int32_t x, int32_t y, fb_color_t color) {
    if (g_fb.mode != FB_MODE_LINEAR) return;
    if ((uint32_t)x >= g_fb.width || (uint32_t)y >= g_fb.height) return;

    uint32_t native = rgb_to_native(color);
    uint8_t *dst = draw_buf() + (uint64_t)y * g_fb.pitch + (uint64_t)x * g_fb.bytes_pp;

    switch (g_fb.bytes_pp) {
        case 4: dst[3] = (uint8_t)(native >> 24);  
        case 3: dst[2] = (uint8_t)(native >> 16);  
        case 2: dst[1] = (uint8_t)(native >>  8);  
        case 1: dst[0] = (uint8_t)(native      ); break;
    }
}

fb_color_t fb_get_pixel(int32_t x, int32_t y) {
    if (g_fb.mode != FB_MODE_LINEAR) return 0;
    if ((uint32_t)x >= g_fb.width || (uint32_t)y >= g_fb.height) return 0;

    const uint8_t *src = draw_buf() + (uint64_t)y * g_fb.pitch + (uint64_t)x * g_fb.bytes_pp;
    uint32_t native = 0;
    switch (g_fb.bytes_pp) {
        case 4: native |= (uint32_t)src[3] << 24;  
        case 3: native |= (uint32_t)src[2] << 16;  
        case 2: native |= (uint32_t)src[1] <<  8;  
        case 1: native |= (uint32_t)src[0];       break;
    }
    uint8_t r = (uint8_t)((native >> g_fb.r_shift) & 0xFF);
    uint8_t g = (uint8_t)((native >> g_fb.g_shift) & 0xFF);
    uint8_t b = (uint8_t)((native >> g_fb.b_shift) & 0xFF);
    return ((fb_color_t)r << 16) | ((fb_color_t)g << 8) | b;
}

void fb_hline(int32_t x, int32_t y, uint32_t len, fb_color_t color) {
    if (g_fb.mode != FB_MODE_LINEAR) return;
    if ((uint32_t)y >= g_fb.height) return;
    if (x < 0) { if ((uint32_t)(-x) >= len) return; len += x; x = 0; }
    if ((uint32_t)x + len > g_fb.width) len = g_fb.width - (uint32_t)x;
    if (!len) return;

    uint32_t native = rgb_to_native(color);
    uint8_t *row = draw_buf() + (uint64_t)y * g_fb.pitch + (uint64_t)x * g_fb.bytes_pp;

    if (g_fb.bytes_pp == 4) {
        uint32_t *p = (uint32_t *)row;
        for (uint32_t i = 0; i < len; i++) p[i] = native;
    } else {
        for (uint32_t i = 0; i < len; i++) {
            uint8_t *p = row + i * g_fb.bytes_pp;
            switch (g_fb.bytes_pp) {
                case 3: p[2] = (uint8_t)(native >> 16);  
                case 2: p[1] = (uint8_t)(native >>  8);  
                case 1: p[0] = (uint8_t)(native      ); break;
            }
        }
    }
}

void fb_vline(int32_t x, int32_t y, uint32_t len, fb_color_t color) {
    if (g_fb.mode != FB_MODE_LINEAR) return;
    if ((uint32_t)x >= g_fb.width) return;
    if (y < 0) { if ((uint32_t)(-y) >= len) return; len += y; y = 0; }
    if ((uint32_t)y + len > g_fb.height) len = g_fb.height - (uint32_t)y;
    for (uint32_t i = 0; i < len; i++)
        fb_put_pixel(x, (int32_t)((uint32_t)y + i), color);
}

void fb_fill_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, fb_color_t color) {
    if (g_fb.mode != FB_MODE_LINEAR) return;
    if (x < 0) { if ((uint32_t)(-x) >= w) return; w += x; x = 0; }
    if (y < 0) { if ((uint32_t)(-y) >= h) return; h += y; y = 0; }
    if ((uint32_t)x >= g_fb.width || (uint32_t)y >= g_fb.height) return;
    if ((uint32_t)x + w > g_fb.width)  w = g_fb.width  - (uint32_t)x;
    if ((uint32_t)y + h > g_fb.height) h = g_fb.height - (uint32_t)y;
    if (!w || !h) return;

    for (uint32_t row = 0; row < h; row++)
        fb_hline(x, (int32_t)((uint32_t)y + row), w, color);
}

void fb_draw_rect(int32_t x, int32_t y, uint32_t w, uint32_t h,
                  fb_color_t color, uint32_t t) {
    if (!t) t = 1;
    fb_fill_rect(x,                  y,                  w, t, color);
    fb_fill_rect(x,                  y + (int32_t)(h-t), w, t, color);
    fb_fill_rect(x,                  y,                  t, h, color);
    fb_fill_rect(x + (int32_t)(w-t), y,                  t, h, color);
}

void fb_clear(fb_color_t color) {
    fb_fill_rect(0, 0, g_fb.width, g_fb.height, color);
}

void fb_blit(int32_t dx, int32_t dy, int32_t sx, int32_t sy, uint32_t w, uint32_t h) {
    if (g_fb.mode != FB_MODE_LINEAR) return;
    uint8_t *buf = draw_buf();
    for (uint32_t row = 0; row < h; row++) {
        int32_t sr = (int32_t)((uint32_t)sy + row);
        int32_t dr = (int32_t)((uint32_t)dy + row);
        if ((uint32_t)sr >= g_fb.height || (uint32_t)dr >= g_fb.height) continue;
        uint8_t *sp = buf + (uint64_t)(uint32_t)sr * g_fb.pitch + (uint64_t)(uint32_t)sx * g_fb.bytes_pp;
        uint8_t *dp = buf + (uint64_t)(uint32_t)dr * g_fb.pitch + (uint64_t)(uint32_t)dx * g_fb.bytes_pp;
        uint64_t nbytes = (uint64_t)w * g_fb.bytes_pp;
        extern void *memmove(void *, const void *, size_t);
        memmove(dp, sp, (size_t)nbytes);
    }
}

void fb_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, fb_color_t color) {
    int32_t dx =  (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int32_t dy = -((y1 > y0) ? (y1 - y0) : (y0 - y1));
    int32_t sx = (x0 < x1) ? 1 : -1;
    int32_t sy = (y0 < y1) ? 1 : -1;
    int32_t err = dx + dy;

    while (1) {
        fb_put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = err * 2;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

 


void fb_draw_char(int32_t x, int32_t y, char c, fb_color_t fg, fb_color_t bg) {
    if (g_fb.mode != FB_MODE_LINEAR) return;

    uint8_t idx = (uint8_t)c;
    if (idx < 0x20 || idx > 0x7F) idx = '?';
    const uint8_t *glyph = g_font8x16[idx - 0x20];

    bool transparent = (bg == FB_TRANSPARENT_BG);

    for (int row = 0; row < FB_FONT_H; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < FB_FONT_W; col++) {
            bool set = (bits >> (7 - col)) & 1;
            if (set)
                fb_put_pixel(x + col, y + row, fg);
            else if (!transparent)
                fb_put_pixel(x + col, y + row, bg);
        }
    }
}

int32_t fb_draw_string(int32_t x, int32_t y, const char *str,
                       fb_color_t fg, fb_color_t bg) {
    while (*str) {
        fb_draw_char(x, y, *str, fg, bg);
        x += FB_FONT_W;
        str++;
    }
    return x;
}

 


typedef struct {
    uint32_t   ox, oy;
    uint32_t   ow, oh;
    uint32_t   cols;
    uint32_t   rows;
    uint32_t   cur_col;
    uint32_t   cur_row;
    fb_color_t fg, bg;
} fbcon_t;

static fbcon_t g_con;

void fbcon_init(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                fb_color_t fg, fb_color_t bg) {
    g_con.ox      = x;
    g_con.oy      = y;
    g_con.ow      = w;
    g_con.oh      = h;
    g_con.cols    = w / FB_FONT_W;
    g_con.rows    = h / FB_FONT_H;
    g_con.cur_col = 0;
    g_con.cur_row = 0;
    g_con.fg      = fg;
    g_con.bg      = bg;

    if (g_fb.mode == FB_MODE_LINEAR)
        fb_fill_rect((int32_t)x, (int32_t)y, w, h, bg);
}

void fbcon_set_colors(fb_color_t fg, fb_color_t bg) {
    g_con.fg = fg;
    g_con.bg = bg;
}

void fbcon_clear(void) {
    if (g_fb.mode == FB_MODE_LINEAR)
        fb_fill_rect((int32_t)g_con.ox, (int32_t)g_con.oy,
                     g_con.ow, g_con.oh, g_con.bg);
    g_con.cur_col = 0;
    g_con.cur_row = 0;
}

static void fbcon_scroll(void) {
    if (g_fb.mode != FB_MODE_LINEAR) return;
    fb_blit((int32_t)g_con.ox,
            (int32_t)g_con.oy,
            (int32_t)g_con.ox,
            (int32_t)(g_con.oy + FB_FONT_H),
            g_con.ow,
            g_con.oh - (uint32_t)FB_FONT_H);
    fb_fill_rect((int32_t)g_con.ox,
                 (int32_t)(g_con.oy + g_con.oh - (uint32_t)FB_FONT_H),
                 g_con.ow, (uint32_t)FB_FONT_H, g_con.bg);
}

void fbcon_putc(char c) {
    if (g_fb.mode == FB_MODE_VGA) {
        dbg_putc(c);
        return;
    }

    if (c == '\r') { g_con.cur_col = 0; return; }

    if (c == '\n') {
        g_con.cur_col = 0;
        g_con.cur_row++;
        if (g_con.cur_row >= g_con.rows) {
            fbcon_scroll();
            g_con.cur_row = g_con.rows - 1;
        }
        return;
    }

    if (c == '\b') {
        if (g_con.cur_col > 0) {
            g_con.cur_col--;
            fb_fill_rect(
                (int32_t)(g_con.ox + g_con.cur_col * FB_FONT_W),
                (int32_t)(g_con.oy + g_con.cur_row * FB_FONT_H),
                FB_FONT_W, FB_FONT_H, g_con.bg);
        }
        return;
    }

    fb_draw_char(
        (int32_t)(g_con.ox + g_con.cur_col * FB_FONT_W),
        (int32_t)(g_con.oy + g_con.cur_row * FB_FONT_H),
        c, g_con.fg, g_con.bg);

    g_con.cur_col++;
    if (g_con.cur_col >= g_con.cols) {
        g_con.cur_col = 0;
        g_con.cur_row++;
        if (g_con.cur_row >= g_con.rows) {
            fbcon_scroll();
            g_con.cur_row = g_con.rows - 1;
        }
    }
}

void fbcon_puts(const char *str) {
    while (*str) fbcon_putc(*str++);
}

 



 
static inline uint8_t cmos_read(uint8_t reg) {
    dbg_outb(0x70, reg);
    for (volatile int i = 0; i < 64; i++) (void)dbg_inb(0x80);
    return dbg_inb(0x71);
}

static inline int bcd_to_bin(uint8_t v) {
    return (v & 0x0F) + ((v >> 4) * 10);
}

static void rtc_get_msk(int *out_h, int *out_m, int *out_s) {
     






    uint8_t ss, mm, hh, ss2, mm2, hh2;
    int guard;
    do {
        guard = 1000000;
        while ((cmos_read(0x0A) & 0x80) && guard-- > 0) ;
        ss = cmos_read(0x00);
        mm = cmos_read(0x02);
        hh = cmos_read(0x04);

        guard = 1000000;
        while ((cmos_read(0x0A) & 0x80) && guard-- > 0) ;
        ss2 = cmos_read(0x00);
        mm2 = cmos_read(0x02);
        hh2 = cmos_read(0x04);
    } while (ss != ss2 || mm != mm2 || hh != hh2);

    uint8_t sb = cmos_read(0x0B);

    if (!(sb & 0x04)) {
        ss = (uint8_t)bcd_to_bin(ss);
        mm = (uint8_t)bcd_to_bin(mm);
        hh = (uint8_t)bcd_to_bin(hh);
    }

    if (!(sb & 0x02)) {
        bool pm = (hh & 0x80) != 0;
        hh &= 0x7F;
        if (pm && hh != 12) hh += 12;
        else if (!pm && hh == 12) hh = 0;
    }

    int h = (int)hh + 3;
    if (h >= 24) h -= 24;

    *out_h = h;
    *out_m = (int)mm;
    *out_s = (int)ss;
}

static void fb_draw_digit2(int32_t x, int32_t y, int val,
                            fb_color_t fg, fb_color_t bg) {
    char buf[3];
    buf[0] = (char)('0' + (val / 10) % 10);
    buf[1] = (char)('0' + val % 10);
    buf[2] = '\0';
    fb_draw_string(x, y, buf, fg, bg);
}

 

#define TASKBAR_H            38u
 


static fb_color_t TASKBAR_BG = 0xDCC2A0U;

 



#define MENU_BTN_X      0    
#define MENU_BTN_W      94u
#define MENU_BTN_H      TASKBAR_H
#define MENU_BTN_RADIUS  16    
 
static fb_color_t MENU_BTN_BG = 0xFF9F40U;
#define MENU_BTN_FG     0x202020U
#define WALLPAPER_COLOR 0x6C9FCEU
#define CLOCK_FG        0x101010U
#define CLOCK_BG        TASKBAR_BG

 











typedef struct {
    const char *name;             
    const char *wallpaper_file;   
    fb_color_t  taskbar_bg;
    fb_color_t  menu_btn_bg;
    fb_color_t  app_btn_bg;
    fb_color_t  win_titlebar_bg;
} ui_theme_t;

#define UI_THEME_DESERT 0    
#define UI_THEME_SPACE  1
#define UI_THEME_LINES  2
#define UI_THEME_COUNT  3

static const ui_theme_t g_themes[UI_THEME_COUNT] = {
     

    { "Desert", "FexoxLightW.png", 0xDCC2A0U, 0xFF9F40U, 0xECE9D8U, 0xC2661AU },
     
    { "Space",  "SpaceTheme.png",  0xE4EEF8U, 0x3BA8DEU, 0x3BA8DEU, 0x3BA8DEU },
     

    { "Lines",  "ThemeLines.png",  0xBEF2EDU, 0x3ED6C4U, 0x3ED6C4U, 0x20B2AAU },
};

static int g_theme_idx = UI_THEME_DESERT;
static void apply_theme(int idx);    

 










#define MENU_POPUP_W        190u
#define MENU_POPUP_H         98u
#define MENU_POPUP_GAP        0    
#define MENU_POPUP_BG        0xFFD9A6U    
#define MENU_POPUP_BORDER    0xC98A4EU    
#define MENU_ITEM_W          (MENU_POPUP_W - 20u)
#define MENU_ITEM_H           36u
#define MENU_ITEM_GAP          8
#define MENU_ITEM_BG          0xFFF1DCU   
#define MENU_ITEM_BG_HOVER    0xFFB966U   
#define MENU_ITEM_FG          0x202020U

static bool       g_menu_open  = false;
static int        g_menu_hover = -1;       
static uint8_t    g_prev_buttons = 0;
static fb_color_t *g_menu_saved_bg = NULL;

static void ui_system_shutdown(void);
static void ui_system_reboot(void);

 












#define FXAPP_ICON_W          64u
#define FXAPP_ICON_H          64u
#define FXAPP_ICON_MARGIN     16    
#define FXAPP_ICON_HALO_PAD    6    
#define FXAPP_ICON_HALO_COLOR  0x9B59B6U    
#define FXAPP_ICON_HALO_ALPHA  90U          

#define FXAPP_ICON_FILE_MAXSZ  (256u * 1024u)

#define FXAPP_WIN_CLOSE_SZ     20u     
#define FXAPP_WIN_W           420u     

#define FXAPP_WIN_H           360u     
 



static png_image_t g_fxapp_icon_img;               
static int          g_fxapp_icon_loaded  = 0;
static png_image_t g_fxapp_close_img;              
static int          g_fxapp_close_loaded = 0;
static png_image_t g_fxapp_expand_img;              
static int          g_fxapp_expand_loaded = 0;
static png_image_t g_fxapp_minimize_img;            
static int          g_fxapp_minimize_loaded = 0;

static char g_fxapp_title[FXAPP_NAME_MAX] = "My Computer";
static char g_fxapp_icon_rel[FXAPP_ICON_MAX] = "MyPC.png";

static fb_color_t *g_fxapp_icon_clean_bg = NULL;    
static bool         g_fxapp_icon_hover = false;

 



static int g_fxapp_win_id = -1;  

 





static int g_theme_win_id = -1;  
static void theme_win_draw(int win_id);

 






static png_image_t g_calc_icon_img;                 
static int          g_calc_icon_loaded = 0;
static char g_calc_title[FXAPP_NAME_MAX] = "Calculator";
static char g_calc_icon_rel[FXAPP_ICON_MAX] = "calc.png";
static char g_calc_elf_path[FXAPP_PATH_MAX] = "/APPS/calc.elf";   




static fb_color_t *g_calc_icon_clean_bg = NULL;
static bool         g_calc_icon_hover = false;
static int32_t      g_calc_running_tid = -1;   
extern bool sched_task_alive(uint32_t tid);    

 




static png_image_t g_syslogs_icon_img;              
static int          g_syslogs_icon_loaded = 0;
static char g_syslogs_title[FXAPP_NAME_MAX] = "System Logs";
static char g_syslogs_icon_rel[FXAPP_ICON_MAX] = "SysLogs.png";
static char g_syslogs_elf_path[FXAPP_PATH_MAX] = "/APPS/syslogs.elf";   

static fb_color_t *g_syslogs_icon_clean_bg = NULL;
static bool         g_syslogs_icon_hover = false;
static int32_t      g_syslogs_running_tid = -1;   

 





static png_image_t g_browser_icon_img;              
static int          g_browser_icon_loaded = 0;
static char g_browser_title[FXAPP_NAME_MAX] = "Browser";
static char g_browser_icon_rel[FXAPP_ICON_MAX] = "Browser.png";
static char g_browser_elf_path[FXAPP_PATH_MAX] = "/APPS/browser.elf";   

static fb_color_t *g_browser_icon_clean_bg = NULL;
static bool         g_browser_icon_hover = false;
static int32_t      g_browser_running_tid = -1;   

static void icon_rect(int32_t *ix, int32_t *iy);
static void calc_icon_rect(int32_t *ix, int32_t *iy);
static void syslogs_icon_rect(int32_t *ix, int32_t *iy);
static void browser_icon_rect(int32_t *ix, int32_t *iy);
static void fxapp_ensure_assets_loaded(void);
static void fxapp_capture_icon_bg(void);
static void fxapp_draw_icon(bool hovered);
static void calc_capture_icon_bg(void);
static void calc_draw_icon(bool hovered);
static void syslogs_capture_icon_bg(void);
static void syslogs_draw_icon(bool hovered);
static void browser_draw_icon(bool hovered);

 






#define UI_MAX_WINDOWS        8u

#define UI_WIN_TITLEBAR_H     28u
#define UI_WIN_CLOSE_SZ       20u
#define UI_WIN_EXPAND_SZ      20u
#define UI_WIN_MIN_SZ         20u
#define UI_WIN_BTN_GAP         4u    
 
static fb_color_t UI_WIN_TITLEBAR_BG = 0xC2661AU;
#define UI_WIN_TITLEBAR_FG    FB_WHITE
#define UI_WIN_BODY_BG        FB_WHITE
#define UI_WIN_BORDER         0x404040U

 

#define TASKBAR_APP_BTN_GAP        8u     

#define TASKBAR_APP_BTN_W         140u    
#define TASKBAR_APP_BTN_H         (MENU_BTN_H)
 

static fb_color_t TASKBAR_APP_BTN_BG = 0xECE9D8U;
#define TASKBAR_APP_BTN_FG        0x202020U
#define TASKBAR_APP_BTN_TEXT_PAD   8

typedef struct {
    bool     used;           
    bool     open;           
    char     title[UI_WIN_TITLE_MAX];
    uint32_t w, h;            
    int32_t  x, y;
    int32_t  owner_pid;       

    uint8_t    *saved_bg;     
    uint8_t    *content_bak;  





    bool        dragging;
    int32_t     drag_off_x, drag_off_y;

    bool        minimized;    


    bool        maximized;    

    int32_t     rest_x, rest_y;
    uint32_t    rest_w, rest_h;

     








    uint32_t    redraw_gen;
} ui_window_t;

static ui_window_t g_windows[UI_MAX_WINDOWS];

 









static int g_focus_win = -1;

static void ui_focus_set(int id) {
    if (id == g_focus_win) return;
    DBG_VAL("FOCUS", "focus win id (0xFFFF.. = none)", (uint64_t)(int64_t)id);    
    g_focus_win = id;
    usb_hid_kbd_flush();    
}

 

static void ui_focus_release(int id) {
    if (g_focus_win != id) return;
    int next = -1;
    for (int i = (int)UI_MAX_WINDOWS - 1; i >= 0; i--) {
        if (i == id) continue;
        const ui_window_t *w = &g_windows[i];
        if (!w->used || !w->open || w->minimized) continue;
        next = i; break;
    }
    g_focus_win = next;
    usb_hid_kbd_flush();
}

 
int ui_kbd_dbg_focus(int32_t *owner, int *minimized) {
    if (owner) *owner = -99;
    if (minimized) *minimized = 0;
    if (g_focus_win < 0 || g_focus_win >= (int)UI_MAX_WINDOWS) return -1;
    const ui_window_t *w = &g_windows[g_focus_win];
    if (owner) *owner = (w->used && w->open) ? w->owner_pid : -98;
    if (minimized) *minimized = w->minimized;
    return g_focus_win;
}

 
bool ui_kbd_focus_owned_by(int32_t pid) {
    if (pid <= 0 || g_focus_win < 0 || g_focus_win >= (int)UI_MAX_WINDOWS) return false;
    const ui_window_t *w = &g_windows[g_focus_win];
    return w->used && w->open && !w->minimized && w->owner_pid == pid;
}

 



























void ui_flip_userspace_windows(void) {
    for (int i = 0; i < (int)UI_MAX_WINDOWS; i++) {
        ui_window_t *win = &g_windows[i];
        if (!win->used || !win->open || win->minimized) continue;
        if (win->owner_pid < 0) continue;  
        fb_flip_rect(win->x, win->y, win->w, win->h);
    }
}

static void ui_win_draw(int id);
static void ui_win_save_bg(int id, int32_t wx, int32_t wy);
static void ui_win_restore_bg(int id, int32_t wx, int32_t wy);

 



static void ui_win_btn_rects(const ui_window_t *win,
                             int32_t *close_x, int32_t *close_y,
                             int32_t *expand_x, int32_t *expand_y,
                             int32_t *min_x, int32_t *min_y) {
    int32_t wx = win->x, wy = win->y;
    *close_x  = wx + (int32_t)win->w - (int32_t)UI_WIN_CLOSE_SZ - 4;
    *close_y  = wy + (int32_t)((UI_WIN_TITLEBAR_H - UI_WIN_CLOSE_SZ) / 2u);

    *expand_x = *close_x - (int32_t)UI_WIN_BTN_GAP - (int32_t)UI_WIN_EXPAND_SZ;
    *expand_y = wy + (int32_t)((UI_WIN_TITLEBAR_H - UI_WIN_EXPAND_SZ) / 2u);

    *min_x    = *expand_x - (int32_t)UI_WIN_BTN_GAP - (int32_t)UI_WIN_MIN_SZ;
    *min_y    = wy + (int32_t)((UI_WIN_TITLEBAR_H - UI_WIN_MIN_SZ) / 2u);
}
static void ui_taskbar_draw_app_buttons(int32_t panel_y);
static void ui_taskbar_app_btn_rect(int slot_idx, int32_t panel_y,
                                    int32_t *bx, int32_t *by, uint32_t *bw, uint32_t *bh);

 







#include "VFS.h"

 

extern int elf64_spawn(const char *path, const char *name, int prio);

 







static inline uint64_t fm_io_cli(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}
static inline void fm_io_sti(uint64_t flags) {
    __asm__ volatile ("push %0; popfq" :: "r"(flags) : "memory", "cc");
}

#ifndef UI_O_RDONLY
#  define UI_O_RDONLY O_RDONLY
#endif

 






#define WALLPAPER_PNG_MAXSZ (16u * 1024u * 1024u)    

 




volatile int32_t fexox_os_dbg_wp_reason  = 0;
volatile int32_t fexox_os_dbg_wp_rc      = 0;
volatile int32_t fexox_os_dbg_spawn_rc   = 0;

static bool ui_load_wallpaper(uint32_t taskbar_y) {
    if (g_fb.mode != FB_MODE_LINEAR) return false;

     

    char path_a[96] = "UI/";
    char path_b[96] = "/UI/";
    strncat(path_a, g_themes[g_theme_idx].wallpaper_file, sizeof(path_a) - 4);
    strncat(path_b, g_themes[g_theme_idx].wallpaper_file, sizeof(path_b) - 5);

    fexox_os_dbg_wp_reason = 0; fexox_os_dbg_wp_rc = 0;
    int fd = vfs_open(path_a, UI_O_RDONLY);
    if (fd < 0) fd = vfs_open(path_b, UI_O_RDONLY);
    if (fd < 0) {
        DBG_MSG("UI", "wallpaper PNG not found");
        fexox_os_dbg_wp_reason = 1; fexox_os_dbg_wp_rc = fd;
        return false;
    }

     
    uint8_t *png_buf = (uint8_t *)kmalloc(WALLPAPER_PNG_MAXSZ);
    if (!png_buf) { vfs_close(fd); fexox_os_dbg_wp_reason = 2; return false; }

    size_t total = 0;
    for (;;) {
        int64_t got = vfs_read(fd, png_buf + total,
                               (uint64_t)(WALLPAPER_PNG_MAXSZ - total));
        if (got <= 0) break;
        total += (size_t)got;
        if (total >= WALLPAPER_PNG_MAXSZ) break;
    }
    vfs_close(fd);

    if (total < 8) {
        DBG_MSG("UI", "wallpaper: file too small");
        kfree(png_buf);
        fexox_os_dbg_wp_reason = 3; fexox_os_dbg_wp_rc = (int32_t)total;
        return false;
    }

     
    png_image_t img;
    int rc = png_decode(png_buf, total, &img);
    kfree(png_buf);

    if (rc != PNG_OK) {
        DBG_VAL("UI", "wallpaper PNG decode failed rc", (uint64_t)(int64_t)rc);
        fexox_os_dbg_wp_reason = 4; fexox_os_dbg_wp_rc = rc;
        return false;
    }

    DBG_VAL("UI", "wallpaper w", img.width);
    DBG_VAL("UI", "wallpaper h", img.height);

     







    uint32_t dst_w = g_fb.width;
    uint32_t dst_h = taskbar_y;    

    for (uint32_t dy = 0; dy < dst_h; dy++) {
         
        uint32_t sy = (uint32_t)((uint64_t)dy * img.height / dst_h);
        if (sy >= img.height) sy = img.height - 1;

        uint8_t *dst_row = draw_buf() + (uint64_t)dy * g_fb.pitch;
        const uint8_t *src_row = img.pixels + (uint64_t)sy * img.width * 4u;

        if (g_fb.bytes_pp == 4) {
            uint32_t *dst32 = (uint32_t *)dst_row;
            for (uint32_t dx = 0; dx < dst_w; dx++) {
                uint32_t sx = (uint32_t)((uint64_t)dx * img.width / dst_w);
                if (sx >= img.width) sx = img.width - 1;
                const uint8_t *px = src_row + sx * 4u;
                dst32[dx] = rgb_to_native(fb_rgb(px[0], px[1], px[2]));
            }
        } else {
            for (uint32_t dx = 0; dx < dst_w; dx++) {
                uint32_t sx = (uint32_t)((uint64_t)dx * img.width / dst_w);
                if (sx >= img.width) sx = img.width - 1;
                const uint8_t *px = src_row + sx * 4u;
                uint32_t native = rgb_to_native(fb_rgb(px[0], px[1], px[2]));
                uint8_t *p = dst_row + (uint64_t)dx * g_fb.bytes_pp;
                switch (g_fb.bytes_pp) {
                    case 3: p[2] = (uint8_t)(native >> 16);  
                    case 2: p[1] = (uint8_t)(native >>  8);  
                    case 1: p[0] = (uint8_t)(native      ); break;
                }
            }
        }
    }

    kfree(img.pixels);
    return (dst_h > 0);
}

 


static void ui_draw_taskbar_clock(int32_t panel_y) {
    int hh, mm, ss;
    rtc_get_msk(&hh, &mm, &ss);

    int32_t cw = 8 * FB_FONT_W;
    int32_t cx = (int32_t)g_fb.width - cw - 8;
    int32_t cy = panel_y + (int32_t)((TASKBAR_H - FB_FONT_H) / 2);

    fb_draw_digit2(cx,               cy, hh, CLOCK_FG, CLOCK_BG);
    fb_draw_char  (cx + 16,          cy, ':', CLOCK_FG, CLOCK_BG);
    fb_draw_digit2(cx + 16 + 8,      cy, mm, CLOCK_FG, CLOCK_BG);
    fb_draw_char  (cx + 16 + 8 + 16, cy, ':', CLOCK_FG, CLOCK_BG);
    fb_draw_digit2(cx + 16 + 8 + 24, cy, ss, CLOCK_FG, CLOCK_BG);
}

 




void fb_flip_clock_area(void) {
    uint32_t panel_y = (g_fb.height >= TASKBAR_H) ? g_fb.height - TASKBAR_H : 0;
    fb_flip_rect(0, (int32_t)panel_y, g_fb.width, TASKBAR_H);
}

void ui_redraw_clock(void) {
    if (g_fb.mode != FB_MODE_LINEAR) return;
    if (!g_fb.width || !g_fb.height) return;

    uint32_t panel_y = (g_fb.height >= TASKBAR_H) ? g_fb.height - TASKBAR_H : 0;
    int32_t cw = 8 * FB_FONT_W;
    int32_t cx = (int32_t)g_fb.width - cw - 8;

    fb_fill_rect(cx, (int32_t)panel_y, (uint32_t)cw + 8, TASKBAR_H, CLOCK_BG);
    ui_draw_taskbar_clock((int32_t)panel_y);
    fb_flip();
}

 










int ui_clock_tick(void) {
    if (g_fb.mode != FB_MODE_LINEAR) return 0;
    if (!g_fb.width || !g_fb.height) return 0;

    static int last_h = -1, last_m = -1, last_s = -1;
    int hh, mm, ss;
    rtc_get_msk(&hh, &mm, &ss);

    if (hh == last_h && mm == last_m && ss == last_s) return 0;
    last_h = hh; last_m = mm; last_s = ss;

    uint32_t panel_y = (g_fb.height >= TASKBAR_H) ? g_fb.height - TASKBAR_H : 0;
    int32_t cw = 8 * FB_FONT_W;
    int32_t cx = (int32_t)g_fb.width - cw - 8;

    fb_fill_rect(cx, (int32_t)panel_y, (uint32_t)cw + 8, TASKBAR_H, CLOCK_BG);
    ui_draw_taskbar_clock((int32_t)panel_y);
     

    return 1;
}

 

static uint32_t isqrt32(uint32_t n) {
    if (n == 0) return 0;
    uint32_t x = n, y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + n / x) / 2; }
    return x;
}

 





static void fb_round_right_corners(int32_t bx, int32_t by, uint32_t bw, uint32_t bh,
                                   int32_t r, fb_color_t bg_under) {
    if (r <= 0) return;
    int32_t rx = bx + (int32_t)bw - r;    

    for (int32_t row = 0; row < r; row++) {
         
        int32_t dy = r - 1 - row;
        int32_t cut = r - (int32_t)isqrt32((uint32_t)(r * r - dy * dy));
        if (cut < 0) cut = 0;
        if (cut > r) cut = r;

         
        fb_hline(rx + (r - cut), by + row, (uint32_t)cut, bg_under);
         
        fb_hline(rx + (r - cut), by + (int32_t)bh - 1 - row, (uint32_t)cut, bg_under);
    }
}

 


static void ui_draw_menu_button(int32_t panel_y) {
    int32_t bx = MENU_BTN_X;
    int32_t by = panel_y;    
    uint32_t bw = MENU_BTN_W;
    uint32_t bh = MENU_BTN_H;

    fb_fill_rect(bx, by, bw, bh, MENU_BTN_BG);

     

    fb_round_right_corners(bx, by, bw, bh, MENU_BTN_RADIUS, TASKBAR_BG);

    int32_t tx = bx + (int32_t)(bw / 2u) - (int32_t)(4u * FB_FONT_W / 2u);
    int32_t ty = by + (int32_t)((bh - FB_FONT_H) / 2u);
    fb_draw_string(tx, ty, "Menu", MENU_BTN_FG, MENU_BTN_BG);
}

 


static void menu_popup_rect(int32_t panel_y, int32_t *mx, int32_t *my) {
    *mx = MENU_BTN_X;
    *my = panel_y - (int32_t)MENU_POPUP_H - MENU_POPUP_GAP;
    if (*my < 0) *my = 0;
}

static void menu_item_rect(int idx, int32_t mx, int32_t my,
                            int32_t *ix, int32_t *iy, uint32_t *iw, uint32_t *ih) {
    *ix = mx + 10;
    *iy = my + 10 + idx * (int32_t)(MENU_ITEM_H + MENU_ITEM_GAP);
    *iw = MENU_ITEM_W;
    *ih = MENU_ITEM_H;
}

static bool point_in_rect(int32_t px, int32_t py,
                           int32_t x, int32_t y, uint32_t w, uint32_t h) {
    return px >= x && py >= y && px < x + (int32_t)w && py < y + (int32_t)h;
}

static void menu_save_bg(int32_t mx, int32_t my) {
    if (!g_menu_saved_bg) {
        g_menu_saved_bg = (fb_color_t *)kmalloc(
            (uint64_t)MENU_POPUP_W * MENU_POPUP_H * sizeof(fb_color_t));
    }
    if (!g_menu_saved_bg) return;
    for (uint32_t row = 0; row < MENU_POPUP_H; row++)
        for (uint32_t col = 0; col < MENU_POPUP_W; col++)
            g_menu_saved_bg[row * MENU_POPUP_W + col] =
                fb_get_pixel(mx + (int32_t)col, my + (int32_t)row);
}

static void menu_restore_bg(int32_t mx, int32_t my) {
    if (!g_menu_saved_bg) return;
    for (uint32_t row = 0; row < MENU_POPUP_H; row++)
        for (uint32_t col = 0; col < MENU_POPUP_W; col++)
            fb_put_pixel(mx + (int32_t)col, my + (int32_t)row,
                        g_menu_saved_bg[row * MENU_POPUP_W + col]);
    fb_flip();
}

static void ui_draw_start_menu(int32_t panel_y) {
    int32_t mx, my;
    menu_popup_rect(panel_y, &mx, &my);

     
    fb_fill_rect(mx, my, MENU_POPUP_W, MENU_POPUP_H, MENU_POPUP_BG);
    fb_draw_rect(mx, my, MENU_POPUP_W, MENU_POPUP_H, MENU_POPUP_BORDER, 1);

    static const char *labels[2] = { "Shutdown", "Reboot" };

    for (int i = 0; i < 2; i++) {
        int32_t ix, iy; uint32_t iw, ih;
        menu_item_rect(i, mx, my, &ix, &iy, &iw, &ih);

        fb_color_t bg = (g_menu_hover == i) ? MENU_ITEM_BG_HOVER : MENU_ITEM_BG;
        fb_fill_rect(ix, iy, iw, ih, bg);
        fb_draw_rect(ix, iy, iw, ih, MENU_POPUP_BORDER, 1);

        int32_t tw = (int32_t)fb_text_width(labels[i]);
        int32_t tx = ix + (int32_t)((iw - (uint32_t)tw) / 2u);
        int32_t ty = iy + (int32_t)((ih - FB_FONT_H) / 2u);
        fb_draw_string(tx, ty, labels[i], MENU_ITEM_FG, bg);
    }

    fb_flip();
}

 




static inline uint8_t fxapp_blend_chan(uint8_t src, uint8_t dst, uint8_t a) {
    return (uint8_t)(((uint32_t)src * a + (uint32_t)dst * (255 - a)) / 255);
}

 
static void icon_rect(int32_t *ix, int32_t *iy) {
    *ix = (int32_t)g_fb.width - FXAPP_ICON_MARGIN - (int32_t)FXAPP_ICON_W;
    *iy = FXAPP_ICON_MARGIN;
}

 


static void calc_icon_rect(int32_t *ix, int32_t *iy) {
    int32_t mx, my;
    icon_rect(&mx, &my);
    *ix = mx;
    *iy = my + (int32_t)FXAPP_ICON_H + 40;
}

 

static void syslogs_icon_rect(int32_t *ix, int32_t *iy) {
    int32_t cx, cy;
    calc_icon_rect(&cx, &cy);
    *ix = cx;
    *iy = cy + (int32_t)FXAPP_ICON_H + 40;
}

 
static void browser_icon_rect(int32_t *ix, int32_t *iy) {
    int32_t sx, sy;
    syslogs_icon_rect(&sx, &sy);
    *ix = sx;
    *iy = sy + (int32_t)FXAPP_ICON_H + 40;
}

 




static bool ui_point_over_any_desktop_icon(int32_t x, int32_t y) {
    int32_t icx, icy;

    icon_rect(&icx, &icy);
    if (point_in_rect(x, y,
                       icx - FXAPP_ICON_HALO_PAD, icy - FXAPP_ICON_HALO_PAD,
                       FXAPP_ICON_W + 2u * FXAPP_ICON_HALO_PAD,
                       FXAPP_ICON_H + 2u * FXAPP_ICON_HALO_PAD))
        return true;

    calc_icon_rect(&icx, &icy);
    if (point_in_rect(x, y,
                       icx - FXAPP_ICON_HALO_PAD, icy - FXAPP_ICON_HALO_PAD,
                       FXAPP_ICON_W + 2u * FXAPP_ICON_HALO_PAD,
                       FXAPP_ICON_H + 2u * FXAPP_ICON_HALO_PAD))
        return true;

    syslogs_icon_rect(&icx, &icy);
    if (point_in_rect(x, y,
                       icx - FXAPP_ICON_HALO_PAD, icy - FXAPP_ICON_HALO_PAD,
                       FXAPP_ICON_W + 2u * FXAPP_ICON_HALO_PAD,
                       FXAPP_ICON_H + 2u * FXAPP_ICON_HALO_PAD))
        return true;

    browser_icon_rect(&icx, &icy);
    if (point_in_rect(x, y,
                       icx - FXAPP_ICON_HALO_PAD, icy - FXAPP_ICON_HALO_PAD,
                       FXAPP_ICON_W + 2u * FXAPP_ICON_HALO_PAD,
                       FXAPP_ICON_H + 2u * FXAPP_ICON_HALO_PAD))
        return true;

    return false;
}

 

static void fxapp_blit_png(int32_t x, int32_t y, const png_image_t *img) {
    uint32_t w = img->width, h = img->height;
    for (uint32_t row = 0; row < h; row++) {
        for (uint32_t col = 0; col < w; col++) {
            const uint8_t *px = img->pixels + (((size_t)row * w + col) * 4);
            uint8_t r = px[0], g = px[1], b = px[2], a = px[3];
            if (a == 0) continue;

            int32_t sx = x + (int32_t)col;
            int32_t sy = y + (int32_t)row;

            if (a == 255) {
                fb_put_pixel(sx, sy, fb_rgb(r, g, b));
            } else {
                fb_color_t bg = fb_get_pixel(sx, sy);
                uint8_t br  = (uint8_t)((bg >> 16) & 0xFF);
                uint8_t bgc = (uint8_t)((bg >> 8)  & 0xFF);
                uint8_t bb  = (uint8_t)(bg & 0xFF);
                fb_put_pixel(sx, sy, fb_rgb(fxapp_blend_chan(r, br, a),
                                            fxapp_blend_chan(g, bgc, a),
                                            fxapp_blend_chan(b, bb, a)));
            }
        }
    }
}

 

static bool fxapp_load_png_asset(const char *rel_name, uint32_t max_w, uint32_t max_h,
                                  png_image_t *out) {
    char path_a[FXAPP_PATH_MAX] = "UI/";
    char path_b[FXAPP_PATH_MAX] = "/UI/";
    strncat(path_a, rel_name, sizeof(path_a) - 4);
    strncat(path_b, rel_name, sizeof(path_b) - 5);

    int fd = vfs_open(path_a, UI_O_RDONLY);
    if (fd < 0) fd = vfs_open(path_b, UI_O_RDONLY);
    if (fd < 0) {
        dbg_puts("[FXA] icon asset not found: "); dbg_puts(rel_name); dbg_puts("\n");
        return false;
    }

    uint8_t *buf = (uint8_t *)kmalloc(FXAPP_ICON_FILE_MAXSZ);
    if (!buf) { vfs_close(fd); return false; }

    size_t total = 0;
    for (;;) {
        int64_t got = vfs_read(fd, buf + total, (uint64_t)(FXAPP_ICON_FILE_MAXSZ - total));
        if (got <= 0) break;
        total += (size_t)got;
        if (total >= FXAPP_ICON_FILE_MAXSZ) break;
    }
    vfs_close(fd);

    if (total < 8 || buf[0] != 0x89 || buf[1] != 'P' || buf[2] != 'N' || buf[3] != 'G') {
        dbg_puts("[FXA] icon asset: not a PNG: "); dbg_puts(rel_name); dbg_puts("\n");
        kfree(buf);
        return false;
    }

    int rc = png_decode(buf, total, out);
    kfree(buf);

    if (rc != PNG_OK) {
        DBG_VAL("FXA", "icon asset decode failed, rc", (uint64_t)(int64_t)rc);
        return false;
    }
    if (out->width > max_w || out->height > max_h) {
        DBG_MSG("FXA", "icon asset too large, using fallback");
        kfree(out->pixels);
        out->pixels = NULL;
        return false;
    }
    return true;
}

 




static void fxapp_ensure_assets_loaded(void) {
    static bool tried = false;
    if (tried) return;
    tried = true;

    fxapp_t app;
    if (fxapp_load("APPS/MyComputer.fxapp", &app) == 0 ||
        fxapp_load("/APPS/MyComputer.fxapp", &app) == 0) {
        if (app.header.name[0] != '\0')
            strlcpy(g_fxapp_title, app.header.name, sizeof(g_fxapp_title));
        if (app.header.icon[0] != '\0')
            strlcpy(g_fxapp_icon_rel, app.header.icon, sizeof(g_fxapp_icon_rel));
        DBG_MSG("FXA", "MyComputer.fxapp manifest loaded");
    } else {
        DBG_MSG("FXA", "MyComputer.fxapp manifest not found, using defaults");
    }

    g_fxapp_icon_img.pixels = NULL;
    if (fxapp_load_png_asset(g_fxapp_icon_rel, FXAPP_ICON_W, FXAPP_ICON_H, &g_fxapp_icon_img))
        g_fxapp_icon_loaded = 1;

    g_fxapp_close_img.pixels = NULL;
    if (fxapp_load_png_asset("close.png", FXAPP_WIN_CLOSE_SZ, FXAPP_WIN_CLOSE_SZ, &g_fxapp_close_img))
        g_fxapp_close_loaded = 1;

    g_fxapp_expand_img.pixels = NULL;
    if (fxapp_load_png_asset("expand.png", UI_WIN_EXPAND_SZ, UI_WIN_EXPAND_SZ, &g_fxapp_expand_img))
        g_fxapp_expand_loaded = 1;

    g_fxapp_minimize_img.pixels = NULL;
    if (fxapp_load_png_asset("minimize.png", UI_WIN_MIN_SZ, UI_WIN_MIN_SZ, &g_fxapp_minimize_img))
        g_fxapp_minimize_loaded = 1;

     



    fxapp_t capp;
    if (fxapp_load("APPS/Calculator.fxapp", &capp) == 0 ||
        fxapp_load("/APPS/Calculator.fxapp", &capp) == 0) {
        if (capp.header.name[0] != '\0')
            strlcpy(g_calc_title, capp.header.name, sizeof(g_calc_title));
        if (capp.header.icon[0] != '\0')
            strlcpy(g_calc_icon_rel, capp.header.icon, sizeof(g_calc_icon_rel));
        if (capp.header.elf_path[0] != '\0')
            strlcpy(g_calc_elf_path, capp.header.elf_path, sizeof(g_calc_elf_path));
        DBG_MSG("FXA", "Calculator.fxapp manifest loaded");
    } else {
        DBG_MSG("FXA", "Calculator.fxapp manifest not found, using defaults");
    }

    g_calc_icon_img.pixels = NULL;
    if (fxapp_load_png_asset(g_calc_icon_rel, FXAPP_ICON_W, FXAPP_ICON_H, &g_calc_icon_img))
        g_calc_icon_loaded = 1;

     


    fxapp_t lapp;
    if (fxapp_load("APPS/SysLogs.fxapp", &lapp) == 0 ||
        fxapp_load("/APPS/SysLogs.fxapp", &lapp) == 0) {
        if (lapp.header.name[0] != '\0')
            strlcpy(g_syslogs_title, lapp.header.name, sizeof(g_syslogs_title));
        if (lapp.header.icon[0] != '\0')
            strlcpy(g_syslogs_icon_rel, lapp.header.icon, sizeof(g_syslogs_icon_rel));
        if (lapp.header.elf_path[0] != '\0')
            strlcpy(g_syslogs_elf_path, lapp.header.elf_path, sizeof(g_syslogs_elf_path));
        DBG_MSG("FXA", "SysLogs.fxapp manifest loaded");
    } else {
        DBG_MSG("FXA", "SysLogs.fxapp manifest not found, using defaults");
    }

    g_syslogs_icon_img.pixels = NULL;
    if (fxapp_load_png_asset(g_syslogs_icon_rel, FXAPP_ICON_W, FXAPP_ICON_H, &g_syslogs_icon_img))
        g_syslogs_icon_loaded = 1;

     


    fxapp_t bapp;
    if (fxapp_load("APPS/Browser.fxapp", &bapp) == 0 ||
        fxapp_load("/APPS/Browser.fxapp", &bapp) == 0) {
        if (bapp.header.name[0] != '\0')
            strlcpy(g_browser_title, bapp.header.name, sizeof(g_browser_title));
        if (bapp.header.icon[0] != '\0')
            strlcpy(g_browser_icon_rel, bapp.header.icon, sizeof(g_browser_icon_rel));
        if (bapp.header.elf_path[0] != '\0')
            strlcpy(g_browser_elf_path, bapp.header.elf_path, sizeof(g_browser_elf_path));
        DBG_MSG("FXA", "Browser.fxapp manifest loaded");
    } else {
        DBG_MSG("FXA", "Browser.fxapp manifest not found, using defaults");
    }

    g_browser_icon_img.pixels = NULL;
    if (fxapp_load_png_asset(g_browser_icon_rel, FXAPP_ICON_W, FXAPP_ICON_H, &g_browser_icon_img))
        g_browser_icon_loaded = 1;
}

 





static void fxapp_capture_icon_bg_generic(int32_t icx, int32_t icy, fb_color_t **bg_slot) {
    int32_t  hx = icx - FXAPP_ICON_HALO_PAD;
    int32_t  hy = icy - FXAPP_ICON_HALO_PAD;
    uint32_t hw = FXAPP_ICON_W + 2u * FXAPP_ICON_HALO_PAD;
    uint32_t hh = FXAPP_ICON_H + 2u * FXAPP_ICON_HALO_PAD;

    if (!*bg_slot) {
        *bg_slot = (fb_color_t *)kmalloc((uint64_t)hw * hh * sizeof(fb_color_t));
    }
    if (!*bg_slot) return;

    for (uint32_t row = 0; row < hh; row++)
        for (uint32_t col = 0; col < hw; col++)
            (*bg_slot)[row * hw + col] = fb_get_pixel(hx + (int32_t)col, hy + (int32_t)row);
}

 




static void fxapp_draw_icon_generic(int32_t icx, int32_t icy, fb_color_t *clean_bg,
                                    bool hovered, bool loaded, const png_image_t *img,
                                    const char *fallback_text, const char *label) {
    int32_t  hx = icx - FXAPP_ICON_HALO_PAD;
    int32_t  hy = icy - FXAPP_ICON_HALO_PAD;
    uint32_t hw = FXAPP_ICON_W + 2u * FXAPP_ICON_HALO_PAD;
    uint32_t hh = FXAPP_ICON_H + 2u * FXAPP_ICON_HALO_PAD;

    if (clean_bg) {
        for (uint32_t row = 0; row < hh; row++)
            for (uint32_t col = 0; col < hw; col++)
                fb_put_pixel(hx + (int32_t)col, hy + (int32_t)row, clean_bg[row * hw + col]);
    }

    if (hovered) {
        uint8_t hr = (uint8_t)((FXAPP_ICON_HALO_COLOR >> 16) & 0xFF);
        uint8_t hg = (uint8_t)((FXAPP_ICON_HALO_COLOR >> 8)  & 0xFF);
        uint8_t hb = (uint8_t)(FXAPP_ICON_HALO_COLOR & 0xFF);

        for (uint32_t row = 0; row < hh; row++) {
            for (uint32_t col = 0; col < hw; col++) {
                int32_t px = hx + (int32_t)col;
                int32_t py = hy + (int32_t)row;
                fb_color_t bg = fb_get_pixel(px, py);
                uint8_t br  = (uint8_t)((bg >> 16) & 0xFF);
                uint8_t bgc = (uint8_t)((bg >> 8)  & 0xFF);
                uint8_t bb  = (uint8_t)(bg & 0xFF);
                fb_put_pixel(px, py, fb_rgb(
                    fxapp_blend_chan(hr, br, (uint8_t)FXAPP_ICON_HALO_ALPHA),
                    fxapp_blend_chan(hg, bgc, (uint8_t)FXAPP_ICON_HALO_ALPHA),
                    fxapp_blend_chan(hb, bb, (uint8_t)FXAPP_ICON_HALO_ALPHA)));
            }
        }
    }

    if (loaded && img && img->pixels) {
        fxapp_blit_png(icx, icy, img);
    } else {
         
        fb_fill_rect(icx, icy, FXAPP_ICON_W, FXAPP_ICON_H, FB_LIGHT_GRAY);
        fb_draw_rect(icx, icy, FXAPP_ICON_W, FXAPP_ICON_H, FB_DARK_GRAY, 2);
        int32_t tw = (int32_t)fb_text_width(fallback_text);
        fb_draw_string(icx + (int32_t)((FXAPP_ICON_W - (uint32_t)(tw > 0 ? tw : 0)) / 2),
                       icy + (int32_t)(FXAPP_ICON_H / 2u) - 8,
                       fallback_text, FB_BLACK, FB_LIGHT_GRAY);
    }

     

    {
        int32_t lw = (int32_t)fb_text_width(label);
        int32_t lx = icx + (int32_t)((FXAPP_ICON_W - (uint32_t)(lw > 0 ? lw : 0)) / 2);
        int32_t ly = icy + (int32_t)FXAPP_ICON_H + FXAPP_ICON_HALO_PAD + 4;
        fb_draw_string(lx, ly, label, FB_WHITE, FB_TRANSPARENT_BG);
    }
}

 
static void fxapp_capture_icon_bg(void) {
    int32_t icx, icy;
    icon_rect(&icx, &icy);
    fxapp_capture_icon_bg_generic(icx, icy, &g_fxapp_icon_clean_bg);
}

static void fxapp_draw_icon(bool hovered) {
    int32_t icx, icy;
    icon_rect(&icx, &icy);
    fxapp_draw_icon_generic(icx, icy, g_fxapp_icon_clean_bg, hovered,
                            g_fxapp_icon_loaded, &g_fxapp_icon_img, "PC", g_fxapp_title);
}

 
static void calc_capture_icon_bg(void) {
    int32_t icx, icy;
    calc_icon_rect(&icx, &icy);
    fxapp_capture_icon_bg_generic(icx, icy, &g_calc_icon_clean_bg);
}

static void calc_draw_icon(bool hovered) {
    int32_t icx, icy;
    calc_icon_rect(&icx, &icy);
    fxapp_draw_icon_generic(icx, icy, g_calc_icon_clean_bg, hovered,
                            g_calc_icon_loaded, &g_calc_icon_img, "+-", g_calc_title);
}

 
static void syslogs_capture_icon_bg(void) {
    int32_t icx, icy;
    syslogs_icon_rect(&icx, &icy);
    fxapp_capture_icon_bg_generic(icx, icy, &g_syslogs_icon_clean_bg);
}

static void syslogs_draw_icon(bool hovered) {
    int32_t icx, icy;
    syslogs_icon_rect(&icx, &icy);
    fxapp_draw_icon_generic(icx, icy, g_syslogs_icon_clean_bg, hovered,
                            g_syslogs_icon_loaded, &g_syslogs_icon_img, "LG", g_syslogs_title);
}

 
static void browser_capture_icon_bg(void) {
    int32_t icx, icy;
    browser_icon_rect(&icx, &icy);
    fxapp_capture_icon_bg_generic(icx, icy, &g_browser_icon_clean_bg);
}

static void browser_draw_icon(bool hovered) {
    int32_t icx, icy;
    browser_icon_rect(&icx, &icy);
     



    fxapp_draw_icon_generic(icx, icy, g_browser_icon_clean_bg, hovered,
                            g_browser_icon_loaded, &g_browser_icon_img, "WWW", "");
}

 









#define FM_ICON_W        64u
#define FM_ICON_H        64u
#define FM_CELL_W          96    
#define FM_CELL_H          96    
#define FM_GRID_PAD          8   
#define FM_PATH_MAX        256
#define FM_MAX_ENTRIES      32   
#define FM_LABEL_MAX_CH     11   

typedef enum {
    FM_KIND_DRIVE = 0,    
    FM_KIND_DIR,          
    FM_KIND_FILE,         
    FM_KIND_UP,           
} fm_kind_t;

typedef struct {
    fm_kind_t kind;
    char      label[VFS_MAX_NAME + 1];    
    char      drive_path[FM_PATH_MAX];    
} fm_entry_t;

typedef enum { FM_VIEW_DRIVES, FM_VIEW_DIR } fm_view_t;

static fm_view_t   g_fm_view       = FM_VIEW_DRIVES;
static char        g_fm_cur_path[FM_PATH_MAX]   = "/";
static char        g_fm_drive_root[FM_PATH_MAX] = "/";   


static fm_entry_t  g_fm_entries[FM_MAX_ENTRIES];
static int         g_fm_entry_count = 0;

static png_image_t g_fm_icon_disk;    static int g_fm_icon_disk_loaded   = 0;
static png_image_t g_fm_icon_folder;  static int g_fm_icon_folder_loaded = 0;
static png_image_t g_fm_icon_file;    static int g_fm_icon_file_loaded   = 0;

 

static void fm_path_join(const char *base, const char *name, char *out, size_t out_sz) {
    strlcpy(out, base, out_sz);
    size_t n = strlen(out);
    if (n == 0 || out[n - 1] != '/') {
        if (n + 1 < out_sz) { out[n] = '/'; out[n + 1] = '\0'; }
    }
    size_t cur = strlen(out);
    if (cur < out_sz) strncat(out, name, out_sz - cur - 1);
}

 
static void fm_path_up(char *path) {
    size_t n = strlen(path);
    while (n > 1 && path[n - 1] == '/') path[--n] = '\0';
    char *slash = strrchr(path, '/');
    if (!slash) { path[0] = '/'; path[1] = '\0'; return; }
    if (slash == path) { path[1] = '\0'; return; }    
    *slash = '\0';
}

 










static void fm_ensure_assets_loaded(void) {
    static bool ok = false;
    if (ok) return;

    bool all_ok = true;

    if (!g_fm_icon_disk_loaded) {
        g_fm_icon_disk.pixels = NULL;
        if (fxapp_load_png_asset("disk.png", FM_ICON_W, FM_ICON_H, &g_fm_icon_disk))
            g_fm_icon_disk_loaded = 1;
        else
            all_ok = false;
    }

    if (!g_fm_icon_folder_loaded) {
        g_fm_icon_folder.pixels = NULL;
        if (fxapp_load_png_asset("folder.png", FM_ICON_W, FM_ICON_H, &g_fm_icon_folder))
            g_fm_icon_folder_loaded = 1;
        else
            all_ok = false;
    }

    if (!g_fm_icon_file_loaded) {
        g_fm_icon_file.pixels = NULL;
        if (fxapp_load_png_asset("file.png", FM_ICON_W, FM_ICON_H, &g_fm_icon_file))
            g_fm_icon_file_loaded = 1;
        else
            all_ok = false;
    }

     

    if (all_ok) ok = true;
    else DBG_MSG("FXA", "my computer: some icons (disk/folder/file.png) failed to load, will retry");
}

 
static void fm_reload_drives(void) {
    g_fm_entry_count = 0;

    int n = vfs_mount_count();
    for (int i = 0; i < n && g_fm_entry_count < FM_MAX_ENTRIES; i++) {
        char path[FM_PATH_MAX];
        char dev[32];
        if (!vfs_mount_info(i, path, sizeof(path), dev, sizeof(dev))) continue;

        fm_entry_t *e = &g_fm_entries[g_fm_entry_count++];
        e->kind = FM_KIND_DRIVE;
        strlcpy(e->drive_path, path, sizeof(e->drive_path));
         

        strlcpy(e->label, dev[0] ? dev : path, sizeof(e->label));
    }
    DBG_VAL("FXA", "my computer: vfs mounts", (uint64_t)n);
    DBG_VAL("FXA", "my computer: drives found", (uint64_t)g_fm_entry_count);
}

 

static void fm_reload_dir(const char *path) {
    g_fm_entry_count = 0;

    if (g_fm_entry_count < FM_MAX_ENTRIES) {
        fm_entry_t *up = &g_fm_entries[g_fm_entry_count++];
        up->kind = FM_KIND_UP;
        strlcpy(up->label, "..", sizeof(up->label));
    }

    int fd = vfs_open(path, O_DIRECTORY);
    if (fd < 0) {
        DBG_VAL("FXA", "my computer: vfs_open(dir) failed, rc", (uint64_t)(int64_t)fd);
        return;
    }

    for (uint32_t idx = 0; g_fm_entry_count < FM_MAX_ENTRIES; idx++) {
        vfs_dirent_t de;
        if (vfs_readdir(fd, idx, &de) != VFS_OK) break;

        fm_entry_t *e = &g_fm_entries[g_fm_entry_count++];
        e->kind = (de.type == VFS_TYPE_DIR) ? FM_KIND_DIR : FM_KIND_FILE;
        strlcpy(e->label, de.name, sizeof(e->label));
         
        dbg_puts("[FXA]   entry '"); dbg_puts(de.name);
        dbg_puts(de.type == VFS_TYPE_DIR ? "' DIR\n" : "' FILE\n");
    }
    vfs_close(fd);
    dbg_puts("[FXA] my computer: dir '"); dbg_puts(path); dbg_puts("'\n");
    DBG_VAL("FXA", "my computer: dir entries", (uint64_t)g_fm_entry_count);
}

 










static volatile bool g_fm_busy = false;

 

static void fm_reset(void) {
    if (g_fm_busy) {
        DBG_MSG("FXA", "my computer: reset ignored (reentrant, I/O in progress)");
        return;
    }
    g_fm_busy = true;
    uint64_t fm_flags = fm_io_cli();

    g_fm_view = FM_VIEW_DRIVES;
    strlcpy(g_fm_cur_path,   "/", sizeof(g_fm_cur_path));
    strlcpy(g_fm_drive_root, "/", sizeof(g_fm_drive_root));
    fm_ensure_assets_loaded();
    fm_reload_drives();

    fm_io_sti(fm_flags);
    g_fm_busy = false;
}

static int fm_cols(uint32_t content_w) {
    int c = (int)((content_w - 2u * FM_GRID_PAD) / FM_CELL_W);
    return (c > 0) ? c : 1;
}

 

static void fm_cell_origin(int idx, uint32_t content_w, int32_t *cx, int32_t *cy) {
    int cols = fm_cols(content_w);
    int col = idx % cols;
    int row = idx / cols;
    *cx = FM_GRID_PAD + col * FM_CELL_W;
    *cy = FM_GRID_PAD + row * FM_CELL_H;
}

static void fm_draw_entry_icon(fm_kind_t kind, int32_t x, int32_t y) {
    png_image_t *img = NULL;
    int loaded = 0;

    switch (kind) {
        case FM_KIND_DRIVE:
            img = &g_fm_icon_disk;   loaded = g_fm_icon_disk_loaded;   break;
        case FM_KIND_DIR:
        case FM_KIND_UP:
            img = &g_fm_icon_folder; loaded = g_fm_icon_folder_loaded; break;
        case FM_KIND_FILE:
        default:
            img = &g_fm_icon_file;   loaded = g_fm_icon_file_loaded;   break;
    }

    if (loaded && img->pixels) {
        fxapp_blit_png(x, y, img);
    } else {
         


        fb_color_t bg;
        const char *hint;
        switch (kind) {
            case FM_KIND_DRIVE: bg = 0xB0C4DEU; hint = "D"; break;   
            case FM_KIND_FILE:  bg = FB_LIGHT_GRAY; hint = "F"; break;
            case FM_KIND_UP:    bg = 0xE8D080U; hint = "^"; break;   
            case FM_KIND_DIR:
            default:            bg = 0xE8D080U; hint = "/"; break;  
        }
        fb_fill_rect(x, y, FM_ICON_W, FM_ICON_H, bg);
        fb_draw_rect(x, y, FM_ICON_W, FM_ICON_H, FB_DARK_GRAY, 1);
        fb_draw_string(x + (int32_t)(FM_ICON_W / 2u) - 4, y + (int32_t)(FM_ICON_H / 2u) - 8,
                       hint, FB_BLACK, bg);
    }
}

 

static void fm_label_fit(const char *name, char *out, size_t out_sz) {
    size_t n = strlen(name);
    if (n >= out_sz) n = out_sz - 1;
    for (size_t i = 0; i < n; i++) out[i] = name[i];
    out[n] = '\0';
    if (n >= FM_LABEL_MAX_CH && out_sz > FM_LABEL_MAX_CH) {
        out[FM_LABEL_MAX_CH - 1] = '.';
        out[FM_LABEL_MAX_CH] = '\0';
    }
}

 



static void fm_draw(int win_id) {
    ui_window_t *win = &g_windows[win_id];
    int32_t  base_x   = win->x;
    int32_t  base_y   = win->y + (int32_t)UI_WIN_TITLEBAR_H;
    uint32_t content_w = win->w;
    uint32_t content_h = win->h - UI_WIN_TITLEBAR_H;

    if (g_fm_entry_count == 0) {
        const char *msg = (g_fm_view == FM_VIEW_DRIVES) ? "No disks" : "Folder is empty";
        int32_t tw = (int32_t)fb_text_width(msg);
        int32_t tx = base_x + (int32_t)((content_w - (uint32_t)(tw > 0 ? tw : 0)) / 2u);
        int32_t ty = base_y + (int32_t)((content_h - FB_FONT_H) / 2u);
        fb_draw_string(tx, ty, msg, FB_GRAY, FB_WHITE);
        return;
    }

    for (int i = 0; i < g_fm_entry_count; i++) {
        int32_t ox, oy;
        fm_cell_origin(i, content_w, &ox, &oy);
        if ((uint32_t)(oy + FM_CELL_H) > content_h) break;   

        int32_t icon_x = base_x + ox + (int32_t)((FM_CELL_W - (int32_t)FM_ICON_W) / 2);
        int32_t icon_y = base_y + oy;
        fm_draw_entry_icon(g_fm_entries[i].kind, icon_x, icon_y);

        char label[FM_LABEL_MAX_CH + 1];
        fm_label_fit(g_fm_entries[i].label, label, sizeof(label));
        int32_t lw = (int32_t)fb_text_width(label);
        int32_t label_x = base_x + ox + (int32_t)((FM_CELL_W - (uint32_t)(lw > 0 ? lw : 0)) / 2);
        int32_t label_y = icon_y + (int32_t)FM_ICON_H + 4;
        fb_draw_string(label_x, label_y, label, FB_BLACK, FB_WHITE);
    }
}

 




static void fm_handle_click(int win_id, int32_t x, int32_t y) {
     







    if (g_fm_busy) {
        DBG_MSG("FXA", "my computer: click ignored (reentrant, I/O in progress)");
        return;
    }
    g_fm_busy = true;
    uint64_t fm_flags = fm_io_cli();

    ui_window_t *win = &g_windows[win_id];

    int32_t lx = x - win->x - FM_GRID_PAD;
    int32_t ly = y - win->y - (int32_t)UI_WIN_TITLEBAR_H - FM_GRID_PAD;
    bool handled = false;

    if (lx >= 0 && ly >= 0) {
        int cols = fm_cols(win->w);
        int col = lx / FM_CELL_W;
        int row = ly / FM_CELL_H;

        if (col >= 0 && col < cols) {
            int idx = row * cols + col;

            if (idx >= 0 && idx < g_fm_entry_count) {
                fm_entry_t *e = &g_fm_entries[idx];

                switch (e->kind) {
                    case FM_KIND_DRIVE:
                        g_fm_view = FM_VIEW_DIR;
                        strlcpy(g_fm_cur_path,   e->drive_path, sizeof(g_fm_cur_path));
                        strlcpy(g_fm_drive_root, e->drive_path, sizeof(g_fm_drive_root));
                        fm_reload_dir(g_fm_cur_path);
                        handled = true;
                        break;

                    case FM_KIND_UP:
                        if (strcmp(g_fm_cur_path, g_fm_drive_root) == 0) {
                             
                            g_fm_view = FM_VIEW_DRIVES;
                            fm_reload_drives();
                        } else {
                            fm_path_up(g_fm_cur_path);
                            fm_reload_dir(g_fm_cur_path);
                        }
                        handled = true;
                        break;

                    case FM_KIND_DIR: {
                        char child[FM_PATH_MAX];
                        fm_path_join(g_fm_cur_path, e->label, child, sizeof(child));
                        strlcpy(g_fm_cur_path, child, sizeof(g_fm_cur_path));
                        fm_reload_dir(g_fm_cur_path);
                        handled = true;
                        break;
                    }

                    case FM_KIND_FILE:
                    default:
                        break;    
                }
            }
        }
    }

    if (handled) {
        ui_win_draw(win_id);
        fm_draw(win_id);
        fb_flip();
    }

    fm_io_sti(fm_flags);
    g_fm_busy = false;
}

 



















#define FMDLG_WIN_W          300u
#define FMDLG_WIN_H          150u
#define FMDLG_NAME_MAX       VFS_MAX_NAME    

#define FMDLG_LABEL_X          16
#define FMDLG_LABEL_Y           8

#define FMDLG_INPUT_X          16
#define FMDLG_INPUT_Y          30
#define FMDLG_INPUT_H          28u
#define FMDLG_INPUT_BG      FB_WHITE
#define FMDLG_INPUT_BORDER  0x808080U
#define FMDLG_INPUT_FG      FB_BLACK

#define FMDLG_BTN_W             90u
#define FMDLG_BTN_H             32u
#define FMDLG_BTN_MARGIN        16
#define FMDLG_BTN_BG        0x2266CCU    
#define FMDLG_BTN_FG        FB_WHITE

typedef enum { FMDLG_NONE = 0, FMDLG_FOLDER, FMDLG_FILE } fmdlg_kind_t;

static int          g_fmdlg_win_id     = -1;
static fmdlg_kind_t g_fmdlg_kind       = FMDLG_NONE;
static char         g_fmdlg_target[FM_PATH_MAX] = "/";   
static int          g_fmdlg_target_win = -1;              
static char         g_fmdlg_text[VFS_MAX_NAME + 1] = "";
static int          g_fmdlg_text_len   = 0;

static void fmdlg_input_rect(int win_id, int32_t *ix, int32_t *iy, uint32_t *iw, uint32_t *ih) {
    ui_window_t *win = &g_windows[win_id];
    *ix = win->x + FMDLG_INPUT_X;
    *iy = win->y + (int32_t)UI_WIN_TITLEBAR_H + FMDLG_INPUT_Y;
    *iw = win->w - 2u * (uint32_t)FMDLG_INPUT_X;
    *ih = FMDLG_INPUT_H;
}

static void fmdlg_btn_rect(int win_id, int32_t *bx, int32_t *by, uint32_t *bw, uint32_t *bh) {
    ui_window_t *win = &g_windows[win_id];
    *bw = FMDLG_BTN_W;
    *bh = FMDLG_BTN_H;
    *bx = win->x + (int32_t)win->w - (int32_t)FMDLG_BTN_W - FMDLG_BTN_MARGIN;
    *by = win->y + (int32_t)win->h - (int32_t)FMDLG_BTN_H - FMDLG_BTN_MARGIN;
}

 

static void fmdlg_draw(int win_id) {
    if (win_id < 0 || win_id >= (int)UI_MAX_WINDOWS) return;
    ui_window_t *win = &g_windows[win_id];
    if (!win->used) return;

    int32_t  base_x    = win->x;
    int32_t  base_y    = win->y + (int32_t)UI_WIN_TITLEBAR_H;
    uint32_t content_h = (win->h > UI_WIN_TITLEBAR_H) ? win->h - UI_WIN_TITLEBAR_H : 0;
    fb_fill_rect(base_x, base_y, win->w, content_h, UI_WIN_BODY_BG);

    fb_draw_string(base_x + FMDLG_LABEL_X, base_y + FMDLG_LABEL_Y,
                   "Name:", FB_BLACK, UI_WIN_BODY_BG);

    int32_t ix, iy; uint32_t iw, ih;
    fmdlg_input_rect(win_id, &ix, &iy, &iw, &ih);
    fb_fill_rect(ix, iy, iw, ih, FMDLG_INPUT_BG);
    fb_draw_rect(ix, iy, iw, ih, FMDLG_INPUT_BORDER, 1);

     

    int32_t tx = ix + 6;
    int32_t ty = iy + (int32_t)((ih - FB_FONT_H) / 2u);
    int max_chars = (int)((iw > 12u ? iw - 12u : 0) / FB_FONT_W);
    if (max_chars < 1) max_chars = 1;
    const char *show = g_fmdlg_text;
    if (g_fmdlg_text_len > max_chars) show = g_fmdlg_text + (g_fmdlg_text_len - max_chars);
    fb_draw_string(tx, ty, show, FMDLG_INPUT_FG, FMDLG_INPUT_BG);

    int32_t bx, by; uint32_t bw, bh;
    fmdlg_btn_rect(win_id, &bx, &by, &bw, &bh);
    fb_fill_rect(bx, by, bw, bh, FMDLG_BTN_BG);
    const char *label = "Create";
    int32_t lw = (int32_t)fb_text_width(label);
    fb_draw_string(bx + (int32_t)((bw - (uint32_t)(lw > 0 ? lw : 0)) / 2u),
                   by + (int32_t)((bh - FB_FONT_H) / 2u), label, FMDLG_BTN_FG, FMDLG_BTN_BG);
}

 


static void fmdlg_open(fmdlg_kind_t kind, int target_win_id) {
    if (g_fmdlg_win_id >= 0 && g_windows[g_fmdlg_win_id].used) {
        ui_win_close(g_fmdlg_win_id);
        g_fmdlg_win_id = -1;
    }

    g_fmdlg_kind       = kind;
    g_fmdlg_target_win = target_win_id;
    strlcpy(g_fmdlg_target, g_fm_cur_path, sizeof(g_fmdlg_target));
    g_fmdlg_text[0]    = '\0';
    g_fmdlg_text_len   = 0;

    const char *title = (kind == FMDLG_FOLDER) ? "Create Folder" : "Create File";
    g_fmdlg_win_id = ui_win_create(title, FMDLG_WIN_W, FMDLG_WIN_H, -1);
    if (g_fmdlg_win_id >= 0) {
        fmdlg_draw(g_fmdlg_win_id);
        fb_flip();
    }
}

 


static void fmdlg_do_create(void) {
    if (g_fmdlg_win_id < 0 || g_fmdlg_kind == FMDLG_NONE) return;
    if (g_fmdlg_text_len == 0) return;    

    char path[FM_PATH_MAX];
    fm_path_join(g_fmdlg_target, g_fmdlg_text, path, sizeof(path));

    if (g_fmdlg_kind == FMDLG_FOLDER) {
         



        int mr = vfs_mkdir(path);
        if (mr < 0) {
            DBG_RC("FXA", "vfs_mkdir failed, rc", mr);
            return;    
        }
    } else {
        int fd = vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd < 0) {
            DBG_RC("FXA", "create file failed, rc", fd);
            return;
        }
        vfs_close(fd);
    }
    dbg_puts("[FXA] created '"); dbg_puts(path);
    dbg_puts(g_fmdlg_kind == FMDLG_FOLDER ? "' (folder)\n" : "' (file)\n");

    int target_win = g_fmdlg_target_win;
    int win_id     = g_fmdlg_win_id;
    g_fmdlg_win_id  = -1;
    g_fmdlg_kind    = FMDLG_NONE;
    ui_win_close(win_id);
    if (target_win >= 0 && g_windows[target_win].used && g_windows[target_win].open)
        ui_focus_set(target_win);

    if (target_win >= 0 && target_win == g_fxapp_win_id &&
        g_windows[target_win].used && g_windows[target_win].open &&
        g_fm_view == FM_VIEW_DIR && strcmp(g_fm_cur_path, g_fmdlg_target) == 0) {
        fm_reload_dir(g_fm_cur_path);
        ui_win_draw(target_win);
        fm_draw(target_win);
    }
    fb_flip();
}

 


static void fmdlg_handle_click(int win_id, int32_t x, int32_t y) {
    int32_t bx, by; uint32_t bw, bh;
    fmdlg_btn_rect(win_id, &bx, &by, &bw, &bh);
    if (point_in_rect(x, y, bx, by, bw, bh)) fmdlg_do_create();
}

 




static void ui_kbd_drain_unclaimed(void) {
    if (g_focus_win >= 0 && g_focus_win < (int)UI_MAX_WINDOWS) {
        const ui_window_t *w = &g_windows[g_focus_win];
        if (w->used && w->open && !w->minimized) {
            if (w->owner_pid > 0) return;
            if (g_focus_win == g_fmdlg_win_id) return;
        }
    }
    char ch;
    while (usb_hid_kbd_read_char(&ch)) { }
}

 



static void fmdlg_poll_keyboard(void) {
    if (g_fmdlg_win_id < 0 || !g_windows[g_fmdlg_win_id].used || !g_windows[g_fmdlg_win_id].open)
        return;
    if (g_focus_win != g_fmdlg_win_id) return;    

    char ch;
    bool changed = false;
    while (usb_hid_kbd_read_char(&ch)) {
        if (ch == '\n') {
            fmdlg_do_create();
            return;    
        } else if (ch == '\b') {
            if (g_fmdlg_text_len > 0) {
                g_fmdlg_text[--g_fmdlg_text_len] = '\0';
                changed = true;
            }
        } else if (ch == '\t') {
             
        } else if (g_fmdlg_text_len < (int)FMDLG_NAME_MAX) {
            g_fmdlg_text[g_fmdlg_text_len++] = ch;
            g_fmdlg_text[g_fmdlg_text_len] = '\0';
            changed = true;
        }
    }

    if (changed) {
        fmdlg_draw(g_fmdlg_win_id);
        fb_flip();
    }
}

 







#define FMCTX_W               170u
#define FMCTX_ITEM_H           28u
#define FMCTX_PAD               6
#define FMCTX_H        (2u * FMCTX_PAD + 2u * FMCTX_ITEM_H)    
#define FMCTX_BG            0x2B2B2BU
#define FMCTX_BORDER        0x000000U
#define FMCTX_ITEM_BG_HOVER 0x454545U
#define FMCTX_ITEM_FG       0xE8E8E8U

static bool        g_fmctx_open   = false;
static int         g_fmctx_hover  = -1;    
 

static int         g_fmctx_mode   = 0;
static int         g_fmctx_n      = 2;     
static char        g_fmctx_dir[FM_PATH_MAX];             
static char        g_fmctx_name[VFS_MAX_NAME + 1];       
static bool        g_fmctx_is_dir = false;
static int32_t     g_fmctx_x = 0, g_fmctx_y = 0;
static int         g_fmctx_win_id = -1;    
static fb_color_t *g_fmctx_saved_bg = NULL;

static uint32_t fmctx_h(void) {
    return 2u * (uint32_t)FMCTX_PAD + (uint32_t)g_fmctx_n * FMCTX_ITEM_H;
}

static void fmctx_item_rect(int idx, int32_t mx, int32_t my,
                            int32_t *ix, int32_t *iy, uint32_t *iw, uint32_t *ih) {
    *ix = mx + FMCTX_PAD;
    *iy = my + FMCTX_PAD + idx * (int32_t)FMCTX_ITEM_H;
    *iw = FMCTX_W - 2u * FMCTX_PAD;
    *ih = FMCTX_ITEM_H;
}

static void fmctx_save_bg(int32_t mx, int32_t my) {
    if (!g_fmctx_saved_bg) {
        g_fmctx_saved_bg = (fb_color_t *)kmalloc(
            (uint64_t)FMCTX_W * FMCTX_H * sizeof(fb_color_t));
    }
    if (!g_fmctx_saved_bg) return;
    for (uint32_t row = 0; row < fmctx_h(); row++)
        for (uint32_t col = 0; col < FMCTX_W; col++)
            g_fmctx_saved_bg[row * FMCTX_W + col] =
                fb_get_pixel(mx + (int32_t)col, my + (int32_t)row);
}

static void fmctx_restore_bg(int32_t mx, int32_t my) {
    if (!g_fmctx_saved_bg) return;
    for (uint32_t row = 0; row < fmctx_h(); row++)
        for (uint32_t col = 0; col < FMCTX_W; col++)
            fb_put_pixel(mx + (int32_t)col, my + (int32_t)row,
                        g_fmctx_saved_bg[row * FMCTX_W + col]);
    fb_flip();
}

static void fmctx_draw(void) {
    int32_t mx = g_fmctx_x, my = g_fmctx_y;
    fb_fill_rect(mx, my, FMCTX_W, fmctx_h(), FMCTX_BG);
    fb_draw_rect(mx, my, FMCTX_W, fmctx_h(), FMCTX_BORDER, 1);

    static const char *labels_create[2] = { "Create Folder", "Create File" };
    static const char *labels_entry[1]  = { "Delete" };
    const char **labels = (g_fmctx_mode == 1) ? labels_entry : labels_create;
    for (int i = 0; i < g_fmctx_n; i++) {
        int32_t ix, iy; uint32_t iw, ih;
        fmctx_item_rect(i, mx, my, &ix, &iy, &iw, &ih);
        fb_color_t bg = (g_fmctx_hover == i) ? FMCTX_ITEM_BG_HOVER : FMCTX_BG;
        fb_fill_rect(ix, iy, iw, ih, bg);
        int32_t tx = ix + 8;
        int32_t ty = iy + (int32_t)((ih - FB_FONT_H) / 2u);
        fb_draw_string(tx, ty, labels[i], FMCTX_ITEM_FG, bg);
    }
    fb_flip();
}

 


static int fm_hit_test(int win_id, int32_t x, int32_t y) {
    ui_window_t *win = &g_windows[win_id];
    int32_t lx = x - win->x - FM_GRID_PAD;
    int32_t ly = y - win->y - (int32_t)UI_WIN_TITLEBAR_H - FM_GRID_PAD;
    if (lx < 0 || ly < 0) return -1;

    int cols = fm_cols(win->w);
    int col = lx / FM_CELL_W;
    int row = ly / FM_CELL_H;
    if (col >= cols) return -1;

    int idx = row * cols + col;
    if (idx < 0 || idx >= g_fm_entry_count) return -1;

    int32_t ox, oy;
    fm_cell_origin(idx, win->w, &ox, &oy);
    if ((uint32_t)(oy + FM_CELL_H) > win->h - UI_WIN_TITLEBAR_H) return -1;
    return idx;
}

 









#define FM_DEL_MAX_DEPTH 8
static int fm_delete_path(const char *path, bool is_dir, int depth) {
    if (!is_dir) return vfs_unlink(path);
    if (depth >= FM_DEL_MAX_DEPTH) return VFS_ERR_BUSY;

    uint32_t skip = 0;
    for (uint32_t guard = 0; guard < 4096; guard++) {
        int fd = vfs_open(path, O_DIRECTORY);
        if (fd < 0) return fd;

        vfs_dirent_t de;
        int rr = vfs_readdir(fd, skip, &de);
        vfs_close(fd);
        if (rr != VFS_OK) break;

        char child[FM_PATH_MAX];
        fm_path_join(path, de.name, child, sizeof(child));
        int cr = fm_delete_path(child, de.type == VFS_TYPE_DIR, depth + 1);
        if (cr < 0) {
            DBG_RC("FXA", "delete child failed, rc", cr);
            skip++;
        }
    }
    return vfs_rmdir(path);
}

 

static void fmctx_do_delete(void) {
    int win_id = g_fmctx_win_id;
    if (win_id < 0 || win_id >= (int)UI_MAX_WINDOWS) return;
    if (!g_windows[win_id].used || !g_windows[win_id].open) return;
    if (g_fm_busy) {
        DBG_MSG("FXA", "my computer: delete ignored (reentrant, I/O in progress)");
        return;
    }
    g_fm_busy = true;
    uint64_t fm_flags = fm_io_cli();

    char path[FM_PATH_MAX];
    fm_path_join(g_fmctx_dir, g_fmctx_name, path, sizeof(path));

    int r = fm_delete_path(path, g_fmctx_is_dir, 0);
    if (r < 0) {
        DBG_RC("FXA", "delete failed, rc", r);
    } else {
        dbg_puts("[FXA] deleted '"); dbg_puts(path);
        dbg_puts(g_fmctx_is_dir ? "' (folder)\n" : "' (file)\n");
    }

     

    if (g_fm_view == FM_VIEW_DIR && strcmp(g_fm_cur_path, g_fmctx_dir) == 0)
        fm_reload_dir(g_fm_cur_path);

    ui_win_draw(win_id);
    fm_draw(win_id);
    fb_flip();

    fm_io_sti(fm_flags);
    g_fm_busy = false;
}

 







 







static void ui_win_save_bg(int id, int32_t wx, int32_t wy) {
    ui_window_t *win = &g_windows[id];
    if (g_fb.mode != FB_MODE_LINEAR) return;

    uint64_t row_bytes = (uint64_t)win->w * g_fb.bytes_pp;
    if (!win->saved_bg) {
        win->saved_bg = (uint8_t *)kmalloc(row_bytes * win->h);
    }
    if (!win->saved_bg) return;

    const uint8_t *buf = draw_buf();
    for (uint32_t row = 0; row < win->h; row++) {
        const uint8_t *src = buf + (uint64_t)(wy + (int32_t)row) * g_fb.pitch
                                  + (uint64_t)wx * g_fb.bytes_pp;
        memcpy(win->saved_bg + (uint64_t)row * row_bytes, src, row_bytes);
    }
}

static void ui_win_restore_bg(int id, int32_t wx, int32_t wy) {
    ui_window_t *win = &g_windows[id];
    if (g_fb.mode != FB_MODE_LINEAR) return;
    if (!win->saved_bg) return;

    uint64_t row_bytes = (uint64_t)win->w * g_fb.bytes_pp;
    uint8_t *buf = draw_buf();
    for (uint32_t row = 0; row < win->h; row++) {
        uint8_t *dst = buf + (uint64_t)(wy + (int32_t)row) * g_fb.pitch
                            + (uint64_t)wx * g_fb.bytes_pp;
        memcpy(dst, win->saved_bg + (uint64_t)row * row_bytes, row_bytes);
    }
}

 











static void ui_win_capture(int id, int32_t wx, int32_t wy) {
    ui_window_t *win = &g_windows[id];
    if (g_fb.mode != FB_MODE_LINEAR) return;

    uint64_t row_bytes = (uint64_t)win->w * g_fb.bytes_pp;
    if (!win->content_bak) {
        win->content_bak = (uint8_t *)kmalloc(row_bytes * win->h);
    }
    if (!win->content_bak) return;

    const uint8_t *buf = draw_buf();
    for (uint32_t row = 0; row < win->h; row++) {
        const uint8_t *src = buf + (uint64_t)(wy + (int32_t)row) * g_fb.pitch
                                  + (uint64_t)wx * g_fb.bytes_pp;
        memcpy(win->content_bak + (uint64_t)row * row_bytes, src, row_bytes);
    }
}

static void ui_win_blit_captured(int id, int32_t wx, int32_t wy) {
    ui_window_t *win = &g_windows[id];
    if (g_fb.mode != FB_MODE_LINEAR) return;
    if (!win->content_bak) { ui_win_draw(id); return; }  

    uint64_t row_bytes = (uint64_t)win->w * g_fb.bytes_pp;
    uint8_t *buf = draw_buf();
    for (uint32_t row = 0; row < win->h; row++) {
        uint8_t *dst = buf + (uint64_t)(wy + (int32_t)row) * g_fb.pitch
                            + (uint64_t)wx * g_fb.bytes_pp;
        memcpy(dst, win->content_bak + (uint64_t)row * row_bytes, row_bytes);
    }
}

 




static void ui_win_draw(int id) {
    ui_window_t *win = &g_windows[id];
    int32_t wx = win->x, wy = win->y;

    fb_fill_rect(wx, wy, win->w, win->h, UI_WIN_BODY_BG);
    fb_draw_rect(wx, wy, win->w, win->h, UI_WIN_BORDER, 1);

    fb_fill_rect(wx, wy, win->w, UI_WIN_TITLEBAR_H, UI_WIN_TITLEBAR_BG);
    fb_draw_rect(wx, wy, win->w, UI_WIN_TITLEBAR_H, UI_WIN_BORDER, 1);

    int32_t tx = wx + 8;
    int32_t ty = wy + (int32_t)((UI_WIN_TITLEBAR_H - FB_FONT_H) / 2u);
    fb_draw_string(tx, ty, win->title, UI_WIN_TITLEBAR_FG, UI_WIN_TITLEBAR_BG);

    int32_t cx, cy, ex, ey, mx, my;
    ui_win_btn_rects(win, &cx, &cy, &ex, &ey, &mx, &my);

    if (g_fxapp_close_loaded && g_fxapp_close_img.pixels) {
        fxapp_blit_png(cx, cy, &g_fxapp_close_img);
    } else {
        fb_fill_rect(cx, cy, UI_WIN_CLOSE_SZ, UI_WIN_CLOSE_SZ, 0xC04040U);
        fb_draw_rect(cx, cy, UI_WIN_CLOSE_SZ, UI_WIN_CLOSE_SZ, UI_WIN_BORDER, 1);
        fb_draw_string(cx + (int32_t)((UI_WIN_CLOSE_SZ - FB_FONT_W) / 2u),
                       cy + (int32_t)((UI_WIN_CLOSE_SZ - FB_FONT_H) / 2u),
                       "X", FB_WHITE, 0xC04040U);
    }

    if (g_fxapp_expand_loaded && g_fxapp_expand_img.pixels) {
        fxapp_blit_png(ex, ey, &g_fxapp_expand_img);
    } else {
        fb_fill_rect(ex, ey, UI_WIN_EXPAND_SZ, UI_WIN_EXPAND_SZ, 0x606060U);
        fb_draw_rect(ex, ey, UI_WIN_EXPAND_SZ, UI_WIN_EXPAND_SZ, UI_WIN_BORDER, 1);
         
        const char *glyph = win->maximized ? "><" : "[]";
        fb_draw_string(ex + (int32_t)((UI_WIN_EXPAND_SZ - 2u * FB_FONT_W) / 2u),
                       ey + (int32_t)((UI_WIN_EXPAND_SZ - FB_FONT_H) / 2u),
                       glyph, FB_WHITE, 0x606060U);
    }

    if (g_fxapp_minimize_loaded && g_fxapp_minimize_img.pixels) {
        fxapp_blit_png(mx, my, &g_fxapp_minimize_img);
    } else {
        fb_fill_rect(mx, my, UI_WIN_MIN_SZ, UI_WIN_MIN_SZ, 0x606060U);
        fb_draw_rect(mx, my, UI_WIN_MIN_SZ, UI_WIN_MIN_SZ, UI_WIN_BORDER, 1);
        fb_draw_string(mx + (int32_t)((UI_WIN_MIN_SZ - FB_FONT_W) / 2u),
                       my + (int32_t)((UI_WIN_MIN_SZ - FB_FONT_H) / 2u),
                       "_", FB_WHITE, 0x606060U);
    }
}

static void ui_taskbar_app_btn_rect(int slot_idx, int32_t panel_y,
                                    int32_t *bx, int32_t *by, uint32_t *bw, uint32_t *bh) {
    int32_t base_x = MENU_BTN_X + (int32_t)MENU_BTN_W + (int32_t)TASKBAR_APP_BTN_GAP;
    *bx = base_x + slot_idx * (int32_t)(TASKBAR_APP_BTN_W + TASKBAR_APP_BTN_GAP);
    *by = panel_y;    
    *bw = TASKBAR_APP_BTN_W;
    *bh = TASKBAR_APP_BTN_H;
}

 
static void ui_taskbar_btn_label(const char *title, uint32_t max_px, char *out, size_t out_sz) {
    size_t n = strlen(title);
    if (n >= out_sz) n = out_sz - 1;
     
    for (size_t i = 0; i < n; i++) out[i] = title[i];
    out[n] = '\0';

    if (fb_text_width(out) <= max_px) return;

     
    while (n > 0) {
        n--;
        out[n] = '\0';
        char tmp[UI_WIN_TITLE_MAX + 4];
        size_t m = (n < sizeof(tmp) - 4) ? n : sizeof(tmp) - 4;
        for (size_t i = 0; i < m; i++) tmp[i] = out[i];
        tmp[m] = '.'; tmp[m+1] = '.'; tmp[m+2] = '.'; tmp[m+3] = '\0';
        if (fb_text_width(tmp) <= max_px || n == 0) {
            for (size_t i = 0; i <= m + 3; i++) out[i] = tmp[i];
            return;
        }
    }
}

 

static void ui_taskbar_draw_one_app_button(int id, int slot_idx, int32_t panel_y) {
    int32_t bx, by; uint32_t bw, bh;
    ui_taskbar_app_btn_rect(slot_idx, panel_y, &bx, &by, &bw, &bh);

    fb_fill_rect(bx, by, bw, bh, TASKBAR_APP_BTN_BG);

    char label[UI_WIN_TITLE_MAX + 4];
    ui_taskbar_btn_label(g_windows[id].title, bw - 2u * TASKBAR_APP_BTN_TEXT_PAD, label, sizeof(label));

    int32_t tx = bx + TASKBAR_APP_BTN_TEXT_PAD;
    int32_t ty = by + (int32_t)((bh - FB_FONT_H) / 2u);
    fb_draw_string(tx, ty, label, TASKBAR_APP_BTN_FG, TASKBAR_APP_BTN_BG);
}

 


static void ui_taskbar_draw_app_buttons(int32_t panel_y) {
     

    int32_t area_x = MENU_BTN_X + (int32_t)MENU_BTN_W + (int32_t)TASKBAR_APP_BTN_GAP;
    int32_t clock_x = (int32_t)g_fb.width - (8 * FB_FONT_W) - 16;
    int32_t area_w = clock_x - area_x;
    if (area_w > 0) {
        fb_fill_rect(area_x, panel_y, (uint32_t)area_w, TASKBAR_H, TASKBAR_BG);
    }

    int slot = 0;
    for (int i = 0; i < (int)UI_MAX_WINDOWS; i++) {
        if (!g_windows[i].used || !g_windows[i].open) continue;
        ui_taskbar_draw_one_app_button(i, slot, panel_y);
        slot++;
    }
}

static int32_t ui_taskbar_panel_y(void) {
    return (int32_t)((g_fb.height >= TASKBAR_H) ? g_fb.height - TASKBAR_H : 0);
}

int ui_win_create(const char *title, uint32_t w, uint32_t h, int32_t owner_pid) {
    if (!title || w == 0 || h == 0) return UI_WIN_INVALID;

    int id = -1;
    for (int i = 0; i < (int)UI_MAX_WINDOWS; i++) {
        if (!g_windows[i].used) { id = i; break; }
    }
    if (id < 0) {
        DBG_MSG("WIN", "ui_win_create: no free window slots");
        return UI_WIN_INVALID;
    }

    ui_window_t *win = &g_windows[id];
    memset(win, 0, sizeof(*win));
    win->used = true;
    win->owner_pid = owner_pid;
    win->w = w;
    win->h = h;
    win->redraw_gen = 1;    




    size_t n = strlen(title);
    if (n >= UI_WIN_TITLE_MAX) n = UI_WIN_TITLE_MAX - 1;
    for (size_t i = 0; i < n; i++) win->title[i] = title[i];
    win->title[n] = '\0';

    int32_t panel_y = ui_taskbar_panel_y();
     





    int32_t base_x = 0, base_y = 0;
    bool placed = false;
    {
        const int32_t gap = 12;
        int32_t max_y = panel_y;
         


        int32_t max_x = (int32_t)g_fb.width - FXAPP_ICON_MARGIN - (int32_t)FXAPP_ICON_W
                        - FXAPP_ICON_HALO_PAD - 12;
        int32_t cx[2 * UI_MAX_WINDOWS + 1], cy[2 * UI_MAX_WINDOWS + 1];
        int nc = 0;
        cx[nc] = gap; cy[nc] = gap; nc++;
        for (int i = 0; i < (int)UI_MAX_WINDOWS; i++) {
            const ui_window_t *o = &g_windows[i];
            if (i == id || !o->used || !o->open || o->minimized) continue;
            cx[nc] = o->x + (int32_t)o->w + gap; cy[nc] = o->y; nc++;       
            cx[nc] = o->x; cy[nc] = o->y + (int32_t)o->h + gap; nc++;       
        }
        for (int k = 0; k < nc && !placed; k++) {
            int32_t px = cx[k], py = cy[k];
            if (px < 0 || py < 0) continue;
            if (px + (int32_t)w > max_x || py + (int32_t)h > max_y) continue;
            bool clash = false;
            for (int i = 0; i < (int)UI_MAX_WINDOWS && !clash; i++) {
                const ui_window_t *o = &g_windows[i];
                if (i == id || !o->used || !o->open || o->minimized) continue;
                if (px < o->x + (int32_t)o->w + 4 && px + (int32_t)w + 4 > o->x &&
                    py < o->y + (int32_t)o->h + 4 && py + (int32_t)h + 4 > o->y)
                    clash = true;
            }
            if (!clash) { base_x = px; base_y = py; placed = true; }
        }
    }
    if (!placed) {
        static int32_t cascade = 0;
        base_x = ((int32_t)g_fb.width - (int32_t)w) / 2 + (cascade % 5) * 24;
        base_y = (panel_y - (int32_t)h) / 2 + (cascade % 5) * 24;
        cascade++;
    }
    if (base_x < 0) base_x = 0;
    if (base_y < 0) base_y = 0;
    if (base_x + (int32_t)w > (int32_t)g_fb.width) base_x = (int32_t)g_fb.width - (int32_t)w;
    if (base_y + (int32_t)h > panel_y) base_y = panel_y - (int32_t)h;
    if (base_x < 0) base_x = 0;
    if (base_y < 0) base_y = 0;
    win->x = base_x;
    win->y = base_y;
    win->open = true;

    ui_win_save_bg(id, win->x, win->y);
    ui_win_draw(id);
    ui_taskbar_draw_app_buttons(panel_y);
    ui_focus_set(id);    
    fb_flip();

    DBG_VAL("WIN", "ui_win_create: id", (uint64_t)id);
    return id;
}

 


















 






 





#define UI_EXIT_REQ_MAX 256
static volatile uint8_t g_exit_req[UI_EXIT_REQ_MAX];

static volatile uint16_t g_exit_age[UI_EXIT_REQ_MAX];    
extern int sched_kill(uint32_t tid);

void ui_request_exit(int32_t tid) {
    if (tid > 0 && tid < UI_EXIT_REQ_MAX) { g_exit_req[tid] = 1; g_exit_age[tid] = 0; }
}

 


#define UI_EXIT_GRACE_TICKS 20
void ui_exit_watchdog(void) {
    for (int tid = 1; tid < UI_EXIT_REQ_MAX; tid++) {
        if (!g_exit_req[tid]) continue;
        if (++g_exit_age[tid] < UI_EXIT_GRACE_TICKS) continue;
        g_exit_req[tid] = 0;
        g_exit_age[tid] = 0;
        ui_win_close_all_owned_by((int32_t)tid);
        int rc = sched_kill((uint32_t)tid);
        dbg_puts("[WIN] exit watchdog: process "); dbg_dec64((uint64_t)tid);
        dbg_puts(rc == 1 ? " did not exit by itself -> killed\n"
               : rc == 0 ? " already gone\n" : " cannot be killed\n");
    }
}
bool ui_exit_requested(int32_t tid) {
    return tid > 0 && tid < UI_EXIT_REQ_MAX && g_exit_req[tid] != 0;
}
void ui_exit_request_clear(int32_t tid) {
    if (tid > 0 && tid < UI_EXIT_REQ_MAX) g_exit_req[tid] = 0;
}

int32_t ui_win_owner_pid(int win_id) {
    if (win_id < 0 || win_id >= (int)UI_MAX_WINDOWS) return -1;
    const ui_window_t *win = &g_windows[win_id];
    if (!win->used || !win->open) return -1;
    return win->owner_pid;
}

bool ui_win_get_rect(int win_id, int32_t *out_x, int32_t *out_y,
                     uint32_t *out_w, uint32_t *out_h, uint32_t *out_redraw_gen) {
    if (win_id < 0 || win_id >= (int)UI_MAX_WINDOWS) return false;
    ui_window_t *win = &g_windows[win_id];
    if (!win->used || !win->open) return false;
    if (win->h <= UI_WIN_TITLEBAR_H) return false;

    if (out_x) *out_x = win->x;
    if (out_y) *out_y = win->y + (int32_t)UI_WIN_TITLEBAR_H;
    if (out_w) *out_w = win->w;
    if (out_h) *out_h = win->h - UI_WIN_TITLEBAR_H;
    if (out_redraw_gen) *out_redraw_gen = win->redraw_gen;
    return true;
}

int ui_win_close(int win_id) {
    if (win_id < 0 || win_id >= (int)UI_MAX_WINDOWS) return -1;
    ui_window_t *win = &g_windows[win_id];
    if (!win->used || !win->open) return -1;

    DBG_VAL("WIN", "ui_win_close: id", (uint64_t)win_id);
    DBG_RC("WIN", "ui_win_close: owner_pid", win->owner_pid);
    ui_win_restore_bg(win_id, win->x, win->y);
    ui_focus_release(win_id);
    win->open = false;
    win->dragging = false;
    if (win->saved_bg) { kfree(win->saved_bg); win->saved_bg = NULL; }
    if (win->content_bak) { kfree(win->content_bak); win->content_bak = NULL; }
    win->used = false;

    ui_taskbar_draw_app_buttons(ui_taskbar_panel_y());
    fb_flip();
    return 0;
}

 







static int ui_win_move_ex(int win_id, int32_t x, int32_t y, bool do_flip) {
    if (win_id < 0 || win_id >= (int)UI_MAX_WINDOWS) return -1;
    ui_window_t *win = &g_windows[win_id];
    if (!win->used || !win->open) return -1;

    ui_win_capture(win_id, win->x, win->y);    
    ui_win_restore_bg(win_id, win->x, win->y);
    win->x = x;
    win->y = y;
    ui_win_save_bg(win_id, win->x, win->y);
    ui_win_blit_captured(win_id, win->x, win->y);   
    if (win_id == g_fxapp_win_id) fm_draw(win_id);    
    if (win_id == g_theme_win_id) theme_win_draw(win_id);
    if (win_id == g_fmdlg_win_id) fmdlg_draw(win_id);
    if (do_flip) fb_flip();
    return 0;
}

int ui_win_move(int win_id, int32_t x, int32_t y) {
    return ui_win_move_ex(win_id, x, y, true);
}

 





static void ui_win_minimize(int id) {
    if (id < 0 || id >= (int)UI_MAX_WINDOWS) return;
    ui_window_t *win = &g_windows[id];
    if (!win->used || !win->open || win->minimized) return;

     









    ui_win_capture(id, win->x, win->y);
    ui_win_restore_bg(id, win->x, win->y);
    win->minimized = true;
    win->dragging = false;
    ui_focus_release(id);
    fb_flip();
}

 




static void ui_win_restore_from_taskbar(int id) {
    if (id < 0 || id >= (int)UI_MAX_WINDOWS) return;
    ui_window_t *win = &g_windows[id];
    if (!win->used || !win->open || !win->minimized) return;

    win->minimized = false;
    ui_win_save_bg(id, win->x, win->y);
     




    ui_win_blit_captured(id, win->x, win->y);
    if (id == g_fxapp_win_id) fm_draw(id);
    if (id == g_theme_win_id) theme_win_draw(id);
    if (id == g_fmdlg_win_id) fmdlg_draw(id);
    fb_flip();
}

 










static void ui_win_toggle_maximize(int id) {
    if (id < 0 || id >= (int)UI_MAX_WINDOWS) return;
    ui_window_t *win = &g_windows[id];
    if (!win->used || !win->open || win->minimized) return;

    int32_t panel_y = ui_taskbar_panel_y();

    ui_win_restore_bg(id, win->x, win->y);
    if (win->saved_bg) { kfree(win->saved_bg); win->saved_bg = NULL; }
    if (win->content_bak) { kfree(win->content_bak); win->content_bak = NULL; }
     






    win->redraw_gen++;

    if (!win->maximized) {
        win->rest_x = win->x; win->rest_y = win->y;
        win->rest_w = win->w; win->rest_h = win->h;

        win->x = 0;
        win->y = 0;
        win->w = (uint32_t)g_fb.width;
        win->h = (uint32_t)panel_y;
        win->maximized = true;
    } else {
        win->x = win->rest_x;
        win->y = win->rest_y;
        win->w = win->rest_w;
        win->h = win->rest_h;
        win->maximized = false;
    }

    ui_win_save_bg(id, win->x, win->y);
    ui_win_draw(id);
    if (id == g_fxapp_win_id) fm_draw(id);
    if (id == g_theme_win_id) theme_win_draw(id);
    if (id == g_fmdlg_win_id) fmdlg_draw(id);
    fb_flip();
}

void ui_win_close_all_owned_by(int32_t owner_pid) {
    for (int i = 0; i < (int)UI_MAX_WINDOWS; i++) {
        if (g_windows[i].used && g_windows[i].open && g_windows[i].owner_pid == owner_pid)
            ui_win_close(i);
    }
}

 






#define UI_RES_MAX 24
typedef struct {
    uint32_t w, h;
    char     label[14];
} ui_resolution_t;

static ui_resolution_t g_resolutions[UI_RES_MAX] = {
    { 1280, 800,  "1280x800"  },
    { 1366, 768,  "1366x768"  },
    { 1680, 1050, "1680x1050" },
    { 1920, 1080, "1920x1080" },
};
static int g_res_count = 4;

static int g_res_idx     = 0;    
static int g_res_cur_idx = 0;    

static int res_find(uint32_t w, uint32_t h) {
    for (int i = 0; i < g_res_count; i++)
        if (g_resolutions[i].w == w && g_resolutions[i].h == h) return i;
    return -1;
}

static void res_make_label(char *dst, uint32_t w, uint32_t h) {
    char t[12]; int n = 0, o = 0;
    uint32_t v = w;
    if (v == 0) t[n++] = '0';
    while (v > 0 && n < 6) { t[n++] = (char)('0' + v % 10); v /= 10; }
    while (n > 0) dst[o++] = t[--n];
    dst[o++] = 'x';
    v = h;
    if (v == 0) t[n++] = '0';
    while (v > 0 && n < 6) { t[n++] = (char)('0' + v % 10); v /= 10; }
    while (n > 0) dst[o++] = t[--n];
    dst[o] = '\0';
}

static int res_add(uint32_t w, uint32_t h) {
    int i = res_find(w, h);
    if (i >= 0) return i;
    if (g_res_count >= UI_RES_MAX) return -1;
    g_resolutions[g_res_count].w = w;
    g_resolutions[g_res_count].h = h;
    res_make_label(g_resolutions[g_res_count].label, w, h);
    return g_res_count++;
}

 

static void res_sort(void) {
    for (int i = 1; i < g_res_count; i++) {
        ui_resolution_t key = g_resolutions[i];
        uint64_t ka = (uint64_t)key.w * key.h;
        int j = i - 1;
        while (j >= 0 && (uint64_t)g_resolutions[j].w * g_resolutions[j].h > ka) {
            g_resolutions[j + 1] = g_resolutions[j];
            j--;
        }
        g_resolutions[j + 1] = key;
    }
}

 





















 
static bool ui_persist_display_cfg(const char *label) {
    int fd = vfs_open("/display.cfg", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        DBG_MSG("UI", "display.cfg: vfs_open(write) failed");
        return false;
    }
    size_t len = strlen(label);
    int64_t w1 = vfs_write(fd, label, len);
    int64_t w2 = vfs_write(fd, "\n", 1);
    vfs_close(fd);
    return (w1 == (int64_t)len && w2 == 1);
}

 

static void ui_load_mode_list(void) {
    int fd = vfs_open("/modes.txt", O_RDONLY);
    if (fd < 0) return;    

    char buf[768];
    int64_t got = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (got <= 0) return;
    buf[got] = '\0';

    ui_resolution_t tmp[UI_RES_MAX];
    int tn = 0;
    int64_t i = 0;
    while (i < got && tn < UI_RES_MAX) {
        uint32_t a = 0, b = 0; bool ha = false, hb = false;
        while (i < got && buf[i] >= '0' && buf[i] <= '9') { a = a * 10 + (uint32_t)(buf[i] - '0'); i++; ha = true; }
        if (i < got && (buf[i] == 'x' || buf[i] == 'X')) {
            i++;
            while (i < got && buf[i] >= '0' && buf[i] <= '9') { b = b * 10 + (uint32_t)(buf[i] - '0'); i++; hb = true; }
        }
        if (ha && hb && a >= 640 && b >= 480 && a <= 16384 && b <= 16384) {
            bool dup = false;
            for (int k = 0; k < tn; k++) if (tmp[k].w == a && tmp[k].h == b) dup = true;
            if (!dup) { tmp[tn].w = a; tmp[tn].h = b; tn++; }
        }
        while (i < got && buf[i] != '\n') i++;    
        i++;
    }
    if (tn == 0) return;

    g_res_count = 0;
    for (int k = 0; k < tn; k++) res_add(tmp[k].w, tmp[k].h);
}

 



static void ui_load_persisted_resolution(void) {
    ui_load_mode_list();
    (void)res_add(g_fb.native_width, g_fb.native_height);    
    res_sort();
    int cur = res_find(g_fb.native_width, g_fb.native_height);
    g_res_cur_idx = (cur >= 0) ? cur : 0;
    g_res_idx     = g_res_cur_idx;
}


 







static void apply_theme(int idx) {
    if (idx < 0 || idx >= UI_THEME_COUNT) return;
    g_theme_idx = idx;

    const ui_theme_t *t = &g_themes[idx];
    TASKBAR_BG         = t->taskbar_bg;
    MENU_BTN_BG        = t->menu_btn_bg;
    TASKBAR_APP_BTN_BG = t->app_btn_bg;
    UI_WIN_TITLEBAR_BG = t->win_titlebar_bg;

    DBG_VAL("UI", "theme changed to idx", (uint64_t)idx);
    ui_draw_desktop();
}

 












extern void usb_delay_ms(uint32_t ms);

static void res_show_message(const char *msg) {
    uint32_t sw = g_fb.width, sh = g_fb.height;
    uint32_t tw = (uint32_t)fb_text_width(msg);
    uint32_t bw = tw + 40, bh = 60;
    if (bw > sw || bh > sh) return;
    int32_t bx = (int32_t)((sw - bw) / 2u), by = (int32_t)((sh - bh) / 2u);
    fb_fill_rect(bx, by, bw, bh, FB_BLACK);
    fb_draw_rect(bx, by, bw, bh, 0xFFFFFFU, 2);
    fb_draw_string(bx + 20, by + (int32_t)((bh - FB_FONT_H) / 2u), msg, FB_WHITE, FB_BLACK);
    fb_flip();
}

static void apply_resolution(int idx) {
    if (idx < 0 || idx >= g_res_count) return;
    const ui_resolution_t *r = &g_resolutions[idx];

     

    if (r->w == g_fb.native_width && r->h == g_fb.native_height) {
        g_res_idx = g_res_cur_idx = idx;
        fb_apply_virtual_resolution(r->w, r->h);
        ui_draw_desktop();
        return;
    }

    char msg[64] = "Applying ";
    size_t n = strlen(msg);
    for (const char *q = r->label; *q && n < 40; q++) msg[n++] = *q;
    const char *tail = " - restarting...";
    for (const char *q = tail; *q && n < 62; q++) msg[n++] = *q;
    msg[n] = '\0';

    if (ui_persist_display_cfg(r->label)) {
        DBG_VAL("UI", "resolution saved, rebooting, idx", (uint64_t)idx);
        res_show_message(msg);
        usb_delay_ms(800);           
        ui_system_reboot();          
        return;
    }

     
    DBG_MSG("UI", "display.cfg write failed - virtual resolution fallback");
    g_res_idx = g_res_cur_idx = idx;
    fb_apply_virtual_resolution(r->w, r->h);
    ui_draw_desktop();
}

 











#define THEME_WIN_W          300u
#define THEME_WIN_H          196u

#define THEME_ROW_THEME          0
#define THEME_ROW_DISPLAY        1
#define THEME_ROW_COUNT           2

#define THEME_LABEL_X          16
#define THEME_ROW_Y             16    
#define THEME_BOX_W             90u
#define THEME_BOX_H             30u
#define THEME_BOX_GAP           14    
#define THEME_ARROW_W           30u
#define THEME_ARROW_H           30u
#define THEME_BOX_BG        FB_BLACK
#define THEME_BOX_FG        FB_WHITE
#define THEME_BOX_BORDER    0x606060U
#define THEME_ARROW_BG      0x303030U
#define THEME_ARROW_FG      FB_WHITE

 


static const char *theme_row_label(int row) {
    return (row == THEME_ROW_DISPLAY) ? "Display" : "Theme";
}

 


static void theme_row_positions(int win_id, int row,
                                int32_t *label_x, int32_t *box_x, int32_t *box_y) {
    ui_window_t *win = &g_windows[win_id];
    int32_t base_x = win->x;
    int32_t base_y = win->y + (int32_t)UI_WIN_TITLEBAR_H;

    *label_x = base_x + THEME_LABEL_X;
    *box_x   = *label_x + (int32_t)fb_text_width("Display") + THEME_BOX_GAP;
    *box_y   = base_y + THEME_ROW_Y + row * (int32_t)(THEME_BOX_H + THEME_BOX_GAP);
}

 

static void theme_arrow_rect(int win_id, int row, int32_t *ax, int32_t *ay,
                             uint32_t *aw, uint32_t *ah) {
    int32_t label_x, box_x, box_y;
    theme_row_positions(win_id, row, &label_x, &box_x, &box_y);
    *ax = box_x + (int32_t)THEME_BOX_W + THEME_BOX_GAP;
    *ay = box_y;
    *aw = THEME_ARROW_W;
    *ah = THEME_ARROW_H;
}

 
static void theme_apply_rect(int win_id, int32_t *bx, int32_t *by, uint32_t *bw, uint32_t *bh) {
    int32_t label_x, box_x, box_y;
    theme_row_positions(win_id, THEME_ROW_DISPLAY, &label_x, &box_x, &box_y);
    *bx = box_x;
    *by = box_y + (int32_t)THEME_BOX_H + THEME_BOX_GAP;
    *bw = THEME_BOX_W + (uint32_t)THEME_BOX_GAP + THEME_ARROW_W;
    *bh = THEME_BOX_H;
}

 

static void theme_row_draw(int win_id, int row, const char *value) {
    int32_t label_x, box_x, box_y;
    theme_row_positions(win_id, row, &label_x, &box_x, &box_y);

    int32_t label_y = box_y + (int32_t)((THEME_BOX_H - FB_FONT_H) / 2u);
    fb_draw_string(label_x, label_y, theme_row_label(row), FB_BLACK, UI_WIN_BODY_BG);

    fb_fill_rect(box_x, box_y, THEME_BOX_W, THEME_BOX_H, THEME_BOX_BG);
    fb_draw_rect(box_x, box_y, THEME_BOX_W, THEME_BOX_H, THEME_BOX_BORDER, 1);

    int32_t tw = (int32_t)fb_text_width(value);
    int32_t tx = box_x + (int32_t)((THEME_BOX_W - (uint32_t)(tw > 0 ? tw : 0)) / 2u);
    int32_t ty = box_y + (int32_t)((THEME_BOX_H - FB_FONT_H) / 2u);
    fb_draw_string(tx, ty, value, THEME_BOX_FG, THEME_BOX_BG);

    int32_t ax, ay; uint32_t aw, ah;
    theme_arrow_rect(win_id, row, &ax, &ay, &aw, &ah);
    fb_fill_rect(ax, ay, aw, ah, THEME_ARROW_BG);
    fb_draw_rect(ax, ay, aw, ah, THEME_BOX_BORDER, 1);
    fb_draw_string(ax + (int32_t)((aw - FB_FONT_W) / 2u),
                   ay + (int32_t)((ah - FB_FONT_H) / 2u),
                   ">", THEME_ARROW_FG, THEME_ARROW_BG);
}

static void theme_win_draw(int win_id) {
    if (win_id < 0 || win_id >= (int)UI_MAX_WINDOWS) return;
    ui_window_t *win = &g_windows[win_id];
    if (!win->used) return;

    int32_t  base_x    = win->x;
    int32_t  base_y    = win->y + (int32_t)UI_WIN_TITLEBAR_H;
    uint32_t content_h = (win->h > UI_WIN_TITLEBAR_H) ? win->h - UI_WIN_TITLEBAR_H : 0;

     

    fb_fill_rect(base_x, base_y, win->w, content_h, UI_WIN_BODY_BG);

    theme_row_draw(win_id, THEME_ROW_THEME,   g_themes[g_theme_idx].name);
    theme_row_draw(win_id, THEME_ROW_DISPLAY, g_resolutions[g_res_idx].label);

     

    int32_t bx, by; uint32_t bw, bh;
    theme_apply_rect(win_id, &bx, &by, &bw, &bh);
    bool pending = (g_res_idx != g_res_cur_idx);
    fb_color_t bg = pending ? 0x2266CCU : 0xB0B0B0U;
    const char *txt = pending ? "Apply & restart" : "Current mode";
    fb_fill_rect(bx, by, bw, bh, bg);
    fb_draw_rect(bx, by, bw, bh, THEME_BOX_BORDER, 1);
    int32_t ttw = (int32_t)fb_text_width(txt);
    fb_draw_string(bx + (int32_t)((bw - (uint32_t)(ttw > 0 ? ttw : 0)) / 2u),
                   by + (int32_t)((bh - FB_FONT_H) / 2u), txt,
                   pending ? FB_WHITE : 0x505050U, bg);
}

 

static void theme_win_handle_click(int win_id, int32_t x, int32_t y) {
    int32_t ax, ay; uint32_t aw, ah;

    theme_arrow_rect(win_id, THEME_ROW_THEME, &ax, &ay, &aw, &ah);
    if (point_in_rect(x, y, ax, ay, aw, ah)) {
        apply_theme((g_theme_idx + 1) % UI_THEME_COUNT);
        return;
    }

    theme_arrow_rect(win_id, THEME_ROW_DISPLAY, &ax, &ay, &aw, &ah);
    if (point_in_rect(x, y, ax, ay, aw, ah)) {
         
        g_res_idx = (g_res_idx + 1) % (g_res_count > 0 ? g_res_count : 1);
        theme_win_draw(win_id);
        fb_flip();
        return;
    }

    int32_t bx, by; uint32_t bw, bh;
    theme_apply_rect(win_id, &bx, &by, &bw, &bh);
    if (point_in_rect(x, y, bx, by, bw, bh) && g_res_idx != g_res_cur_idx) {
        apply_resolution(g_res_idx);
    }
}

 

static void ui_open_theme_window(void) {
    if (g_theme_win_id < 0 || !g_windows[g_theme_win_id].used || !g_windows[g_theme_win_id].open) {
        g_theme_win_id = ui_win_create("Desktop settings", THEME_WIN_W, THEME_WIN_H, -1);
        if (g_theme_win_id >= 0) {
            theme_win_draw(g_theme_win_id);
            fb_flip();
        }
    } else if (g_windows[g_theme_win_id].minimized) {
        ui_win_restore_from_taskbar(g_theme_win_id);
    }
}

 








#define CTXMENU_W               180u
#define CTXMENU_PAD               8
#define CTXMENU_ITEM_H           32u
#define CTXMENU_H        (2u * CTXMENU_PAD + CTXMENU_ITEM_H)
#define CTXMENU_BG           0x2B2B2BU
#define CTXMENU_BORDER       0x000000U
#define CTXMENU_ITEM_BG_HOVER 0x454545U
#define CTXMENU_ITEM_FG      0xE8E8E8U

static bool        g_ctxmenu_open  = false;
static bool        g_ctxmenu_hover = false;
static int32_t     g_ctxmenu_x = 0, g_ctxmenu_y = 0;
static fb_color_t *g_ctxmenu_saved_bg = NULL;

static void ctxmenu_item_rect(int32_t mx, int32_t my,
                              int32_t *ix, int32_t *iy, uint32_t *iw, uint32_t *ih) {
    *ix = mx + CTXMENU_PAD;
    *iy = my + CTXMENU_PAD;
    *iw = CTXMENU_W - 2u * CTXMENU_PAD;
    *ih = CTXMENU_ITEM_H;
}

static void ctxmenu_save_bg(int32_t mx, int32_t my) {
    if (!g_ctxmenu_saved_bg) {
        g_ctxmenu_saved_bg = (fb_color_t *)kmalloc(
            (uint64_t)CTXMENU_W * CTXMENU_H * sizeof(fb_color_t));
    }
    if (!g_ctxmenu_saved_bg) return;
    for (uint32_t row = 0; row < CTXMENU_H; row++)
        for (uint32_t col = 0; col < CTXMENU_W; col++)
            g_ctxmenu_saved_bg[row * CTXMENU_W + col] =
                fb_get_pixel(mx + (int32_t)col, my + (int32_t)row);
}

static void ctxmenu_restore_bg(int32_t mx, int32_t my) {
    if (!g_ctxmenu_saved_bg) return;
    for (uint32_t row = 0; row < CTXMENU_H; row++)
        for (uint32_t col = 0; col < CTXMENU_W; col++)
            fb_put_pixel(mx + (int32_t)col, my + (int32_t)row,
                        g_ctxmenu_saved_bg[row * CTXMENU_W + col]);
    fb_flip();
}

static void ui_draw_ctxmenu(void) {
    int32_t mx = g_ctxmenu_x, my = g_ctxmenu_y;
    fb_fill_rect(mx, my, CTXMENU_W, CTXMENU_H, CTXMENU_BG);
    fb_draw_rect(mx, my, CTXMENU_W, CTXMENU_H, CTXMENU_BORDER, 1);

    int32_t ix, iy; uint32_t iw, ih;
    ctxmenu_item_rect(mx, my, &ix, &iy, &iw, &ih);
    fb_color_t bg = g_ctxmenu_hover ? CTXMENU_ITEM_BG_HOVER : CTXMENU_BG;
    fb_fill_rect(ix, iy, iw, ih, bg);

    int32_t tx = ix + 8;
    int32_t ty = iy + (int32_t)((ih - FB_FONT_H) / 2u);
    fb_draw_string(tx, ty, "Desktop settings", CTXMENU_ITEM_FG, bg);

    fb_flip();
}

 





 







static inline uint64_t dbg_win_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
static inline uint64_t dbg_win_tsc_to_ns(uint64_t tsc) {
    return tsc / 3ULL;    
}

void ui_desktop_handle_mouse(int32_t x, int32_t y, uint8_t buttons) {
    if (g_fb.mode != FB_MODE_LINEAR) return;
    if (!g_fb.width || !g_fb.height) return;

     


    fmdlg_poll_keyboard();
    ui_kbd_drain_unclaimed();

    int32_t panel_y = (int32_t)((g_fb.height >= TASKBAR_H) ? g_fb.height - TASKBAR_H : 0);

    bool left_now  = (buttons & 0x01) != 0;
    bool left_was  = (g_prev_buttons & 0x01) != 0;
    bool left_click = left_now && !left_was;    

     
    int32_t mbx = MENU_BTN_X;
    int32_t mby = panel_y;    
    if (left_click && point_in_rect(x, y, mbx, mby, MENU_BTN_W, MENU_BTN_H)) {
        if (g_ctxmenu_open) {
            ctxmenu_restore_bg(g_ctxmenu_x, g_ctxmenu_y);
            g_ctxmenu_open = false;
        }
        int32_t mx, my;
        menu_popup_rect(panel_y, &mx, &my);
        if (g_menu_open) {
            menu_restore_bg(mx, my);
            g_menu_open = false;
        } else {
            menu_save_bg(mx, my);
            g_menu_open = true;
            g_menu_hover = -1;
            ui_draw_start_menu(panel_y);
        }
        g_prev_buttons = buttons;
        return;
    }

    if (g_menu_open) {
        int32_t mx, my;
        menu_popup_rect(panel_y, &mx, &my);

        int new_hover = -1;
        for (int i = 0; i < 2; i++) {
            int32_t ix, iy; uint32_t iw, ih;
            menu_item_rect(i, mx, my, &ix, &iy, &iw, &ih);
            if (point_in_rect(x, y, ix, iy, iw, ih)) { new_hover = i; break; }
        }
        if (new_hover != g_menu_hover) {
            g_menu_hover = new_hover;
            ui_draw_start_menu(panel_y);
        }

        if (left_click && new_hover >= 0) {
            if (new_hover == 0) ui_system_shutdown();
            else                ui_system_reboot();
             

        } else if (left_click && new_hover < 0 &&
                   !point_in_rect(x, y, mx, my, MENU_POPUP_W, MENU_POPUP_H)) {
             
            menu_restore_bg(mx, my);
            g_menu_open = false;
        }
    }

     



    bool right_now   = (buttons & 0x02) != 0;
    bool right_was   = (g_prev_buttons & 0x02) != 0;
    bool right_click = right_now && !right_was;

    if (right_click) {
         




        if (g_fxapp_win_id >= 0 && g_windows[g_fxapp_win_id].used &&
            g_windows[g_fxapp_win_id].open && !g_windows[g_fxapp_win_id].minimized) {
            ui_window_t *fmw = &g_windows[g_fxapp_win_id];
            bool in_fm_body = y >= fmw->y + (int32_t)UI_WIN_TITLEBAR_H &&
                              point_in_rect(x, y, fmw->x, fmw->y, fmw->w, fmw->h);

            if (in_fm_body && g_fm_view == FM_VIEW_DIR) {
                if (g_ctxmenu_open) { ctxmenu_restore_bg(g_ctxmenu_x, g_ctxmenu_y); g_ctxmenu_open = false; }
                if (g_fmctx_open)   { fmctx_restore_bg(g_fmctx_x, g_fmctx_y);       g_fmctx_open = false; }

                 

                int hit = fm_hit_test(g_fxapp_win_id, x, y);
                if (hit >= 0 && (g_fm_entries[hit].kind == FM_KIND_DIR ||
                                 g_fm_entries[hit].kind == FM_KIND_FILE)) {
                    g_fmctx_mode   = 1;
                    g_fmctx_n      = 1;
                    g_fmctx_is_dir = (g_fm_entries[hit].kind == FM_KIND_DIR);
                    strlcpy(g_fmctx_name, g_fm_entries[hit].label, sizeof(g_fmctx_name));
                    strlcpy(g_fmctx_dir,  g_fm_cur_path,           sizeof(g_fmctx_dir));
                } else if (hit >= 0) {
                     
                    g_prev_buttons = buttons;
                    return;
                } else {
                    g_fmctx_mode = 0;
                    g_fmctx_n    = 2;
                }

                int32_t mx = x, my = y;
                if (mx + (int32_t)FMCTX_W > (int32_t)g_fb.width) mx = (int32_t)g_fb.width - (int32_t)FMCTX_W;
                if (my + (int32_t)fmctx_h() > panel_y) my = panel_y - (int32_t)fmctx_h();
                if (mx < 0) mx = 0;
                if (my < 0) my = 0;

                g_fmctx_x = mx; g_fmctx_y = my;
                g_fmctx_win_id = g_fxapp_win_id;
                g_fmctx_hover = -1;
                fmctx_save_bg(mx, my);
                fmctx_draw();
                g_fmctx_open = true;

                g_prev_buttons = buttons;
                return;
            } else if (in_fm_body) {
                 

                g_prev_buttons = buttons;
                return;
            }
        }

        bool over_taskbar = (y >= panel_y);
        bool over_icon = ui_point_over_any_desktop_icon(x, y);
        bool over_window = false;
        for (int wi = 0; wi < (int)UI_MAX_WINDOWS; wi++) {
            ui_window_t *w = &g_windows[wi];
            if (!w->used || !w->open || w->minimized) continue;
            if (point_in_rect(x, y, w->x, w->y, w->w, w->h)) { over_window = true; break; }
        }

        if (g_ctxmenu_open) {
            ctxmenu_restore_bg(g_ctxmenu_x, g_ctxmenu_y);
            g_ctxmenu_open = false;
        }

        if (!over_taskbar && !over_icon && !over_window && !g_menu_open) {
            int32_t mx = x, my = y;
            if (mx + (int32_t)CTXMENU_W > (int32_t)g_fb.width) mx = (int32_t)g_fb.width - (int32_t)CTXMENU_W;
            if (my + (int32_t)CTXMENU_H > panel_y) my = panel_y - (int32_t)CTXMENU_H;
            if (mx < 0) mx = 0;
            if (my < 0) my = 0;

            g_ctxmenu_x = mx;
            g_ctxmenu_y = my;
            g_ctxmenu_hover = false;
            ctxmenu_save_bg(mx, my);
            ui_draw_ctxmenu();
            g_ctxmenu_open = true;
        }

        g_prev_buttons = buttons;
        return;
    }

    if (g_ctxmenu_open) {
        int32_t ix, iy; uint32_t iw, ih;
        ctxmenu_item_rect(g_ctxmenu_x, g_ctxmenu_y, &ix, &iy, &iw, &ih);
        bool now_hover = point_in_rect(x, y, ix, iy, iw, ih);
        if (now_hover != g_ctxmenu_hover) {
            g_ctxmenu_hover = now_hover;
            ui_draw_ctxmenu();
        }

        if (left_click && now_hover) {
            ctxmenu_restore_bg(g_ctxmenu_x, g_ctxmenu_y);
            g_ctxmenu_open = false;
            ui_open_theme_window();
        } else if (left_click && !now_hover &&
                   !point_in_rect(x, y, g_ctxmenu_x, g_ctxmenu_y, CTXMENU_W, CTXMENU_H)) {
             
            ctxmenu_restore_bg(g_ctxmenu_x, g_ctxmenu_y);
            g_ctxmenu_open = false;
        }
    }

     

    if (g_fmctx_open) {
        int new_hover = -1;
        for (int i = 0; i < g_fmctx_n; i++) {
            int32_t ix, iy; uint32_t iw, ih;
            fmctx_item_rect(i, g_fmctx_x, g_fmctx_y, &ix, &iy, &iw, &ih);
            if (point_in_rect(x, y, ix, iy, iw, ih)) { new_hover = i; break; }
        }
        if (new_hover != g_fmctx_hover) {
            g_fmctx_hover = new_hover;
            fmctx_draw();
        }

        if (left_click && new_hover >= 0) {
            fmctx_restore_bg(g_fmctx_x, g_fmctx_y);
            g_fmctx_open = false;
            if (g_fmctx_mode == 1)
                fmctx_do_delete();
            else
                fmdlg_open(new_hover == 0 ? FMDLG_FOLDER : FMDLG_FILE, g_fmctx_win_id);
        } else if (left_click && new_hover < 0 &&
                   !point_in_rect(x, y, g_fmctx_x, g_fmctx_y, FMCTX_W, fmctx_h())) {
            fmctx_restore_bg(g_fmctx_x, g_fmctx_y);
            g_fmctx_open = false;
        }
    }

     



    if (left_click) {
        int slot = 0;
        for (int i = 0; i < (int)UI_MAX_WINDOWS; i++) {
            if (!g_windows[i].used || !g_windows[i].open) continue;
            int32_t bx, by; uint32_t bw, bh;
            ui_taskbar_app_btn_rect(slot, panel_y, &bx, &by, &bw, &bh);
            if (point_in_rect(x, y, bx, by, bw, bh)) {
                if (g_windows[i].minimized) ui_win_restore_from_taskbar(i);
                ui_focus_set(i);
                break;
            }
            slot++;
        }
    }

     















    bool cursor_over_window = false;
    for (int wi = 0; wi < (int)UI_MAX_WINDOWS; wi++) {
        ui_window_t *w = &g_windows[wi];
        if (!w->used || !w->open || w->minimized) continue;
        if (point_in_rect(x, y, w->x, w->y, w->w, w->h)) { cursor_over_window = true; break; }
    }

    if (!cursor_over_window) {
        int32_t icx, icy;
        icon_rect(&icx, &icy);
        int32_t  hx = icx - FXAPP_ICON_HALO_PAD;
        int32_t  hy = icy - FXAPP_ICON_HALO_PAD;
        uint32_t hw = FXAPP_ICON_W + 2u * FXAPP_ICON_HALO_PAD;
        uint32_t hh = FXAPP_ICON_H + 2u * FXAPP_ICON_HALO_PAD;

        bool now_hover = point_in_rect(x, y, hx, hy, hw, hh);
        if (now_hover != g_fxapp_icon_hover) {
            g_fxapp_icon_hover = now_hover;
            fxapp_draw_icon(g_fxapp_icon_hover);
        }

        if (left_click && now_hover) {
            if (g_fxapp_win_id < 0 || !g_windows[g_fxapp_win_id].used) {
                g_fxapp_win_id = ui_win_create(g_fxapp_title, FXAPP_WIN_W, FXAPP_WIN_H, -1);
                if (g_fxapp_win_id >= 0) {
                     

                    fm_reset();
                    fm_draw(g_fxapp_win_id);
                    fb_flip();
                }
            }
        }
    } else if (g_fxapp_icon_hover) {
         



        g_fxapp_icon_hover = false;
    }

     






    if (!cursor_over_window) {
        int32_t icx, icy;
        calc_icon_rect(&icx, &icy);
        int32_t  hx = icx - FXAPP_ICON_HALO_PAD;
        int32_t  hy = icy - FXAPP_ICON_HALO_PAD;
        uint32_t hw = FXAPP_ICON_W + 2u * FXAPP_ICON_HALO_PAD;
        uint32_t hh = FXAPP_ICON_H + 2u * FXAPP_ICON_HALO_PAD;

        bool now_hover = point_in_rect(x, y, hx, hy, hw, hh);
        if (now_hover != g_calc_icon_hover) {
            g_calc_icon_hover = now_hover;
            calc_draw_icon(g_calc_icon_hover);
        }

        if (left_click && now_hover) {
             







            if (g_calc_running_tid >= 0 && sched_task_alive((uint32_t)g_calc_running_tid)) {
                DBG_MSG("FXA", "calc: already running, repeated spawn skipped");
            } else {
                dbg_puts("[FXA] click on icon 'calc' -> spawn\n");
                uint64_t spawn_flags = fm_io_cli();
                int tid = elf64_spawn(g_calc_elf_path, g_calc_title, 0  );
                fm_io_sti(spawn_flags);
                g_calc_running_tid = tid;
                fexox_os_dbg_spawn_rc = tid;
                DBG_RC("FXA", "calc: elf64_spawn rc", tid);
            }
        }
    } else if (g_calc_icon_hover) {
        g_calc_icon_hover = false;
    }

     

    if (!cursor_over_window) {
        int32_t icx, icy;
        syslogs_icon_rect(&icx, &icy);
        int32_t  hx = icx - FXAPP_ICON_HALO_PAD;
        int32_t  hy = icy - FXAPP_ICON_HALO_PAD;
        uint32_t hw = FXAPP_ICON_W + 2u * FXAPP_ICON_HALO_PAD;
        uint32_t hh = FXAPP_ICON_H + 2u * FXAPP_ICON_HALO_PAD;

        bool now_hover = point_in_rect(x, y, hx, hy, hw, hh);
        if (now_hover != g_syslogs_icon_hover) {
            g_syslogs_icon_hover = now_hover;
            syslogs_draw_icon(g_syslogs_icon_hover);
        }

        if (left_click && now_hover) {
             

            if (g_syslogs_running_tid >= 0 && sched_task_alive((uint32_t)g_syslogs_running_tid)) {
                DBG_MSG("FXA", "syslogs: already running, repeated spawn skipped");
            } else {
                dbg_puts("[FXA] click on icon 'syslogs' -> spawn\n");
                uint64_t spawn_flags = fm_io_cli();
                int tid = elf64_spawn(g_syslogs_elf_path, g_syslogs_title, 0  );
                fm_io_sti(spawn_flags);
                g_syslogs_running_tid = tid;
                fexox_os_dbg_spawn_rc = tid;
                DBG_RC("FXA", "syslogs: elf64_spawn rc", tid);
            }
        }
    } else if (g_syslogs_icon_hover) {
        g_syslogs_icon_hover = false;
    }

     

    if (!cursor_over_window) {
        int32_t icx, icy;
        browser_icon_rect(&icx, &icy);
        int32_t  hx = icx - FXAPP_ICON_HALO_PAD;
        int32_t  hy = icy - FXAPP_ICON_HALO_PAD;
        uint32_t hw = FXAPP_ICON_W + 2u * FXAPP_ICON_HALO_PAD;
        uint32_t hh = FXAPP_ICON_H + 2u * FXAPP_ICON_HALO_PAD;

        bool now_hover = point_in_rect(x, y, hx, hy, hw, hh);
        if (now_hover != g_browser_icon_hover) {
            g_browser_icon_hover = now_hover;
            browser_draw_icon(g_browser_icon_hover);
        }

        if (left_click && now_hover) {
             

            if (g_browser_running_tid >= 0 && sched_task_alive((uint32_t)g_browser_running_tid)) {
                DBG_MSG("FXA", "browser: already running, repeated spawn skipped");
            } else {
                dbg_puts("[FXA] click on icon 'browser' -> spawn\n");
                uint64_t spawn_flags = fm_io_cli();
                int tid = elf64_spawn(g_browser_elf_path, g_browser_title, 0  );
                fm_io_sti(spawn_flags);
                g_browser_running_tid = tid;
                fexox_os_dbg_spawn_rc = tid;
                DBG_RC("FXA", "browser: elf64_spawn rc", tid);
            }
        }
    } else if (g_browser_icon_hover) {
        g_browser_icon_hover = false;
    }

     


    for (int i = 0; i < (int)UI_MAX_WINDOWS; i++) {
        ui_window_t *win = &g_windows[i];
        if (!win->used || !win->open) continue;
        if (win->minimized) continue;    

        int32_t cx, cy, ex, ey, mnx, mny;
        ui_win_btn_rects(win, &cx, &cy, &ex, &ey, &mnx, &mny);

        if (left_click && point_in_rect(x, y, cx, cy, UI_WIN_CLOSE_SZ, UI_WIN_CLOSE_SZ)) {
            int32_t closed_owner = win->owner_pid;
            ui_win_close(i);
            if (closed_owner > 0) {
                ui_request_exit(closed_owner);    
                dbg_puts("[WIN] close button: exit requested for pid ");
                dbg_dec64((uint64_t)closed_owner); dbg_puts("\n");
            }
            if (i == g_fxapp_win_id) g_fxapp_win_id = -1;
            if (i == g_theme_win_id) g_theme_win_id = -1;
            if (i == g_fmdlg_win_id) { g_fmdlg_win_id = -1; g_fmdlg_kind = FMDLG_NONE; }
            continue;
        }

        if (left_click && point_in_rect(x, y, ex, ey, UI_WIN_EXPAND_SZ, UI_WIN_EXPAND_SZ)) {
            ui_win_toggle_maximize(i);
            continue;
        }

        if (left_click && point_in_rect(x, y, mnx, mny, UI_WIN_MIN_SZ, UI_WIN_MIN_SZ)) {
            ui_win_minimize(i);
            continue;
        }

         

        if (left_click && point_in_rect(x, y, win->x, win->y, win->w, win->h))
            ui_focus_set(i);

         

        if (i == g_fxapp_win_id && left_click &&
            y >= win->y + (int32_t)UI_WIN_TITLEBAR_H &&
            point_in_rect(x, y, win->x, win->y, win->w, win->h)) {
            DBG_MSG("FXA", "my computer: content click gate fired");
            fm_handle_click(i, x, y);
        }

         

        if (i == g_theme_win_id && left_click &&
            y >= win->y + (int32_t)UI_WIN_TITLEBAR_H &&
            point_in_rect(x, y, win->x, win->y, win->w, win->h)) {
            theme_win_handle_click(i, x, y);
        }

         

        if (i == g_fmdlg_win_id && left_click &&
            y >= win->y + (int32_t)UI_WIN_TITLEBAR_H &&
            point_in_rect(x, y, win->x, win->y, win->w, win->h)) {
            fmdlg_handle_click(i, x, y);
        }

        if (left_click && point_in_rect(x, y, win->x, win->y, win->w, UI_WIN_TITLEBAR_H)) {
             
            win->dragging = true;
            win->drag_off_x = x - win->x;
            win->drag_off_y = y - win->y;
        }

        if (!left_now) win->dragging = false;

        if (win->dragging && left_now) {
            int32_t nx = x - win->drag_off_x;
            int32_t ny = y - win->drag_off_y;
            if (nx < 0) nx = 0;
            if (ny < 0) ny = 0;
            if (nx + (int32_t)win->w > (int32_t)g_fb.width)
                nx = (int32_t)g_fb.width - (int32_t)win->w;
            if (ny + (int32_t)win->h > panel_y)
                ny = panel_y - (int32_t)win->h;
            if (ny < 0) ny = 0;

            if (nx != win->x || ny != win->y) {
                uint64_t t0 = dbg_win_rdtsc();
                ui_win_move_ex(i, nx, ny, false);
                uint64_t ns = dbg_win_tsc_to_ns(dbg_win_rdtsc() - t0);
                if (ns >= 500000ULL) {    
                    DBG_VAL("DRAG", "ui_win_move_ex SLOW, ns", ns);
                    DBG_VAL("DRAG", "  win w", win->w);
                    DBG_VAL("DRAG", "  win h", win->h);
                }
            }
        }
    }

    g_prev_buttons = buttons;
}

 











static void ui_system_shutdown(void) {
    DBG_MSG("UI", "shutdown requested");

    acpi_power_off();    

    DBG_MSG("UI", "shutdown: ACPI unavailable/failed, QEMU fallback");
    __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0x604));
    __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0xB004));

    for (;;) __asm__ volatile ("hlt");
}

 












static void ui_system_reboot(void) {
    DBG_MSG("UI", "reboot requested");

    acpi_reset();         

    DBG_MSG("UI", "reboot: ACPI reset unavailable, 8042 fallback");
    for (int i = 0; i < 100; i++) {
        uint8_t status;
        __asm__ volatile ("inb %1, %0" : "=a"(status) : "Nd"((uint16_t)0x64));
        if (!(status & 0x02)) break;
    }
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64));

     
    for (volatile uint32_t i = 0; i < 10000000u; i++) { }

    DBG_MSG("UI", "reboot: 8042 failed, triple fault");
    cpu_triple_fault_reset();    
}

 


void ui_draw_desktop(void) {
    if (g_fb.mode != FB_MODE_LINEAR) return;

     



    static bool persisted_res_loaded = false;
    if (!persisted_res_loaded) {
        persisted_res_loaded = true;
        ui_load_persisted_resolution();
    }

    uint32_t w = g_fb.width;
    uint32_t h = g_fb.height;
    if (!w || !h) return;

    uint32_t panel_y = (h >= TASKBAR_H) ? h - TASKBAR_H : 0;

    DBG_MSG("UI", "draw_desktop: wallpaper...");

    bool wallpaper_ok = false;
    if (panel_y > 0) {
        wallpaper_ok = ui_load_wallpaper(panel_y);
    }
    if (!wallpaper_ok) {
        fb_fill_rect(0, 0, w, panel_y, WALLPAPER_COLOR);
        DBG_MSG("UI", "wallpaper fallback color");
    }

    fb_fill_rect(0, (int32_t)panel_y, w, TASKBAR_H, TASKBAR_BG);
    ui_draw_menu_button((int32_t)panel_y);
    ui_draw_taskbar_clock((int32_t)panel_y);
    ui_taskbar_draw_app_buttons((int32_t)panel_y);

     


    fb_draw_string(FXAPP_ICON_MARGIN, FXAPP_ICON_MARGIN,
                   "Fexox OS Desktop", FB_WHITE, FB_TRANSPARENT_BG);

     

    fxapp_ensure_assets_loaded();
    fxapp_capture_icon_bg();
    fxapp_draw_icon(g_fxapp_icon_hover);

     
    calc_capture_icon_bg();
    calc_draw_icon(g_calc_icon_hover);

     
    syslogs_capture_icon_bg();
    syslogs_draw_icon(g_syslogs_icon_hover);

     
    browser_capture_icon_bg();
    browser_draw_icon(g_browser_icon_hover);

     


    for (int i = 0; i < (int)UI_MAX_WINDOWS; i++) {
        if (g_windows[i].used && g_windows[i].open) {
            ui_win_save_bg(i, g_windows[i].x, g_windows[i].y);
            ui_win_draw(i);
            if (i == g_fxapp_win_id) fm_draw(i);
            if (i == g_theme_win_id) theme_win_draw(i);
        }
    }

    fb_flip();

    DBG_MSG("UI", "draw_desktop: done");
}
