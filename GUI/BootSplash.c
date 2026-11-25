/* Fexox OS Project, EULA GPLv2 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "BootSplash.h"
#include "Framebuffer.h"
#include "FexoxDriverDisplay.h"
#include "VFS.h"
#include "png.h"
#include "USB.h"
#include "debug_out.h"

extern void *kmalloc(uint64_t size);
extern void  kfree(void *ptr);

/* ----------------------------------- look ---------------------------------- */
#define SPL_BAR_H       4u          /* thin */
#define SPL_GAP         28u         /* image -> bar */
#define SPL_PERIOD_MS   800u        /* one run of the segment, left to right */
#define SPL_FRAME_MS    8u          /* animation redraw rate limit (the 100 Hz timer draws every tick) */
#define SPL_TRACK_RGB   0x1E1E1Eu   /* faint rail the segment slides on (0 = none) */
#define SPL_BAR_RGB     0xFFFFFFu
#define SPL_LOG_BYTES   (48u * 1024u)
#ifndef FEXOX_SPLASH_IMG_MAX
#define FEXOX_SPLASH_IMG_MAX (8u * 1024u * 1024u)
#endif

static struct {
    bool     enabled, active, error, done;
    volatile bool busy;                       /* layout / image redraw in progress: the timer skips frames */
    uint8_t *front; uint32_t pitch, w, h;
    uint8_t  rs, gs, bs;
    /* layout */
    uint32_t img_w, img_h, img_x, img_y;
    uint32_t bar_x, bar_y, bar_w;
    uint8_t  track_cov[1024][SPL_BAR_H];      /* anti-aliased rounded rail, per pixel */
    uint64_t last_tsc, per_ms;
} S;

static char     g_ring[SPL_LOG_BYTES];
static uint32_t g_ring_len;                   /* bytes stored (<= size) */
static uint32_t g_ring_head;                  /* next write position */
static bool     g_ring_wrapped;

/* ---------------------------------- helpers -------------------------------- */
static uint64_t rdtsc_(void) { uint32_t lo, hi; __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi)); return ((uint64_t)hi << 32) | lo; }

static inline uint32_t px_rgb(uint32_t r, uint32_t g, uint32_t b) {
    return (r << S.rs) | (g << S.gs) | (b << S.bs);
}
static inline uint32_t *row_ptr(uint32_t y) { return (uint32_t *)(S.front + (uint64_t)y * S.pitch); }

static void clear_screen(void) {
    for (uint32_t y = 0; y < S.h; y++) {
        uint32_t *r = row_ptr(y);
        for (uint32_t x = 0; x < S.w; x++) r[x] = 0;
    }
}

/* coverage (0..255) of a horizontal pill at pixel (px, py), 4x4 supersampling.
   The pill starts at x0q and is wq wide, both in QUARTER pixels (smooth sub-pixel motion). */
static uint32_t pill_cov(int32_t px, int32_t py, int32_t x0q, int32_t wq, int32_t h) {
    if (px * 4 + 4 <= x0q || px * 4 >= x0q + wq) return 0;
    int32_t Wp = wq * 2, Hp = h * 8, R = h * 4, hit = 0;
    for (int sy = 0; sy < 4; sy++) {
        int32_t Y2 = 2 * (py * 4 + sy) + 1, dy = Y2 - R;
        if (Y2 < 0 || Y2 >= Hp) continue;
        for (int sx = 0; sx < 4; sx++) {
            int32_t X2 = 2 * (px * 4 + sx - x0q) + 1;
            if (X2 < 0 || X2 >= Wp) continue;
            int32_t dx = (X2 < R) ? R - X2 : (X2 > Wp - R) ? X2 - (Wp - R) : 0;
            if (dx * dx + dy * dy <= R * R) hit++;
        }
    }
    return (uint32_t)(hit * 255 / 16);
}

/* ---------------------------------- layout --------------------------------- */
static void layout(void) {
    uint32_t block = S.img_h + (S.img_h ? SPL_GAP : 0) + SPL_BAR_H;
    uint32_t top = S.h > block ? (S.h - block) / 2u : 0;
    S.img_x = (S.w - S.img_w) / 2u;
    S.img_y = top;
    S.bar_w = S.img_w ? S.img_w : 280u;
    if (S.bar_w < 200u) S.bar_w = 200u;
    if (S.bar_w > 360u) S.bar_w = 360u;
    if (S.bar_w > S.w * 6u / 10u) S.bar_w = S.w * 6u / 10u;
    if (S.bar_w > 1024u) S.bar_w = 1024u;
    S.bar_x = (S.w - S.bar_w) / 2u;
    S.bar_y = top + S.img_h + (S.img_h ? SPL_GAP : 0);
    for (uint32_t x = 0; x < S.bar_w; x++)
        for (uint32_t y = 0; y < SPL_BAR_H; y++)
            S.track_cov[x][y] = (uint8_t)pill_cov((int32_t)x, (int32_t)y, 0, (int32_t)S.bar_w * 4, (int32_t)SPL_BAR_H);
}

static void draw_bar(uint32_t pos_us) {                 /* pos_us: 0 .. SPL_PERIOD_MS*1000 */
    int32_t segw = (int32_t)S.bar_w * 35 / 100;
    int32_t travelq = ((int32_t)S.bar_w + segw) * 4;
    int32_t sxq = (int32_t)((uint64_t)pos_us * (uint64_t)travelq / (SPL_PERIOD_MS * 1000ull)) - segw * 4;
    uint32_t tr = (SPL_TRACK_RGB >> 16) & 255, tg = (SPL_TRACK_RGB >> 8) & 255, tb = SPL_TRACK_RGB & 255;
    uint32_t br = (SPL_BAR_RGB >> 16) & 255, bg = (SPL_BAR_RGB >> 8) & 255, bb = SPL_BAR_RGB & 255;
    for (uint32_t y = 0; y < SPL_BAR_H; y++) {
        uint32_t *r = row_ptr(S.bar_y + y) + S.bar_x;
        for (uint32_t x = 0; x < S.bar_w; x++) {
            uint32_t tc = S.track_cov[x][y];
            uint32_t sc = ((int32_t)x * 4 + 4 > sxq && (int32_t)x * 4 < sxq + segw * 4)
                          ? pill_cov((int32_t)x, (int32_t)y, sxq, segw * 4, (int32_t)SPL_BAR_H) : 0;
            sc = sc * tc / 255u;
            uint32_t cr = tr * tc / 255u, cg = tg * tc / 255u, cb = tb * tc / 255u;
            cr += (br - cr) * sc / 255u; cg += (bg - cg) * sc / 255u; cb += (bb - cb) * sc / 255u;
            r[x] = px_rgb(cr, cg, cb);
        }
    }
}

/* ----------------------------------- image --------------------------------- */
static void draw_image(const uint8_t *rgba, uint32_t sw, uint32_t sh) {
    for (uint32_t y = 0; y < S.img_h; y++) {
        uint32_t sy = (uint32_t)((uint64_t)y * sh / S.img_h);
        uint32_t *r = row_ptr(S.img_y + y) + S.img_x;
        for (uint32_t x = 0; x < S.img_w; x++) {
            uint32_t sxp = (uint32_t)((uint64_t)x * sw / S.img_w);
            const uint8_t *p = rgba + ((size_t)sy * sw + sxp) * 4;
            uint32_t a = p[3];                                     /* over black */
            r[x] = px_rgb(p[0] * a / 255u, p[1] * a / 255u, p[2] * a / 255u);
        }
    }
}

bool splash_set_image(const char *path) {
    if (!S.enabled || S.error || !path) return false;
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) return false;
    vfs_stat_t st;
    uint64_t cap = (vfs_fstat(fd, &st) == VFS_OK && st.size > 0) ? st.size : 1024u * 1024u;
    if (cap > FEXOX_SPLASH_IMG_MAX) cap = FEXOX_SPLASH_IMG_MAX;
    uint8_t *buf = (uint8_t *)kmalloc(cap);
    if (!buf) { vfs_close(fd); return false; }
    uint64_t total = 0;
    for (;;) {
        int64_t got = vfs_read(fd, buf + total, cap - total);
        if (got <= 0) break;
        total += (uint64_t)got;
        splash_tick();
        if (total >= cap) break;
    }
    vfs_close(fd);
    png_image_t img = {0};
    int rc = (total >= 8) ? png_decode(buf, (size_t)total, &img) : PNG_ERR_FORMAT;
    kfree(buf);
    if (rc != PNG_OK || !img.pixels || !img.width || !img.height) { DBG_MSG("SPL", "Load.png: decode failed"); return false; }

    uint32_t iw = img.width, ih = img.height;                       /* fit into 60% x 50% of the screen */
    uint32_t mw = S.w * 6u / 10u, mh = S.h * 5u / 10u;
    if (iw > mw) { ih = (uint32_t)((uint64_t)ih * mw / iw); iw = mw; }
    if (ih > mh) { iw = (uint32_t)((uint64_t)iw * mh / ih); ih = mh; }
    if (!iw) iw = 1;
    if (!ih) ih = 1;
    S.busy = true;
    S.img_w = iw; S.img_h = ih;
    layout();
    clear_screen();
    draw_image(img.pixels, img.width, img.height);
    S.busy = false;
    kfree(img.pixels);
    S.last_tsc = 0;
    splash_tick();
    DBG_VAL("SPL", "image shown, w", iw);
    return true;
}

/* --------------------------------- animation ------------------------------- */
static volatile int g_in_tick;
void splash_tick(void) {
    if (!S.active || S.busy || g_in_tick) return;       /* also called from the timer interrupt */
    g_in_tick = 1;
    if (!S.per_ms) S.per_ms = usb_tsc_per_ms();
    if (!S.per_ms) S.per_ms = 3000000ull;
    uint64_t now = rdtsc_();
    if (!S.last_tsc || now - S.last_tsc >= S.per_ms * SPL_FRAME_MS) {
        S.last_tsc = now;
        uint64_t us = (now / (S.per_ms / 1000u ? S.per_ms / 1000u : 1u)) % (SPL_PERIOD_MS * 1000ull);
        draw_bar((uint32_t)us);
    }
    g_in_tick = 0;
}

/* The 100 Hz timer interrupt drives the animation, so it keeps running smoothly while the main
   thread is busy (USB enumeration, formatting, decoding, drawing the desktop). */
extern void interrupt_set_scheduler_tick(uint64_t (*fn)(uint64_t frame_rsp));
static uint64_t splash_isr_tick(uint64_t frame_rsp) { splash_tick(); return frame_rsp; }
void splash_attach_timer(void) { if (S.enabled) interrupt_set_scheduler_tick(splash_isr_tick); }

/* ------------------------------------ log ---------------------------------- */
static void ring_put(const char *s) {
    for (; *s; s++) {
        g_ring[g_ring_head] = *s;
        g_ring_head = (g_ring_head + 1u) % SPL_LOG_BYTES;
        if (g_ring_len < SPL_LOG_BYTES) g_ring_len++; else g_ring_wrapped = true;
    }
}

static bool has(const char *s, const char *needle) {
    for (; *s; s++) {
        const char *a = s, *b = needle;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return true;
    }
    return false;
}

enum { C_NORMAL, C_OK, C_WARN, C_ERR };
static bool is_error_text(const char *s) {
    return has(s, "FATAL") || has(s, "PANIC") || has(s, "[ERR") || has(s, "ERROR") ||
           has(s, "FAIL") || has(s, "no root filesystem");
}
static int text_class(const char *s) {
    if (is_error_text(s) || has(s, "ERR")) return C_ERR;
    if (has(s, "[WARN")) return C_WARN;
    if (has(s, "[OK]")) return C_OK;
    return C_NORMAL;
}
static int g_line_class = C_NORMAL;

static void con_out(const char *s) {
    int c = text_class(s);
    if (c != C_NORMAL) g_line_class = c;
    fb_color_t fg = g_line_class == C_ERR ? 0xFF5050u : g_line_class == C_WARN ? 0xFFD040u
                  : g_line_class == C_OK ? 0x60E060u : 0xC8C8C8u;
    fbcon_set_colors(fg, FB_BLACK);
    fbcon_puts(s);
    for (const char *p = s; *p; p++) if (*p == '\n') g_line_class = C_NORMAL;
    if (fdd_active()) fb_flip();
}

static void enter_error_screen(void) {
    if (S.error) return;
    S.error = true; S.active = false;
    fb_clear(FB_BLACK);
    fbcon_init(0, 0, fb_width(), fb_height(), 0xC8C8C8u, FB_BLACK);
    fbcon_set_colors(FB_WHITE, FB_BLACK);
    fbcon_puts("=== Fexox OS Kernel ===\n");
    /* replay everything collected so far, oldest first, from a line boundary */
    uint32_t start = g_ring_wrapped ? g_ring_head : 0, n = g_ring_len;
    if (g_ring_wrapped) {                         /* skip the (cut) first line */
        while (n && g_ring[start] != '\n') { start = (start + 1u) % SPL_LOG_BYTES; n--; }
        if (n) { start = (start + 1u) % SPL_LOG_BYTES; n--; }
    }
    char line[160]; uint32_t ll = 0;
    g_line_class = C_NORMAL;
    for (uint32_t i = 0; i < n; i++) {
        char c = g_ring[(start + i) % SPL_LOG_BYTES];
        if (ll < sizeof(line) - 2) line[ll++] = c;
        if (c == '\n' || ll >= sizeof(line) - 2) { line[ll] = 0; con_out(line); ll = 0; }
    }
    if (ll) { line[ll] = 0; con_out(line); }
}

void splash_log(const char *text) {
    if (!S.enabled || !text) return;
    ring_put(text);
    if (S.error) { con_out(text); return; }
    if (S.done) {                              /* desktop is up: only a fatal line takes the screen back */
        if (has(text, "FATAL") || has(text, "PANIC")) enter_error_screen();
        return;
    }
    if (is_error_text(text)) { enter_error_screen(); return; }   /* ring already holds the text */
    splash_tick();
}

void splash_error(const char *reason) {
    if (!S.enabled) return;
    if (reason) { ring_put("[ERR] "); ring_put(reason); ring_put("\n"); }
    if (S.error) { if (reason) { con_out("[ERR] "); con_out(reason); con_out("\n"); } return; }
    enter_error_screen();
}

static void panic_hook(const char *msg) {
    if (!S.enabled) return;
    ring_put("\n*** KERNEL PANIC *** "); ring_put(msg ? msg : ""); ring_put("\n");
    if (S.error) { con_out("\n*** KERNEL PANIC *** "); con_out(msg ? msg : ""); con_out("\n"); }
    else enter_error_screen();
}

void splash_hold_errors(uint32_t ms) {
    if (!S.error) return;
    con_out("\nContinuing to the desktop...\n");
    for (uint32_t t = 0; t < ms; t += 100u) usb_delay_ms(100);
}

bool splash_enabled(void)    { return S.enabled; }
bool splash_active(void)     { return S.active; }
bool splash_error_mode(void) { return S.error; }

void splash_finish(void) {
    if (!S.enabled) return;
    S.active = false;
    S.done = true;
    S.error = false;                 /* a later panic shows the log screen again */
    fexox_os_idle_hook = 0;
}

int splash_init(void) {
    if (fb_get_mode() != FB_MODE_LINEAR || fb_bpp() != 32) return -1;
    S.front = (uint8_t *)fb_front_ptr();
    if (!S.front) return -1;
    S.w = fb_width(); S.h = fb_height(); S.pitch = fb_pitch();
    if (S.w < 320 || S.h < 240) return -1;
    fb_get_channel_shifts(&S.rs, &S.gs, &S.bs);
    S.img_w = S.img_h = 0;
    layout();
    clear_screen();
    S.enabled = S.active = true;
    S.error = false;
    fexox_os_idle_hook  = splash_tick;
    fexox_os_panic_hook = panic_hook;
    splash_tick();
    return 0;
}
