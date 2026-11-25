/* Fexox OS Project, EULA GPLv2 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "jpeg.h"
#include "debug_out.h"

extern void *kmalloc(uint64_t size);
extern void  kfree(void *ptr);
extern void *memset(void *dst, int c, size_t n);

/* ------------------------------ constants --------------------------------- */
static const uint8_t k_zz[64] = {      /* zigzag index -> natural (row * 8 + col) */
     0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };

/* A[x][u] = 0.5 * C(u) * cos((2x+1) u pi / 2N) * 4096,  C(0) = 1/sqrt(2)
   f(x,y) = sum_u sum_v A[x][u] A[y][v] F(u,v):  an N-point IDCT of the NxN low frequencies of an
   8x8 block gives that block shrunk to N x N pixels. */
static const int16_t k_idct8[8][8] = {
    {1448, 2009, 1892, 1703, 1448, 1138, 784, 400},
    {1448, 1703, 784, -400, -1448, -2009, -1892, -1138},
    {1448, 1138, -784, -2009, -1448, 400, 1892, 1703},
    {1448, 400, -1892, -1138, 1448, 1703, -784, -2009},
    {1448, -400, -1892, 1138, 1448, -1703, -784, 2009},
    {1448, -1138, -784, 2009, -1448, -400, 1892, -1703},
    {1448, -1703, 784, 400, -1448, 2009, -1892, 1138},
    {1448, -2009, 1892, -1703, 1448, -1138, 784, -400}
};
static const int16_t k_idct4[4][4] = {
    {1448, 1892, 1448, 784}, {1448, 784, -1448, -1892}, {1448, -784, -1448, 1892}, {1448, -1892, 1448, -784}
};
static const int16_t k_idct2[2][2] = { {1448, 1448}, {1448, -1448} };
static const int16_t k_idct1[1][1] = { {1448} };

/* ------------------------------- structures ------------------------------- */
typedef struct {
    uint8_t  bits[17];
    uint8_t  vals[256];
    uint16_t look[512];                /* 9-bit fast table: (len << 8) | symbol, 0 = long code */
    int32_t  maxcode[18];
    int32_t  mincode[17];
    int32_t  valptr[17];
    bool     present;
} huff_t;

typedef struct {
    uint8_t  id, h, v, tq, td, ta;
    uint32_t bw, bh;                   /* blocks incl. MCU padding */
    uint32_t cw, ch;                   /* real blocks (non-interleaved scans) */
    uint8_t *plane; uint32_t pw, ph;   /* decoded samples at the chosen scale */
    int16_t *coef;  uint64_t *mask;    /* progressive: low NxN coefficients + "non-zero" bitmap */
    int      dcpred;
} comp_t;

typedef struct {
    jpeg_read_fn rd; void *ctx;
    uint8_t  *buf; uint32_t bpos, blen; bool eof;
    uint32_t bitbuf; int nbits; int marker;
    uint32_t eobrun;
    huff_t   dc[4], ac[4];
    uint16_t qt[4][64];                /* natural order */
    uint32_t W, H, mcux, mcuy; int hmax, vmax, ncomp, N;
    uint32_t want_w, want_h;
    bool     progressive, have_sof, adobe, adobe_rgb;
    uint32_t restart_interval;
    comp_t   c[3];
    int      scans;
} dec_t;

/* ------------------------------ byte / bit input -------------------------- */
static int rb(dec_t *d) {
    if (d->bpos >= d->blen) {
        if (d->eof) return -1;
        int n = d->rd(d->ctx, d->buf, 65536u);
        if (n <= 0) { d->eof = true; return -1; }
        d->blen = (uint32_t)n; d->bpos = 0;
    }
    return d->buf[d->bpos++];
}
static int rb16(dec_t *d) { int a = rb(d), b = rb(d); return (a < 0 || b < 0) ? -1 : (a << 8) | b; }

static int next_marker(dec_t *d) {
    if (d->marker >= 0) { int m = d->marker; d->marker = -1; return m; }
    for (;;) {
        int b = rb(d);
        if (b < 0) return -1;
        if (b != 0xFF) continue;
        do { b = rb(d); } while (b == 0xFF);
        if (b < 0) return -1;
        if (b == 0) continue;
        return b;
    }
}

static uint32_t next_data_byte(dec_t *d) {
    if (d->marker >= 0) return 0;
    int b = rb(d);
    if (b < 0) return 0;
    if (b != 0xFF) return (uint32_t)b;
    int b2;
    do { b2 = rb(d); } while (b2 == 0xFF);
    if (b2 == 0) return 0xFFu;                  /* stuffed FF */
    if (b2 < 0) return 0;
    d->marker = b2;                             /* real marker: feed zeros from here on */
    return 0;
}

static inline void fill(dec_t *d) {
    while (d->nbits <= 24) { d->bitbuf = (d->bitbuf << 8) | next_data_byte(d); d->nbits += 8; }
}
static inline uint32_t getbits(dec_t *d, int n) {
    if (!n) return 0;
    fill(d);
    d->nbits -= n;
    return (d->bitbuf >> d->nbits) & ((1u << n) - 1u);
}
static inline int extend(uint32_t v, int s) { return (int)v < (1 << (s - 1)) ? (int)v - (1 << s) + 1 : (int)v; }

static int huff_decode(dec_t *d, const huff_t *h) {
    fill(d);
    uint32_t look = (d->bitbuf >> (d->nbits - 9)) & 511u;
    uint16_t e = h->look[look];
    if (e) { d->nbits -= (e >> 8); return e & 255; }
    for (int l = 10; l <= 16; l++) {
        int32_t code = (int32_t)((d->bitbuf >> (d->nbits - l)) & ((1u << l) - 1u));
        if (h->maxcode[l] >= 0 && code <= h->maxcode[l] && code >= h->mincode[l]) {
            d->nbits -= l;
            return h->vals[(h->valptr[l] + code - h->mincode[l]) & 255];
        }
    }
    d->nbits -= 16;                             /* corrupt stream */
    return 0;
}

static void huff_build(huff_t *h) {
    int code = 0, k = 0;
    for (int i = 0; i < 512; i++) h->look[i] = 0;
    for (int l = 1; l <= 16; l++) {
        h->valptr[l] = k;
        h->mincode[l] = code;
        for (int i = 0; i < h->bits[l]; i++, k++, code++) {
            if (l <= 9) {
                int base = code << (9 - l);
                for (int f = 0; f < (1 << (9 - l)); f++)
                    if (base + f < 512) h->look[base + f] = (uint16_t)((l << 8) | h->vals[k & 255]);
            }
        }
        h->maxcode[l] = h->bits[l] ? code - 1 : -1;
        code <<= 1;
    }
    h->present = true;
}

/* -------------------------------- IDCT ----------------------------------- */
static void idct_block(const int16_t *blk_nat, const uint16_t *q, int N, uint8_t *dst, uint32_t stride) {
    const int16_t *A = (N == 8) ? &k_idct8[0][0] : (N == 4) ? &k_idct4[0][0] : (N == 2) ? &k_idct2[0][0] : &k_idct1[0][0];
    int32_t F[64], T[64];
    for (int v = 0; v < N; v++)
        for (int u = 0; u < N; u++) F[v * N + u] = (int32_t)blk_nat[v * 8 + u] * q[v * 8 + u];
    for (int v = 0; v < N; v++)                 /* horizontal pass */
        for (int x = 0; x < N; x++) {
            int32_t s = 0;
            for (int u = 0; u < N; u++) s += A[x * N + u] * F[v * N + u];
            T[v * N + x] = s;
        }
    for (int x = 0; x < N; x++)                 /* vertical pass */
        for (int y = 0; y < N; y++) {
            int64_t s = 0;
            for (int v = 0; v < N; v++) s += (int64_t)A[y * N + v] * T[v * N + x];
            int32_t p = (int32_t)((s + (1 << 23)) >> 24) + 128;
            dst[y * stride + x] = (uint8_t)(p < 0 ? 0 : p > 255 ? 255 : p);
        }
}

/* in-region test for the low NxN coefficients kept in progressive mode */
static inline bool in_reg(int nat, int N) { return (nat & 7) < N && (nat >> 3) < N; }
static inline int  slot_of(int nat, int N) { return (nat >> 3) * N + (nat & 7); }

/* --------------------------------- blocks ---------------------------------- */
static void block_baseline(dec_t *d, comp_t *c, uint32_t bx, uint32_t by) {
    int16_t blk[64];
    for (int i = 0; i < 64; i++) blk[i] = 0;
    int t = huff_decode(d, &d->dc[c->td & 3]);
    int diff = (t & 15) ? extend(getbits(d, t & 15), t & 15) : 0;
    c->dcpred += diff;
    blk[0] = (int16_t)c->dcpred;
    const huff_t *ac = &d->ac[c->ta & 3];
    for (int k = 1; k < 64;) {
        int rs = huff_decode(d, ac), s = rs & 15, r = rs >> 4;
        if (!s) { if (r != 15) break; k += 16; continue; }
        k += r;
        if (k > 63) break;
        blk[k_zz[k]] = (int16_t)extend(getbits(d, s), s);
        k++;
    }
    idct_block(blk, d->qt[c->tq & 3], d->N, c->plane + ((size_t)by * d->N) * c->pw + (size_t)bx * d->N, c->pw);
}

static void block_progressive(dec_t *d, comp_t *c, uint32_t bx, uint32_t by,
                              int Ss, int Se, int Ah, int Al) {
    const int N = d->N;
    size_t bi = (size_t)by * c->bw + bx;
    int16_t *cb = c->coef + bi * (size_t)(N * N);
    uint64_t *mk = &c->mask[bi];

    if (Ss == 0) {                                         /* ---- DC ---- */
        if (Ah == 0) {
            int t = huff_decode(d, &d->dc[c->td & 3]);
            int diff = (t & 15) ? extend(getbits(d, t & 15), t & 15) : 0;
            c->dcpred += diff;
            cb[0] = (int16_t)(c->dcpred * (1 << Al));
        } else if (getbits(d, 1)) cb[0] = (int16_t)(cb[0] | (1 << Al));
        return;
    }
    const huff_t *ac = &d->ac[c->ta & 3];
    if (Ah == 0) {                                         /* ---- AC first ---- */
        if (d->eobrun) { d->eobrun--; return; }
        for (int k = Ss; k <= Se;) {
            int rs = huff_decode(d, ac), s = rs & 15, r = rs >> 4;
            if (!s) {
                if (r < 15) { d->eobrun = (1u << r) - 1u; if (r) d->eobrun += getbits(d, r); break; }
                k += 16; continue;
            }
            k += r;
            if (k > 63) break;
            int nat = k_zz[k];
            int v = extend(getbits(d, s), s) * (1 << Al);
            if (in_reg(nat, N)) cb[slot_of(nat, N)] = (int16_t)v;
            *mk |= 1ull << nat;
            k++;
        }
        return;
    }
    /* ---- AC refinement ---- */
    const int p1 = 1 << Al, m1 = -(1 << Al);
    int k = Ss;
    if (!d->eobrun) {
        for (; k <= Se; k++) {
            int rs = huff_decode(d, ac), s = rs & 15, r = rs >> 4, val = 0;
            if (!s) {
                if (r < 15) { d->eobrun = (1u << r); if (r) d->eobrun += getbits(d, r); break; }
            } else val = getbits(d, 1) ? p1 : m1;
            while (k <= Se) {
                int nat = k_zz[k];
                if ((*mk >> nat) & 1) {                    /* already non-zero: correction bit */
                    if (getbits(d, 1) && in_reg(nat, N)) {
                        int16_t *cp = &cb[slot_of(nat, N)];
                        if ((*cp & p1) == 0) *cp = (int16_t)(*cp + (*cp >= 0 ? p1 : m1));
                    }
                } else {
                    if (--r < 0) break;
                }
                k++;
            }
            if (s && k <= Se) {
                int nat = k_zz[k];
                if (in_reg(nat, N)) cb[slot_of(nat, N)] = (int16_t)val;
                *mk |= 1ull << nat;
            }
        }
    }
    if (d->eobrun) {                                       /* rest of the band: only corrections */
        for (; k <= Se; k++) {
            int nat = k_zz[k];
            if ((*mk >> nat) & 1) {
                if (getbits(d, 1) && in_reg(nat, N)) {
                    int16_t *cp = &cb[slot_of(nat, N)];
                    if ((*cp & p1) == 0) *cp = (int16_t)(*cp + (*cp >= 0 ? p1 : m1));
                }
            }
        }
        d->eobrun--;
    }
}

/* ---------------------------------- scans ---------------------------------- */
static void do_restart(dec_t *d, comp_t **sc, int ns) {
    d->nbits = 0; d->bitbuf = 0;
    if (d->marker < 0) {                                   /* look for the RSTn marker */
        for (;;) {
            int b = rb(d);
            if (b < 0) break;
            if (b != 0xFF) continue;
            do { b = rb(d); } while (b == 0xFF);
            if (b > 0) { d->marker = b; break; }
        }
    }
    if (d->marker >= 0xD0 && d->marker <= 0xD7) d->marker = -1;
    for (int i = 0; i < ns; i++) sc[i]->dcpred = 0;
    d->eobrun = 0;
}

static int do_scan(dec_t *d) {
    int len = rb16(d);
    int ns = rb(d);
    if (len < 0 || ns < 1 || ns > 3 || len != 6 + 2 * ns) return JPEG_ERR_FORMAT;
    comp_t *sc[3];
    for (int i = 0; i < ns; i++) {
        int cid = rb(d), tt = rb(d);
        if (cid < 0 || tt < 0) return JPEG_ERR_FORMAT;
        int j;
        for (j = 0; j < d->ncomp; j++) if (d->c[j].id == cid) break;
        if (j == d->ncomp) return JPEG_ERR_FORMAT;
        sc[i] = &d->c[j];
        sc[i]->td = (uint8_t)(tt >> 4); sc[i]->ta = (uint8_t)(tt & 15);
    }
    int Ss = rb(d), Se = rb(d), a = rb(d);
    if (Ss < 0 || Se < 0 || a < 0) return JPEG_ERR_FORMAT;
    int Ah = a >> 4, Al = a & 15;
    if (!d->progressive) { Ss = 0; Se = 63; Ah = Al = 0; }
    else if (Ss > Se || Se > 63 || (Ss == 0 && Se != 0 && false)) return JPEG_ERR_FORMAT;

    d->nbits = 0; d->bitbuf = 0; d->eobrun = 0;
    for (int i = 0; i < ns; i++) sc[i]->dcpred = 0;

    uint32_t total, mcux_i, mcuy_i;
    if (ns == 1) { mcux_i = sc[0]->cw; mcuy_i = sc[0]->ch; }
    else         { mcux_i = d->mcux;   mcuy_i = d->mcuy; }
    total = mcux_i * mcuy_i;

    for (uint32_t m = 0; m < total; m++) {
        if (d->restart_interval && m && (m % d->restart_interval) == 0) do_restart(d, sc, ns);
        if ((m & 0x3FFu) == 0 && fexox_os_idle_hook) fexox_os_idle_hook();
        uint32_t mx = m % mcux_i, my = m / mcux_i;
        if (ns == 1) {
            if (d->progressive) block_progressive(d, sc[0], mx, my, Ss, Se, Ah, Al);
            else                block_baseline(d, sc[0], mx, my);
        } else {
            for (int i = 0; i < ns; i++) {
                comp_t *c = sc[i];
                for (int v = 0; v < c->v; v++)
                    for (int h = 0; h < c->h; h++) {
                        uint32_t bx = mx * c->h + (uint32_t)h, by = my * c->v + (uint32_t)v;
                        if (d->progressive) block_progressive(d, c, bx, by, Ss, Se, Ah, Al);
                        else                block_baseline(d, c, bx, by);
                    }
            }
        }
    }
    d->scans++;
    d->nbits = 0; d->bitbuf = 0;
    return JPEG_OK;
}

/* ----------------------------------- setup --------------------------------- */
static void free_all(dec_t *d) {
    for (int i = 0; i < 3; i++) {
        if (d->c[i].plane) kfree(d->c[i].plane);
        if (d->c[i].coef)  kfree(d->c[i].coef);
        if (d->c[i].mask)  kfree(d->c[i].mask);
    }
}

static int do_sof(dec_t *d, int marker) {
    int len = rb16(d), prec = rb(d);
    int H = rb16(d), W = rb16(d), nc = rb(d);
    if (len < 0 || prec < 0 || H <= 0 || W <= 0 || nc < 0) return JPEG_ERR_FORMAT;
    if (d->have_sof) return JPEG_ERR_FORMAT;
    if (prec != 8) return JPEG_ERR_UNSUPPORTED;
    if (nc != 1 && nc != 3) return JPEG_ERR_UNSUPPORTED;
    if (len != 8 + 3 * nc) return JPEG_ERR_FORMAT;
    d->W = (uint32_t)W; d->H = (uint32_t)H; d->ncomp = nc;
    d->progressive = (marker == 0xC2);
    d->hmax = d->vmax = 1;
    for (int i = 0; i < nc; i++) {
        int id = rb(d), hv = rb(d), tq = rb(d);
        if (id < 0 || hv < 0 || tq < 0) return JPEG_ERR_FORMAT;
        d->c[i].id = (uint8_t)id; d->c[i].h = (uint8_t)(hv >> 4); d->c[i].v = (uint8_t)(hv & 15); d->c[i].tq = (uint8_t)(tq & 3);
        if (d->c[i].h < 1 || d->c[i].h > 4 || d->c[i].v < 1 || d->c[i].v > 4) return JPEG_ERR_FORMAT;
        if (d->c[i].h > d->hmax) d->hmax = d->c[i].h;
        if (d->c[i].v > d->vmax) d->vmax = d->c[i].v;
    }
    d->mcux = ((uint32_t)W + 8u * (uint32_t)d->hmax - 1u) / (8u * (uint32_t)d->hmax);
    d->mcuy = ((uint32_t)H + 8u * (uint32_t)d->vmax - 1u) / (8u * (uint32_t)d->vmax);

    int N = 8;                                   /* smallest scale that still covers the wanted size */
    if (d->want_w && d->want_h) {
        N = 1;
        while (N < 8 && (((uint64_t)W * (uint64_t)N + 7u) / 8u < d->want_w ||
                         ((uint64_t)H * (uint64_t)N + 7u) / 8u < d->want_h)) N <<= 1;
    }
    d->N = N;
    for (int i = 0; i < nc; i++) {
        comp_t *c = &d->c[i];
        c->bw = d->mcux * c->h; c->bh = d->mcuy * c->v;
        uint32_t cwpx = ((uint32_t)W * c->h + (uint32_t)d->hmax - 1u) / (uint32_t)d->hmax;
        uint32_t chpx = ((uint32_t)H * c->v + (uint32_t)d->vmax - 1u) / (uint32_t)d->vmax;
        c->cw = (cwpx + 7u) / 8u; c->ch = (chpx + 7u) / 8u;
        c->pw = c->bw * (uint32_t)N; c->ph = c->bh * (uint32_t)N;
        c->plane = (uint8_t *)kmalloc((uint64_t)c->pw * c->ph);
        if (!c->plane) return JPEG_ERR_NOMEM;
        memset(c->plane, 128, (size_t)c->pw * c->ph);
        if (d->progressive) {
            uint64_t nb = (uint64_t)c->bw * c->bh;
            c->coef = (int16_t *)kmalloc(nb * (uint64_t)(N * N) * 2u);
            c->mask = (uint64_t *)kmalloc(nb * 8u);
            if (!c->coef || !c->mask) return JPEG_ERR_NOMEM;
            memset(c->coef, 0, (size_t)(nb * (uint64_t)(N * N) * 2u));
            memset(c->mask, 0, (size_t)(nb * 8u));
        }
    }
    d->have_sof = true;
    return JPEG_OK;
}

static int do_dht(dec_t *d) {
    int len = rb16(d) - 2;
    while (len > 16) {
        int tc = rb(d);
        if (tc < 0) return JPEG_ERR_FORMAT;
        huff_t *h = ((tc >> 4) == 0) ? &d->dc[tc & 3] : &d->ac[tc & 3];
        int n = 0;
        h->bits[0] = 0;
        for (int i = 1; i <= 16; i++) { int b = rb(d); if (b < 0) return JPEG_ERR_FORMAT; h->bits[i] = (uint8_t)b; n += b; }
        if (n > 256) return JPEG_ERR_FORMAT;
        for (int i = 0; i < n; i++) { int b = rb(d); if (b < 0) return JPEG_ERR_FORMAT; h->vals[i] = (uint8_t)b; }
        huff_build(h);
        len -= 17 + n;
    }
    return JPEG_OK;
}

static int do_dqt(dec_t *d) {
    int len = rb16(d) - 2;
    while (len > 0) {
        int pq = rb(d);
        if (pq < 0) return JPEG_ERR_FORMAT;
        int prec = pq >> 4, id = pq & 3;
        for (int i = 0; i < 64; i++) {
            int v = prec ? rb16(d) : rb(d);
            if (v < 0) return JPEG_ERR_FORMAT;
            d->qt[id][k_zz[i]] = (uint16_t)v;
        }
        len -= 1 + (prec ? 128 : 64);
    }
    return JPEG_OK;
}

static void skip_seg(dec_t *d) {
    int len = rb16(d);
    for (int i = 0; i < len - 2; i++) if (rb(d) < 0) break;
}

static void do_app14(dec_t *d) {
    int len = rb16(d);
    uint8_t t[16]; int n = 0;
    for (int i = 0; i < len - 2; i++) { int b = rb(d); if (b < 0) break; if (n < 16) t[n++] = (uint8_t)b; }
    if (n >= 12 && t[0] == 'A' && t[1] == 'd' && t[2] == 'o' && t[3] == 'b' && t[4] == 'e') {
        d->adobe = true; d->adobe_rgb = (t[11] == 0);
    }
}

/* ---------------------------------- output --------------------------------- */
static void finish_progressive(dec_t *d) {
    const int N = d->N;
    for (int ci = 0; ci < d->ncomp; ci++) {
        comp_t *c = &d->c[ci];
        for (uint32_t by = 0; by < c->bh; by++) {
            for (uint32_t bx = 0; bx < c->bw; bx++) {
                int16_t blk[64];
                for (int i = 0; i < 64; i++) blk[i] = 0;
                const int16_t *cb = c->coef + ((size_t)by * c->bw + bx) * (size_t)(N * N);
                for (int v = 0; v < N; v++) for (int u = 0; u < N; u++) blk[v * 8 + u] = cb[v * N + u];
                idct_block(blk, d->qt[c->tq & 3], N, c->plane + ((size_t)by * N) * c->pw + (size_t)bx * N, c->pw);
            }
            if ((by & 63u) == 0 && fexox_os_idle_hook) fexox_os_idle_hook();
        }
    }
}

static int build_output(dec_t *d, png_image_t *out) {
    uint32_t ow = (uint32_t)(((uint64_t)d->W * (uint64_t)d->N + 7u) / 8u);
    uint32_t oh = (uint32_t)(((uint64_t)d->H * (uint64_t)d->N + 7u) / 8u);
    uint8_t *px = (uint8_t *)kmalloc((uint64_t)ow * oh * 4u);
    if (!px) return JPEG_ERR_NOMEM;
    uint32_t *xmap = (uint32_t *)kmalloc((uint64_t)ow * 3u * 4u);
    if (!xmap) { kfree(px); return JPEG_ERR_NOMEM; }
    for (int ci = 0; ci < d->ncomp; ci++)
        for (uint32_t x = 0; x < ow; x++) {
            uint32_t xc = x * d->c[ci].h / (uint32_t)d->hmax;
            xmap[ci * ow + x] = xc < d->c[ci].pw ? xc : d->c[ci].pw - 1u;
        }
    const bool rgb_direct = (d->ncomp == 3 && d->adobe && d->adobe_rgb);
    for (uint32_t y = 0; y < oh; y++) {
        const uint8_t *row[3] = {0, 0, 0};
        for (int ci = 0; ci < d->ncomp; ci++) {
            uint32_t yc = y * d->c[ci].v / (uint32_t)d->vmax;
            if (yc >= d->c[ci].ph) yc = d->c[ci].ph - 1u;
            row[ci] = d->c[ci].plane + (size_t)yc * d->c[ci].pw;
        }
        uint8_t *o = px + (size_t)y * ow * 4u;
        for (uint32_t x = 0; x < ow; x++, o += 4) {
            if (d->ncomp == 1) { uint8_t g = row[0][xmap[x]]; o[0] = o[1] = o[2] = g; o[3] = 255; continue; }
            int Y = row[0][xmap[x]], cb = row[1][xmap[ow + x]], cr = row[2][xmap[2 * ow + x]];
            if (rgb_direct) { o[0] = (uint8_t)Y; o[1] = (uint8_t)cb; o[2] = (uint8_t)cr; o[3] = 255; continue; }
            cb -= 128; cr -= 128;
            int r = Y + ((91881 * cr + 32768) >> 16);
            int g = Y - ((22554 * cb + 46802 * cr + 32768) >> 16);
            int b = Y + ((116130 * cb + 32768) >> 16);
            o[0] = (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r);
            o[1] = (uint8_t)(g < 0 ? 0 : g > 255 ? 255 : g);
            o[2] = (uint8_t)(b < 0 ? 0 : b > 255 ? 255 : b);
            o[3] = 255;
        }
        if ((y & 127u) == 0 && fexox_os_idle_hook) fexox_os_idle_hook();
    }
    kfree(xmap);
    out->width = ow; out->height = oh; out->pixels = px;
    return JPEG_OK;
}

/* ----------------------------------- API ----------------------------------- */
int jpeg_decode_stream(jpeg_read_fn rd, void *ctx, uint32_t want_w, uint32_t want_h, png_image_t *out) {
    dec_t *d = (dec_t *)kmalloc(sizeof(dec_t));
    if (!d) return JPEG_ERR_NOMEM;
    memset(d, 0, sizeof(*d));
    d->buf = (uint8_t *)kmalloc(65536u);
    if (!d->buf) { kfree(d); return JPEG_ERR_NOMEM; }
    d->rd = rd; d->ctx = ctx; d->marker = -1;
    d->want_w = want_w; d->want_h = want_h;
    for (int i = 0; i < 4; i++) for (int k = 0; k < 64; k++) d->qt[i][k] = 1;

    int rc = JPEG_OK;
    if (next_marker(d) != 0xD8) { rc = JPEG_ERR_FORMAT; goto done; }
    for (;;) {
        int m = next_marker(d);
        if (m < 0 || m == 0xD9) break;                         /* EOI / end of data */
        if (m == 0xC0 || m == 0xC1 || m == 0xC2) { rc = do_sof(d, m); if (rc) goto done; }
        else if ((m >= 0xC3 && m <= 0xCF) && m != 0xC4 && m != 0xC8 && m != 0xCC) { rc = JPEG_ERR_UNSUPPORTED; goto done; }
        else if (m == 0xC4) { rc = do_dht(d); if (rc) goto done; }
        else if (m == 0xDB) { rc = do_dqt(d); if (rc) goto done; }
        else if (m == 0xDD) { int l = rb16(d); int ri = rb16(d); if (l != 4 || ri < 0) { rc = JPEG_ERR_FORMAT; goto done; } d->restart_interval = (uint32_t)ri; }
        else if (m == 0xEE) do_app14(d);
        else if (m == 0xDA) { if (!d->have_sof) { rc = JPEG_ERR_FORMAT; goto done; } rc = do_scan(d); if (rc) goto done; }
        else if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01) { /* no payload */ }
        else skip_seg(d);
    }
    if (!d->have_sof || d->scans == 0) { rc = JPEG_ERR_FORMAT; goto done; }
    if (d->progressive) finish_progressive(d);
    rc = build_output(d, out);
done:
    free_all(d);
    kfree(d->buf);
    kfree(d);
    return rc;
}

typedef struct { const uint8_t *p; size_t n, pos; } mem_src_t;
static int mem_read(void *ctx, uint8_t *buf, uint32_t max) {
    mem_src_t *s = (mem_src_t *)ctx;
    size_t left = s->n - s->pos;
    if (!left) return 0;
    size_t c = left < max ? left : max;
    for (size_t i = 0; i < c; i++) buf[i] = s->p[s->pos + i];
    s->pos += c;
    return (int)c;
}
int jpeg_decode(const uint8_t *data, size_t size, uint32_t want_w, uint32_t want_h, png_image_t *out) {
    mem_src_t s = { data, size, 0 };
    return jpeg_decode_stream(mem_read, &s, want_w, want_h, out);
}

int jpeg_probe(const uint8_t *data, size_t size, uint32_t *w, uint32_t *h) {
    size_t i = 2;
    if (size < 4 || data[0] != 0xFF || data[1] != 0xD8) return JPEG_ERR_FORMAT;
    while (i + 9 < size) {
        if (data[i] != 0xFF) { i++; continue; }
        uint8_t m = data[i + 1];
        if (m == 0xFF) { i++; continue; }
        if (m == 0xC0 || m == 0xC1 || m == 0xC2) {
            *h = ((uint32_t)data[i + 5] << 8) | data[i + 6];
            *w = ((uint32_t)data[i + 7] << 8) | data[i + 8];
            return JPEG_OK;
        }
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { i += 2; continue; }
        i += 2 + (((size_t)data[i + 2] << 8) | data[i + 3]);
    }
    return JPEG_ERR_FORMAT;
}
