/* Fexox OS Project, EULA GPLv2 */
/*
 * FexoxDriverDisplay.c - software scaler / rotator / damage tracker / cursor overlay in front of
 * the physical framebuffer.  See FexoxDriverDisplay.h.
 * Integer maths only (the kernel is built with -mgeneral-regs-only).  Build with -O2.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "FexoxDriverDisplay.h"
#include "debug_out.h"

extern void *kmalloc(uint64_t size);
extern void  kfree(void *ptr);
extern void *memset(void *dst, int c, size_t n);

#ifdef FDD_HOST_TEST
#  define FDD_IRQ_SAVE()      0ull
#  define FDD_IRQ_RESTORE(f)  ((void)(f))
#else
static inline uint64_t fdd_irq_save(void) {
    uint64_t f; __asm__ volatile ("pushfq; pop %0; cli" : "=r"(f) :: "memory"); return f;
}
static inline void fdd_irq_restore(uint64_t f) {
    __asm__ volatile ("push %0; popfq" :: "r"(f) : "memory", "cc");
}
#  define FDD_IRQ_SAVE()      fdd_irq_save()
#  define FDD_IRQ_RESTORE(f)  fdd_irq_restore(f)
#endif

#define FDD_MAX_COLS 8192u
#define TILE 32u

typedef struct {
    /* physical */
    uint8_t *front; uint32_t pw, ph, ppitch; uint8_t bpp;
    uint8_t  rs, gs, bs;             /* channel shifts of the native pixel format */
    bool     phys_ok;
    /* logical surface (fixed for the whole run) */
    uint8_t *surf; uint32_t spitch, smax_w, smax_h; uint64_t sbytes;
    bool     active;
    /* damage tracking */
    uint64_t *thash; uint8_t *tvalid; uint32_t ntx, nty;
    /* current mode */
    uint32_t lw, lh; int rot, filter, fit;
    uint32_t gen;
    /* derived */
    uint32_t rw, rh;                 /* logical size after rotation */
    uint32_t vx, vy, vw, vh;         /* viewport on the physical screen */
    int64_t  base, step_u, step_v;   /* byte offsets: rotated (u,v) -> surface */
    bool     identity;
} fdd_t;

static fdd_t g;

static int32_t col_o0[FDD_MAX_COLS];     /* u0 * step_u */
static int32_t col_o1[FDD_MAX_COLS];     /* u1 * step_u */
static uint8_t col_w[FDD_MAX_COLS];      /* 0..255 weight of u1 */

/* ----------------------------- small helpers ------------------------------ */
static void copy_nt(void *dst, const void *src, size_t n) {
    uint64_t *d = (uint64_t *)dst; const uint64_t *s = (const uint64_t *)src;
    size_t c = n / 8;
    for (size_t i = 0; i < c; i++) __asm__ volatile ("movnti %1, %0" : "=m"(d[i]) : "r"(s[i]));
    __asm__ volatile ("sfence" ::: "memory");
    size_t t = n & 7u;
    if (t) { uint8_t *db = (uint8_t *)(d + c); const uint8_t *sb = (const uint8_t *)(s + c); for (size_t i = 0; i < t; i++) db[i] = sb[i]; }
}

/* blends 4 byte lanes at once: result = a*(256-w)/256 + b*w/256, w in 0..255 */
static inline uint32_t blend(uint32_t a, uint32_t b, uint32_t w) {
    uint32_t aw = 256u - w;
    uint32_t lo = ((a & 0x00FF00FFu) * aw + (b & 0x00FF00FFu) * w) >> 8;
    uint32_t hi = (((a >> 8) & 0x00FF00FFu) * aw + ((b >> 8) & 0x00FF00FFu) * w) >> 8;
    return (lo & 0x00FF00FFu) | ((hi & 0x00FF00FFu) << 8);
}

static uint8_t mask_shift(uint32_t m, uint8_t dflt) {
    if (!m) return dflt;
    uint8_t s = 0; while (!(m & 1u)) { m >>= 1; s++; } return s;
}

/* ------------------------------ geometry ---------------------------------- */
static int derive(fdd_t *n, bool commit) {
    bool swap = (n->rot == FDD_ROT_90 || n->rot == FDD_ROT_270);
    n->rw = swap ? n->lh : n->lw;
    n->rh = swap ? n->lw : n->lh;

    int64_t P = n->spitch;
    switch (n->rot) {
    default:
    case FDD_ROT_0:   n->base = 0;                                       n->step_u = 4;  n->step_v = P;  break;
    case FDD_ROT_90:  n->base = (int64_t)(n->lh - 1) * P;                n->step_u = -P; n->step_v = 4;  break;
    case FDD_ROT_180: n->base = (int64_t)(n->lw - 1) * 4 + (int64_t)(n->lh - 1) * P; n->step_u = -4; n->step_v = -P; break;
    case FDD_ROT_270: n->base = (int64_t)(n->lw - 1) * 4;                n->step_u = P;  n->step_v = -4; break;
    }

    if (n->fit == FDD_FIT_STRETCH) {
        n->vx = 0; n->vy = 0; n->vw = n->pw; n->vh = n->ph;
    } else {
        if ((uint64_t)n->pw * n->rh <= (uint64_t)n->ph * n->rw) {
            n->vw = n->pw; n->vh = (uint32_t)((uint64_t)n->rh * n->pw / n->rw);
        } else {
            n->vh = n->ph; n->vw = (uint32_t)((uint64_t)n->rw * n->ph / n->rh);
        }
        if (n->vw < 1) n->vw = 1;
        if (n->vh < 1) n->vh = 1;
        n->vx = (n->pw - n->vw) / 2u; n->vy = (n->ph - n->vh) / 2u;
    }
    n->identity = (n->rot == FDD_ROT_0 && n->rw == n->pw && n->rh == n->ph &&
                   n->vw == n->pw && n->vh == n->ph);
    if (n->identity) return 0;
    if (n->bpp != 32) return -4;                       /* scaling/rotation need 32 bpp */
    if (n->vw > FDD_MAX_COLS) return -5;

    if (commit) {
        for (uint32_t x = 0; x < n->vw; x++) {
            if (n->filter == FDD_FILTER_BILINEAR) {
                int64_t u = (int64_t)(((uint64_t)(2u * x + 1u) * n->rw * 256u) / (2ull * n->vw)) - 128;
                if (u < 0) u = 0;
                uint32_t iu0 = (uint32_t)(u >> 8), fu = (uint32_t)(u & 255);
                if (iu0 >= n->rw - 1u) { iu0 = n->rw - 1u; fu = 0; }
                uint32_t iu1 = (iu0 + 1u < n->rw) ? iu0 + 1u : iu0;
                col_o0[x] = (int32_t)((int64_t)iu0 * n->step_u);
                col_o1[x] = (int32_t)((int64_t)iu1 * n->step_u);
                col_w[x]  = (uint8_t)fu;
            } else {
                uint32_t iu = (uint32_t)(((uint64_t)(2u * x + 1u) * n->rw) / (2ull * n->vw));
                if (iu >= n->rw) iu = n->rw - 1u;
                col_o0[x] = (int32_t)((int64_t)iu * n->step_u);
                col_o1[x] = col_o0[x];
                col_w[x]  = 0;
            }
        }
    }
    return 0;
}

static void clear_front(void) {
    if (!g.front) return;
    for (uint32_t y = 0; y < g.ph; y++) memset(g.front + (uint64_t)y * g.ppitch, 0, (size_t)g.pw * (g.bpp / 8u));
}

/* ------------------------------- cursor state ------------------------------ */
static struct {
    bool     vis, have;
    uint32_t w, h;
    uint8_t  rgba[FDD_CURSOR_MAX * FDD_CURSOR_MAX * 4];
    int32_t  lx, ly;                 /* logical position of the hot spot (top-left corner) */
    bool     drawn;                  /* currently composited on the screen */
    int32_t  dx0, dy0, dx1, dy1;     /* screen rectangle it occupies while drawn */
} cur = { .vis = true, .lx = 100, .ly = 100 };

static const char k_arrow[19][13] = {
    "#           ", "##          ", "#.#         ", "#..#        ", "#...#       ", "#....#      ",
    "#.....#     ", "#......#    ", "#.......#   ", "#........#  ", "#.....#####", "#...#       ",
    "#..##       ", "#.#.#       ", "#. #.#      ", "#   #.#     ", "    #.#     ", "     #      ", "            " };

static void cursor_default_image(void) {
    cur.w = 12; cur.h = 19;
    for (uint32_t y = 0; y < cur.h; y++)
        for (uint32_t x = 0; x < cur.w; x++) {
            char c = k_arrow[y][x];
            uint8_t *p = cur.rgba + ((size_t)y * cur.w + x) * 4;
            p[0] = p[1] = p[2] = (c == '.') ? 255 : 0;
            p[3] = (c == '#' || c == '.') ? 255 : 0;
        }
    cur.have = true;
}

/* ------------------------------ scene rendering ---------------------------- */
/* Renders the destination rectangle [dx0,dx1) x [dy0,dy1) (inside the viewport) of the scaled /
   rotated scene into `dst` (pixel (dx0,dy0) is at `dst`, rows `dstride` bytes apart). */
static void blit_core(int64_t dx0, int64_t dy0, int64_t dx1, int64_t dy1, uint8_t *dst, size_t dstride) {
    if (g.identity) {
        for (int64_t y = dy0; y < dy1; y++)
            copy_nt(dst + (size_t)(y - dy0) * dstride,
                    g.surf + (uint64_t)y * g.spitch + (uint64_t)dx0 * 4u, (size_t)(dx1 - dx0) * 4u);
        return;
    }
    const uint8_t *sb = g.surf + g.base;
    const bool bil = (g.filter == FDD_FILTER_BILINEAR);
    for (int64_t dy = dy0; dy < dy1; dy++) {
        uint32_t ry = (uint32_t)(dy - g.vy), fv = 0;
        int64_t ov0, ov1;
        if (bil) {
            int64_t v = (int64_t)(((uint64_t)(2u * ry + 1u) * g.rh * 256u) / (2ull * g.vh)) - 128;
            if (v < 0) v = 0;
            uint32_t iv0 = (uint32_t)(v >> 8); fv = (uint32_t)(v & 255);
            if (iv0 >= g.rh - 1u) { iv0 = g.rh - 1u; fv = 0; }
            uint32_t iv1 = (iv0 + 1u < g.rh) ? iv0 + 1u : iv0;
            ov0 = (int64_t)iv0 * g.step_v; ov1 = (int64_t)iv1 * g.step_v;
        } else {
            uint32_t iv = (uint32_t)(((uint64_t)(2u * ry + 1u) * g.rh) / (2ull * g.vh));
            if (iv >= g.rh) iv = g.rh - 1u;
            ov0 = ov1 = (int64_t)iv * g.step_v;
        }
        uint32_t *d = (uint32_t *)(dst + (size_t)(dy - dy0) * dstride);
        const uint8_t *r0 = sb + ov0, *r1 = sb + ov1;
        for (int64_t dx = dx0; dx < dx1; dx++, d++) {
            uint32_t c = (uint32_t)(dx - g.vx);
            uint32_t p00 = *(const uint32_t *)(r0 + col_o0[c]);
            if (!bil) { *d = p00; continue; }
            uint32_t fu = col_w[c];
            if (!(fu | fv)) { *d = p00; continue; }
            uint32_t p10 = *(const uint32_t *)(r0 + col_o1[c]);
            uint32_t top = fu ? blend(p00, p10, fu) : p00;
            if (!fv) { *d = top; continue; }
            uint32_t p01 = *(const uint32_t *)(r1 + col_o0[c]);
            uint32_t p11 = *(const uint32_t *)(r1 + col_o1[c]);
            uint32_t bot = fu ? blend(p01, p11, fu) : p01;
            *d = blend(top, bot, fv);
        }
    }
}

/* ------------------------------ cursor overlay ----------------------------- */
static uint32_t cur_tmp[FDD_CURSOR_MAX * FDD_CURSOR_MAX];

static void map_point(int32_t lx, int32_t ly, int32_t *px, int32_t *py) {
    if (lx < 0) lx = 0;
    if (ly < 0) ly = 0;
    if ((uint32_t)lx >= g.lw) lx = (int32_t)g.lw - 1;
    if ((uint32_t)ly >= g.lh) ly = (int32_t)g.lh - 1;
    uint32_t u, v;
    switch (g.rot) {
    default:
    case FDD_ROT_0:   u = (uint32_t)lx;               v = (uint32_t)ly;               break;
    case FDD_ROT_90:  u = g.lh - 1u - (uint32_t)ly;   v = (uint32_t)lx;               break;
    case FDD_ROT_180: u = g.lw - 1u - (uint32_t)lx;   v = g.lh - 1u - (uint32_t)ly;   break;
    case FDD_ROT_270: u = (uint32_t)ly;               v = g.lw - 1u - (uint32_t)lx;   break;
    }
    *px = (int32_t)(g.vx + (uint32_t)((uint64_t)u * g.vw / g.rw));
    *py = (int32_t)(g.vy + (uint32_t)((uint64_t)v * g.vh / g.rh));
}

/* screen rectangle of the cursor (clipped to the screen); returns false when nothing is visible */
static bool cursor_rect(int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1) {
    int32_t px, py;
    map_point(cur.lx, cur.ly, &px, &py);
    int32_t ax0 = px, ay0 = py, ax1 = px + (int32_t)cur.w, ay1 = py + (int32_t)cur.h;
    if (ax1 > (int32_t)g.pw) ax1 = (int32_t)g.pw;
    if (ay1 > (int32_t)g.ph) ay1 = (int32_t)g.ph;
    if (ax0 >= ax1 || ay0 >= ay1) return false;
    *x0 = ax0; *y0 = ay0; *x1 = ax1; *y1 = ay1;
    return true;
}

/* Paints the screen rectangle: scene underneath (black outside the viewport), optionally the
   cursor on top.  Composited in a small RAM buffer, so the (slow) framebuffer is never read. */
static void paint_rect(int32_t x0, int32_t y0, int32_t x1, int32_t y1, bool with_cursor) {
    uint32_t w = (uint32_t)(x1 - x0), h = (uint32_t)(y1 - y0);
    for (uint32_t i = 0; i < w * h; i++) cur_tmp[i] = 0;
    int32_t ix0 = x0 > (int32_t)g.vx ? x0 : (int32_t)g.vx;
    int32_t iy0 = y0 > (int32_t)g.vy ? y0 : (int32_t)g.vy;
    int32_t ix1 = x1 < (int32_t)(g.vx + g.vw) ? x1 : (int32_t)(g.vx + g.vw);
    int32_t iy1 = y1 < (int32_t)(g.vy + g.vh) ? y1 : (int32_t)(g.vy + g.vh);
    if (ix0 < ix1 && iy0 < iy1)
        blit_core(ix0, iy0, ix1, iy1,
                  (uint8_t *)cur_tmp + ((size_t)(iy0 - y0) * w + (size_t)(ix0 - x0)) * 4, (size_t)w * 4);
    if (with_cursor) {
        int32_t px, py;
        map_point(cur.lx, cur.ly, &px, &py);
        for (int32_t y = y0; y < y1; y++) {
            for (int32_t x = x0; x < x1; x++) {
                const uint8_t *s = cur.rgba + ((size_t)(y - py) * cur.w + (size_t)(x - px)) * 4;
                uint32_t a = s[3];
                if (!a) continue;
                uint32_t src = ((uint32_t)s[0] << g.rs) | ((uint32_t)s[1] << g.gs) | ((uint32_t)s[2] << g.bs);
                uint32_t *d = &cur_tmp[(size_t)(y - y0) * w + (size_t)(x - x0)];
                if (a == 255) { *d = src; continue; }
                uint32_t bg = *d, out = 0;
                for (int k = 0; k < 3; k++) {
                    uint8_t sh = (k == 0) ? g.rs : (k == 1) ? g.gs : g.bs;
                    uint32_t sc = (src >> sh) & 255u, dc = (bg >> sh) & 255u;
                    out |= ((sc * a + dc * (255u - a) + 127u) / 255u) << sh;
                }
                *d = out | (bg & ~((255u << g.rs) | (255u << g.gs) | (255u << g.bs)));
            }
        }
    }
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++)
            ((uint32_t *)(g.front + (uint64_t)(y0 + (int32_t)y) * g.ppitch))[x0 + (int32_t)x] = cur_tmp[y * w + x];
}

static void cursor_erase(void) {
    if (!cur.drawn) return;
    paint_rect(cur.dx0, cur.dy0, cur.dx1, cur.dy1, false);
    cur.drawn = false;
}
static void cursor_draw(void) {
    int32_t x0, y0, x1, y1;
    if (!cur.vis || !cur.have || !cursor_rect(&x0, &y0, &x1, &y1)) return;
    paint_rect(x0, y0, x1, y1, true);
    cur.drawn = true; cur.dx0 = x0; cur.dy0 = y0; cur.dx1 = x1; cur.dy1 = y1;
}

/* a scene blit touched screen rectangle D: if it hit the cursor, put the cursor back */
static void cursor_fix(int64_t dx0, int64_t dy0, int64_t dx1, int64_t dy1) {
    if (!cur.vis || !cur.have) return;
    int32_t x0, y0, x1, y1;
    if (!cursor_rect(&x0, &y0, &x1, &y1)) return;
    if (dx1 <= x0 || dx0 >= x1 || dy1 <= y0 || dy0 >= y1) return;
    if (cur.drawn) paint_rect(cur.dx0, cur.dy0, cur.dx1, cur.dy1, false);   /* wipe leftovers */
    cur.drawn = false;
    cursor_draw();
}

/* ------------------------------ mode changes ------------------------------- */
static int recompute(uint32_t lw, uint32_t lh, int rot, int filter, int fit, bool commit) {
    fdd_t n = g;
    n.lw = lw; n.lh = lh; n.rot = rot; n.filter = filter; n.fit = fit;
    int rc = derive(&n, false);                        /* validate first, tables untouched */
    if (rc != 0) return rc;
    uint64_t fl = FDD_IRQ_SAVE();
    rc = derive(&n, true);
    if (rc == 0) {
        g = n;
        if (g.tvalid) memset(g.tvalid, 0, (size_t)g.ntx * g.nty);
        cur.drawn = false;
        if (commit) { clear_front(); g.gen++; }
    }
    FDD_IRQ_RESTORE(fl);
    return rc;
}

/* ------------------------------- public API ------------------------------- */
int fdd_init(const fb_info_t *info, uint8_t *front_virt) {
    g = (fdd_t){0};
    if (!info || info->mode != FB_MODE_LINEAR || !front_virt || !info->width || !info->height) return -1;
    g.front = front_virt; g.pw = info->width; g.ph = info->height;
    g.ppitch = info->pitch ? info->pitch : info->width * (info->bpp / 8u);
    g.bpp = info->bpp ? info->bpp : 32;
    g.rs = mask_shift(info->red_mask, 16);
    g.gs = mask_shift(info->green_mask, 8);
    g.bs = mask_shift(info->blue_mask, 0);
    g.phys_ok = true;
    g.gen = 1;
    DBG_VAL("FDD", "physical w", g.pw);
    DBG_VAL("FDD", "physical h", g.ph);
    DBG_VAL("FDD", "physical bpp", g.bpp);
    return 0;
}
bool fdd_ready(void)  { return g.phys_ok && g.bpp == 32; }
bool fdd_active(void) { return g.active; }

uint8_t *fdd_alloc_surface(void) {
    if (!fdd_ready()) { DBG_MSG("FDD", "no 32 bpp physical framebuffer - layer inactive"); return 0; }
    if (g.surf) return g.surf;

    uint32_t w2 = g.pw * 2u; if (w2 > 3840u) w2 = 3840u; if (w2 < g.pw) w2 = g.pw;
    uint32_t h2 = g.ph * 2u; if (h2 > 2160u) h2 = 2160u; if (h2 < g.ph) h2 = g.ph;
    uint32_t tries[2][2] = { { w2, h2 }, { g.pw, g.ph } };
    for (int t = 0; t < 2; t++) {
        uint32_t mw = tries[t][0], mh = tries[t][1];
        uint64_t bytes = (uint64_t)mw * 4u * mh;
        uint8_t *p = (uint8_t *)kmalloc(bytes);
        if (!p) { DBG_VAL("FDD", "surface kmalloc failed, bytes", bytes); continue; }
        memset(p, 0, (size_t)bytes);
        g.surf = p; g.spitch = mw * 4u; g.smax_w = mw; g.smax_h = mh; g.sbytes = bytes;

        g.ntx = (mw + TILE - 1u) / TILE; g.nty = (mh + TILE - 1u) / TILE;
        g.thash  = (uint64_t *)kmalloc((uint64_t)g.ntx * g.nty * 8u);
        g.tvalid = (uint8_t *)kmalloc((uint64_t)g.ntx * g.nty);
        if (!g.thash || !g.tvalid) {                   /* no damage tracking: every present is full */
            if (g.thash) kfree(g.thash);
            if (g.tvalid) kfree(g.tvalid);
            g.thash = 0; g.tvalid = 0;
            DBG_MSG("FDD", "no tile memory - damage tracking off");
        } else memset(g.tvalid, 0, (size_t)g.ntx * g.nty);

        DBG_VAL("FDD", "surface max w", mw);
        DBG_VAL("FDD", "surface max h", mh);
        if (recompute(g.pw, g.ph, FDD_ROT_0, FDD_FILTER_BILINEAR, FDD_FIT_LETTERBOX, false) != 0) {
            DBG_MSG("FDD", "initial mode failed");
            kfree(p); g.surf = 0; return 0;
        }
        cursor_default_image();
        g.active = true;
        return p;
    }
    return 0;
}

uint8_t *fdd_surface(void)        { return g.surf; }
uint32_t fdd_surface_pitch(void)  { return g.spitch; }
uint64_t fdd_surface_bytes(void)  { return g.sbytes; }
void     fdd_surface_max(uint32_t *w, uint32_t *h) { if (w) *w = g.smax_w; if (h) *h = g.smax_h; }
uint32_t fdd_logical_w(void)      { return g.lw; }
uint32_t fdd_logical_h(void)      { return g.lh; }
int      fdd_rotation(void)       { return g.rot; }
int      fdd_filter(void)         { return g.filter; }
int      fdd_fit(void)            { return g.fit; }
uint32_t fdd_generation(void)     { return g.gen; }
void     fdd_phys_size(uint32_t *w, uint32_t *h) { if (w) *w = g.pw; if (h) *h = g.ph; }

int fdd_set_mode(uint32_t lw, uint32_t lh, int rot, int filter, int fit) {
    if (!g.active) return -1;
    if (lw < FDD_MIN_W || lh < FDD_MIN_H || lw > g.smax_w || lh > g.smax_h) return -2;
    if (rot < 0 || rot > 3 || (filter != 0 && filter != 1) || (fit != 0 && fit != 1)) return -2;
    int rc = recompute(lw, lh, rot, filter, fit, true);
    if (rc == 0) {
        DBG_VAL("FDD", "mode w", lw); DBG_VAL("FDD", "mode h", lh);
        DBG_VAL("FDD", "rotation", (uint64_t)rot); DBG_VAL("FDD", "filter", (uint64_t)filter);
    } else DBG_VAL("FDD", "set_mode failed, rc", (uint64_t)(int64_t)rc);
    return rc;
}
void fdd_rotate_delta(int32_t *dx, int32_t *dy) {
    int32_t x = *dx, y = *dy;
    switch (g.rot) {
    case FDD_ROT_90:  *dx = y;  *dy = -x; break;
    case FDD_ROT_180: *dx = -x; *dy = -y; break;
    case FDD_ROT_270: *dx = -y; *dy = x;  break;
    default: break;
    }
}
int fdd_set_rotation(int rot)           { return fdd_set_mode(g.lw, g.lh, rot, g.filter, g.fit); }
int fdd_set_filter(int filter, int fit) { return fdd_set_mode(g.lw, g.lh, g.rot, filter, fit); }

int fdd_standard_modes(uint32_t (*out)[2], int max) {
    static const uint32_t m[][2] = {
        {640,480},{800,600},{1024,768},{1280,720},{1280,800},{1366,768},{1440,900},
        {1600,900},{1680,1050},{1920,1080},{2560,1440},{3840,2160} };
    int n = 0;
    for (unsigned i = 0; i < sizeof(m) / sizeof(m[0]) && n < max; i++)
        if (m[i][0] <= g.smax_w && m[i][1] <= g.smax_h) { out[n][0] = m[i][0]; out[n][1] = m[i][1]; n++; }
    return n;
}

/* -------------------------------- presenting ------------------------------ */
/* sends one logical rectangle (already known to differ from the screen) */
static void emit_logical(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    int64_t dx0, dy0, dx1, dy1;
    if (g.identity) {
        dx0 = x; dy0 = y; dx1 = (int64_t)x + w; dy1 = (int64_t)y + h;
    } else {
        uint32_t lw = g.lw, lh = g.lh, u0, u1, v0, v1;
        switch (g.rot) {
        default:
        case FDD_ROT_0:   u0 = x;            u1 = x + w;        v0 = y;            v1 = y + h; break;
        case FDD_ROT_90:  u0 = lh - (y + h); u1 = lh - y;       v0 = x;            v1 = x + w; break;
        case FDD_ROT_180: u0 = lw - (x + w); u1 = lw - x;       v0 = lh - (y + h); v1 = lh - y; break;
        case FDD_ROT_270: u0 = y;            u1 = y + h;        v0 = lw - (x + w); v1 = lw - x; break;
        }
        int64_t mx = (int64_t)(g.vw / g.rw) + 2, my = (int64_t)(g.vh / g.rh) + 2;   /* filter footprint */
        dx0 = (int64_t)g.vx + (int64_t)((uint64_t)u0 * g.vw / g.rw) - mx;
        dx1 = (int64_t)g.vx + (int64_t)(((uint64_t)u1 * g.vw + g.rw - 1) / g.rw) + mx;
        dy0 = (int64_t)g.vy + (int64_t)((uint64_t)v0 * g.vh / g.rh) - my;
        dy1 = (int64_t)g.vy + (int64_t)(((uint64_t)v1 * g.vh + g.rh - 1) / g.rh) + my;
        if (dx0 < (int64_t)g.vx) dx0 = g.vx;
        if (dy0 < (int64_t)g.vy) dy0 = g.vy;
        if (dx1 > (int64_t)(g.vx + g.vw)) dx1 = g.vx + g.vw;
        if (dy1 > (int64_t)(g.vy + g.vh)) dy1 = g.vy + g.vh;
    }
    if (dx0 >= dx1 || dy0 >= dy1) return;
    uint64_t fl = FDD_IRQ_SAVE();
    blit_core(dx0, dy0, dx1, dy1, g.front + (uint64_t)dy0 * g.ppitch + (uint64_t)dx0 * 4u, g.ppitch);
    cursor_fix(dx0, dy0, dx1, dy1);
    FDD_IRQ_RESTORE(fl);
}

static inline uint64_t rol64(uint64_t v, int r) { return (v << r) | (v >> (64 - r)); }

static uint64_t tile_hash(uint32_t tx, uint32_t ty) {
    uint32_t x0 = tx * TILE, y0 = ty * TILE;
    uint32_t w = g.lw - x0; if (w > TILE) w = TILE;
    uint32_t h = g.lh - y0; if (h > TILE) h = TILE;
    uint64_t a = 0x9E3779B97F4A7C15ull;
    for (uint32_t r = 0; r < h; r++) {
        const uint64_t *p = (const uint64_t *)(g.surf + (uint64_t)(y0 + r) * g.spitch + (uint64_t)x0 * 4u);
        uint32_t n = w / 2u;
        for (uint32_t i = 0; i < n; i++) a = rol64((a ^ p[i]) * 0xC2B2AE3D27D4EB4Full, 29);
        if (w & 1u) a = rol64((a ^ ((const uint32_t *)p)[w - 1u]) * 0xC2B2AE3D27D4EB4Full, 29);
    }
    return a;
}

void fdd_present_rect(int32_t x, int32_t y, uint32_t w, uint32_t h) {
    if (!g.active || !w || !h) return;
    if (x < 0) { if ((uint32_t)(-x) >= w) return; w += (uint32_t)x; x = 0; }
    if (y < 0) { if ((uint32_t)(-y) >= h) return; h += (uint32_t)y; y = 0; }
    if ((uint32_t)x >= g.lw || (uint32_t)y >= g.lh) return;
    if ((uint64_t)x + w > g.lw) w = g.lw - (uint32_t)x;
    if ((uint64_t)y + h > g.lh) h = g.lh - (uint32_t)y;

    if (!g.thash) { emit_logical((uint32_t)x, (uint32_t)y, w, h); return; }

    uint32_t tx0 = (uint32_t)x / TILE, tx1 = ((uint32_t)x + w - 1u) / TILE;
    uint32_t ty0 = (uint32_t)y / TILE, ty1 = ((uint32_t)y + h - 1u) / TILE;
    for (uint32_t ty = ty0; ty <= ty1; ty++) {
        int64_t run = -1;
        for (uint32_t tx = tx0; tx <= tx1 + 1u; tx++) {
            bool changed = false;
            if (tx <= tx1) {
                uint64_t hv = tile_hash(tx, ty);
                uint32_t idx = ty * g.ntx + tx;
                if (!(g.tvalid[idx] && g.thash[idx] == hv)) { g.thash[idx] = hv; g.tvalid[idx] = 1; changed = true; }
            }
            if (changed) { if (run < 0) run = tx; }
            else if (run >= 0) {                       /* flush the run of changed tiles */
                uint32_t rx = (uint32_t)run * TILE, ry = ty * TILE;
                uint32_t rw = tx * TILE - rx; if (rx + rw > g.lw) rw = g.lw - rx;
                uint32_t rh = TILE;           if (ry + rh > g.lh) rh = g.lh - ry;
                emit_logical(rx, ry, rw, rh);
                run = -1;
            }
        }
    }
}

void fdd_present(void) { fdd_present_rect(0, 0, g.lw, g.lh); }

void fdd_invalidate(void) {
    if (!g.active) return;
    uint64_t fl = FDD_IRQ_SAVE();
    if (g.tvalid) memset(g.tvalid, 0, (size_t)g.ntx * g.nty);
    cur.drawn = false;
    FDD_IRQ_RESTORE(fl);
}

/* ---------------------------------- cursor API ---------------------------- */
void fdd_cursor_set_image(const uint8_t *rgba, uint32_t w, uint32_t h) {
    if (!g.active) return;
    uint64_t fl = FDD_IRQ_SAVE();
    cursor_erase();
    if (rgba && w && h && w <= FDD_CURSOR_MAX && h <= FDD_CURSOR_MAX) {
        for (size_t i = 0; i < (size_t)w * h * 4; i++) cur.rgba[i] = rgba[i];
        cur.w = w; cur.h = h; cur.have = true;
    } else cursor_default_image();
    cursor_draw();
    FDD_IRQ_RESTORE(fl);
}
void fdd_cursor_set_visible(bool v) {
    if (!g.active || cur.vis == v) return;
    uint64_t fl = FDD_IRQ_SAVE();
    cur.vis = v;
    if (v) cursor_draw(); else cursor_erase();
    FDD_IRQ_RESTORE(fl);
}
void fdd_cursor_move(int32_t lx, int32_t ly) {
    if (!g.active) { cur.lx = lx; cur.ly = ly; return; }
    if (cur.drawn && lx == cur.lx && ly == cur.ly) return;
    uint64_t fl = FDD_IRQ_SAVE();
    cursor_erase();
    cur.lx = lx; cur.ly = ly;
    cursor_draw();
    FDD_IRQ_RESTORE(fl);
}
