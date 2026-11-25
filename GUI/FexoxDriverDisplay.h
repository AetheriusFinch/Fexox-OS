/* Fexox OS Project, EULA GPLv2 */
/*
 * FexoxDriverDisplay (FDD) - display layer between the GUI and the physical screen.
 *
 *   GUI / apps draw into a LOGICAL surface (any resolution up to the surface maximum).
 *   FDD presents that surface to the physical linear framebuffer (the one UEFI/GOP/VBE gave us):
 *
 *     - scaling   : nearest or bilinear filter, stretched or aspect-preserving (letterbox)
 *     - rotation  : 0 / 90 / 180 / 270 degrees
 *     - mode switch at run time, no reboot (the logical resolution is just a number;
 *       the physical mode stays whatever the firmware set)
 *     - damage tracking: the surface is cut into 32x32 tiles; every present hashes the tiles of
 *       the requested area and sends ONLY the tiles whose pixels changed since the last present.
 *       (Works for pixels written directly by user processes, too: no hints needed.)
 *     - hardware-style cursor overlay: the cursor is composited at present time on the physical
 *       screen, it never touches the logical surface (no save/restore of the background, no
 *       flicker, stays crisp and upright on scaled / rotated screens).
 *
 * The GPU does none of this, so everything is done in software (integer-only, no SSE).
 * The logical surface has a FIXED pitch and a fixed size for the whole run, so a pointer that
 * was handed to a user process stays valid when the logical resolution changes.
 */
#ifndef FEXOX_DRIVER_DISPLAY_H
#define FEXOX_DRIVER_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>
#include "Framebuffer.h"

#define FDD_ROT_0    0
#define FDD_ROT_90   1      /* clockwise */
#define FDD_ROT_180  2
#define FDD_ROT_270  3

#define FDD_FILTER_NEAREST   0
#define FDD_FILTER_BILINEAR  1

#define FDD_FIT_LETTERBOX    0      /* keep aspect ratio, black bars */
#define FDD_FIT_STRETCH      1      /* fill the whole screen */

#define FDD_MIN_W   640u
#define FDD_MIN_H   480u

#define FDD_CURSOR_MAX 64u          /* cursor image is at most 64x64 */

/* Physical side: call once from fb_init() with the firmware framebuffer. */
int  fdd_init(const fb_info_t *info, uint8_t *front_virt);   /* front_virt = mapped framebuffer */
bool fdd_ready(void);                 /* physical framebuffer known, 32 bpp */

/* Allocates the logical surface (tries 2x native capped at 3840x2160, then smaller) and
   selects logical = physical.  Returns the surface or NULL (FDD stays inactive). */
uint8_t *fdd_alloc_surface(void);
bool     fdd_active(void);            /* surface allocated and present path enabled */

uint8_t *fdd_surface(void);
uint32_t fdd_surface_pitch(void);     /* bytes, constant */
uint64_t fdd_surface_bytes(void);     /* whole allocation, constant */
void     fdd_surface_max(uint32_t *w, uint32_t *h);

/* Mode control.  Returns 0 or a negative error (mode unchanged). */
int  fdd_set_mode(uint32_t logical_w, uint32_t logical_h, int rotation, int filter, int fit);
int  fdd_set_rotation(int rotation);
int  fdd_set_filter(int filter, int fit);

uint32_t fdd_logical_w(void);
uint32_t fdd_logical_h(void);
int      fdd_rotation(void);
int      fdd_filter(void);
int      fdd_fit(void);
void     fdd_phys_size(uint32_t *w, uint32_t *h);
/* Incremented on every successful mode / rotation / filter change (apps poll or wait on it). */
uint32_t fdd_generation(void);

/* Relative pointer motion measured on the physical screen -> motion on the logical surface
   (call this on every mouse delta, otherwise the cursor moves sideways on a rotated screen). */
void fdd_rotate_delta(int32_t *dx, int32_t *dy);

/* Sends the logical surface (or a logical rectangle) to the screen.  Only tiles whose pixels
   changed since the last present are really transferred. */
void fdd_present(void);
void fdd_present_rect(int32_t x, int32_t y, uint32_t w, uint32_t h);
/* Forget what is on the screen: the next present sends everything. */
void fdd_invalidate(void);

/* Cursor overlay.  Image = RGBA bytes (r,g,b,a), w,h <= FDD_CURSOR_MAX; NULL = built-in arrow.
   Position = logical coordinates of the cursor's top-left corner (its hot spot). */
void fdd_cursor_set_image(const uint8_t *rgba, uint32_t w, uint32_t h);
void fdd_cursor_set_visible(bool visible);
void fdd_cursor_move(int32_t lx, int32_t ly);

/* Standard modes that fit into the surface, ascending.  Returns the count. */
int  fdd_standard_modes(uint32_t (*out)[2], int max);

#endif
