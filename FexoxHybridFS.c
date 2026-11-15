/* Fexox OS Project, EULA GPLv2 */
/* FexoxHybridFS - see FexoxHybridFS.h for the design summary. */

#include "FexoxHybridFS.h"
#include "debug_out.h"

extern void *kmalloc(uint64_t size);
extern void  kfree(void *p);
extern void  sched_yield(void);
extern uint64_t usb_tsc_per_ms(void);

/* ------------------------------------------------------------------ */
/* small helpers (no libc dependency)                                  */
/* ------------------------------------------------------------------ */
static void *fh_zalloc(uint64_t n) {
    void *p = kmalloc(n);
    if (p) { uint8_t *b = (uint8_t *)p; for (uint64_t i = 0; i < n; i++) b[i] = 0; }
    return p;
}
typedef struct { uint64_t v; } __attribute__((packed, may_alias)) fh_u64u_t;   /* unaligned-safe access */
static void fh_set(void *d, uint8_t v, uint64_t n) {
    uint8_t *p = (uint8_t *)d;
    uint64_t w = 0x0101010101010101ULL * v;
    while (n >= 8) { ((fh_u64u_t *)p)->v = w; p += 8; n -= 8; }
    while (n--) *p++ = v;
}
static void fh_cpy(void *d, const void *s, uint64_t n) {
    uint8_t *dd = (uint8_t *)d; const uint8_t *ss = (const uint8_t *)s;
    while (n >= 8) { ((fh_u64u_t *)dd)->v = ((const fh_u64u_t *)ss)->v; dd += 8; ss += 8; n -= 8; }
    while (n--) *dd++ = *ss++;
}
static void fh_move(void *d, const void *s, uint64_t n) {
    uint8_t *dd = (uint8_t *)d; const uint8_t *ss = (const uint8_t *)s;
    if (dd == ss || !n) return;
    if (dd < ss) { while (n--) *dd++ = *ss++; }
    else { dd += n; ss += n; while (n--) *--dd = *--ss; }
}
static int fh_cmp(const void *a, const void *b, uint64_t n) {
    const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b;
    while (n--) { if (*x != *y) return (int)*x - (int)*y; x++; y++; }
    return 0;
}
static uint32_t fh_slen(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }
static bool fh_is_zero(const void *p, uint64_t n) {
    const uint8_t *b = (const uint8_t *)p;
    for (uint64_t i = 0; i < n; i++) if (b[i]) return false;
    return true;
}

#define BLK        4096u
#define FANOUT     128u
#define UB_SLOTS   16u
#define HOT_SLOTS  8
#define IHASH      256u
#define ICACHE_MAX 2048u
#define DIRTY_LIMIT (8u * 1024u * 1024u)
#define FH_MAGIC   0x53464859484F5846ULL      /* "FOXHYFS" */
#define FH_UB_MAGIC 0x42554859484F5846ULL
#define FH_VERSION 1u
#define FH_ROOT_INO 1u
#define FH_LINK_MAX 65000u

/* ------------------------------------------------------------------ */
/* checksum: CRC32C lane + Fletcher lane = 64 bits                     */
/* ------------------------------------------------------------------ */
static uint32_t g_crc[256];
static bool     g_crc_ok;

static void fh_crc_init(void) {
    if (g_crc_ok) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (c >> 1) ^ 0x82F63B78u : (c >> 1);
        g_crc[i] = c;
    }
    g_crc_ok = true;
}

static uint64_t fh_cksum(const void *p, uint64_t n) {
    const uint8_t *b = (const uint8_t *)p;
    uint32_t c = 0xFFFFFFFFu, a = 1, s = 0;
    for (uint64_t i = 0; i < n; i++) {
        uint8_t v = b[i];
        c = g_crc[(c ^ v) & 0xFF] ^ (c >> 8);
        a += v; s += a;
    }
    return ((uint64_t)(s ^ (a << 16)) << 32) | (uint64_t)(~c);
}

/* ------------------------------------------------------------------ */
/* LZ4-format block compressor ("FHLZ")                                */
/* ------------------------------------------------------------------ */
#define LZ_HBITS 12

static inline uint32_t lz_rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* returns compressed size, or 0 if it does not fit in cap */
static uint32_t fh_lz_compress(const uint8_t *src, uint32_t n, uint8_t *dst, uint32_t cap,
                               uint32_t *ht, uint32_t *basep) {
    if (n < 32) return 0;
    uint32_t base = *basep;
    if (base > 0xF0000000u) { fh_set(ht, 0, (1u << LZ_HBITS) * 4); base = 1; }
    if (base == 0) base = 1;
    uint32_t ip = 0, anchor = 0, op = 0;
    const uint32_t mflimit = n - 12, matchlimit = n - 5;

    while (ip < mflimit) {
        uint32_t h = (lz_rd32(src + ip) * 2654435761u) >> (32 - LZ_HBITS);
        uint32_t ref = ht[h];
        ht[h] = base + ip;
        if (ref >= base && (ref - base) < ip && (ip - (ref - base)) <= 65535 &&
            lz_rd32(src + (ref - base)) == lz_rd32(src + ip)) {
            uint32_t r = ref - base, ml = 4;
            while (ip + ml < matchlimit && src[r + ml] == src[ip + ml]) ml++;
            uint32_t lit = ip - anchor;
            uint32_t need = 1 + lit / 255 + 1 + lit + 2 + (ml - 4) / 255 + 1;
            if (op + need > cap) { *basep = base + n; return 0; }
            uint8_t *tok = dst + op++;
            uint32_t t = (lit >= 15 ? 15u : lit) << 4;
            if (lit >= 15) { uint32_t l = lit - 15; while (l >= 255) { dst[op++] = 255; l -= 255; } dst[op++] = (uint8_t)l; }
            fh_cpy(dst + op, src + anchor, lit); op += lit;
            uint32_t off = ip - r;
            dst[op++] = (uint8_t)off; dst[op++] = (uint8_t)(off >> 8);
            uint32_t m = ml - 4;
            t |= (m >= 15 ? 15u : m);
            if (m >= 15) { m -= 15; while (m >= 255) { dst[op++] = 255; m -= 255; } dst[op++] = (uint8_t)m; }
            *tok = (uint8_t)t;
            ip += ml; anchor = ip;
        } else ip++;
    }
    uint32_t lit = n - anchor;
    if (op + 1 + lit / 255 + 1 + lit > cap) { *basep = base + n; return 0; }
    uint8_t *tok = dst + op++;
    *tok = (uint8_t)((lit >= 15 ? 15u : lit) << 4);
    if (lit >= 15) { uint32_t l = lit - 15; while (l >= 255) { dst[op++] = 255; l -= 255; } dst[op++] = (uint8_t)l; }
    fh_cpy(dst + op, src + anchor, lit); op += lit;
    *basep = base + n;
    return op;
}

/* returns FH_OK if exactly dlen bytes were produced */
static int fh_lz_decompress(const uint8_t *src, uint32_t clen, uint8_t *dst, uint32_t dlen) {
    uint32_t ip = 0, op = 0;
    while (ip < clen) {
        uint8_t tok = src[ip++];
        uint32_t lit = tok >> 4;
        if (lit == 15) { uint8_t b; do { if (ip >= clen) return FH_ECORRUPT; b = src[ip++]; lit += b; } while (b == 255); }
        if (ip + lit > clen || op + lit > dlen) return FH_ECORRUPT;
        fh_cpy(dst + op, src + ip, lit); ip += lit; op += lit;
        if (ip >= clen) break;
        if (ip + 2 > clen) return FH_ECORRUPT;
        uint32_t off = (uint32_t)src[ip] | ((uint32_t)src[ip + 1] << 8); ip += 2;
        if (off == 0 || off > op) return FH_ECORRUPT;
        uint32_t ml = (tok & 15u);
        if (ml == 15) { uint8_t b; do { if (ip >= clen) return FH_ECORRUPT; b = src[ip++]; ml += b; } while (b == 255); }
        ml += 4;
        if (op + ml > dlen) return FH_ECORRUPT;
        for (uint32_t i = 0; i < ml; i++) { dst[op] = dst[op - off]; op++; }
    }
    return op == dlen ? FH_OK : FH_ECORRUPT;
}

/* ------------------------------------------------------------------ */
/* runtime structures                                                  */
/* ------------------------------------------------------------------ */
typedef struct fh_tnode {
    fh_bp_t          bp;                 /* where the CURRENT on-disk copy lives */
    fh_bp_t          ptr[FANOUT];        /* exactly 4096 bytes: the on-disk image */
    struct fh_tnode *kid[FANOUT];
    bool             dirty;
    uint8_t          level;              /* 1 = points to leaves */
} fh_tnode_t;

typedef struct {
    fh_bp_t     root;
    uint32_t    depth;                   /* 0: root IS the single leaf */
    fh_tnode_t *rnode;
} fh_tree_t;

struct fhfs_inode {
    uint64_t         ino;
    fh_dinode_t      d;
    fh_tree_t        tree;
    uint32_t         ref;
    bool             dirty, tdirty;
    struct fhfs_inode *hnext;
    fhfs_t          *fs;
};

typedef struct { fhfs_inode_t *ip; int64_t idx; uint8_t *buf; bool dirty, used; uint64_t stamp; } fh_hot_t;
typedef struct { uint64_t blk; uint32_t n; } fh_pend_t;

struct fhfs {
    blkdev_t *dev;
    uint32_t  spb;                       /* device sectors per 4K block */
    bool      ro, dead, compress;
    fh_label_t lab;
    uint64_t  total, data_start, data_end, dstart, meta_lo, meta_hi;
    uint32_t  media, rec_shift, rec_size;
    uint64_t  txg, open_txg;
    fh_tree_t itree;
    uint64_t *bm; uint64_t bm_words;
    uint64_t  used;
    uint64_t  cur_data, cur_meta;
    fh_pend_t *pend; uint32_t npend, cappend;
    fhfs_inode_t *ihash[IHASH]; uint32_t icount;
    fh_hot_t  hot[HOT_SLOTS]; uint64_t stamp;
    uint8_t  *iobuf, *cbuf, *tmpbuf;
    uint32_t *lz_ht; uint32_t lz_base;
    uint64_t  ino_hint;
    volatile int lock;
    uint64_t  dirty_bytes;
    bool      gdirty;
    uint64_t  last_commit_ms;
    uint32_t  cksum_errors;
    /* block sharing (reflink / snapshots): holders of every block that has more than one */
    struct { uint64_t blk; uint32_t rc, pad; } *rc; uint32_t rc_cap, rc_n;
    /* snapshots */
    struct fh_snap { char name[48]; uint64_t id, txg, time_ns; fh_bp_t root; uint32_t depth, flags; } snaps[36];
    uint32_t  nsnaps; uint64_t snap_next_id;
    fh_bp_t   snap_bp; bool snap_dirty;
    fhfs_t   *parent; uint32_t snap_mounts;       /* read-only snapshot view -> its live fs */
};

/* ------------------------------------------------------------------ */
/* time / lock / device I/O                                            */
/* ------------------------------------------------------------------ */
static inline uint64_t fh_rdtsc(void) {
    uint32_t lo, hi; __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi)); return ((uint64_t)hi << 32) | lo;
}
static uint64_t fh_now_ms(void) {
    uint64_t per = usb_tsc_per_ms(); if (!per) per = 3000000ULL;
    return fh_rdtsc() / per;
}
__attribute__((weak)) uint64_t fhfs_time_ns(void) {
    return 1767225600000000000ULL + fh_now_ms() * 1000000ULL;   /* 2026-01-01 + uptime */
}
__attribute__((weak)) int blkdev_flush(blkdev_t *dev) { (void)dev; return 0; }

static void fh_lock(fhfs_t *fs) {
    int spins = 0;
    while (__atomic_exchange_n(&fs->lock, 1, __ATOMIC_ACQUIRE)) {
        __asm__ volatile ("pause" ::: "memory");
        if (++spins > 2000) { spins = 0; sched_yield(); }
    }
}
static void fh_unlock(fhfs_t *fs) { __atomic_store_n(&fs->lock, 0, __ATOMIC_RELEASE); }

static int fh_bio(fhfs_t *fs, uint64_t blk, uint32_t nb, void *buf, bool wr) {
    uint64_t lba = blk * fs->spb;
    uint32_t cnt = nb * fs->spb;
    uint8_t *p = (uint8_t *)buf;
    uint32_t chunk = 256;                         /* sectors per request */
    while (cnt) {
        uint32_t c = cnt > chunk ? chunk : cnt;
        int r = wr ? blkdev_write(fs->dev, lba, c, p) : blkdev_read(fs->dev, lba, c, p);
        if (r != VFS_OK) return FH_EIO;
        lba += c; cnt -= c; p += (uint64_t)c * fs->dev->sector_size;
    }
    return FH_OK;
}
static int fh_bread(fhfs_t *fs, uint64_t blk, uint32_t nb, void *buf) { return fh_bio(fs, blk, nb, buf, false); }
static int fh_bwrite(fhfs_t *fs, uint64_t blk, uint32_t nb, const void *buf) {
    if (fs->ro) return FH_EROFS;
    return fh_bio(fs, blk, nb, (void *)buf, true);
}
static void fh_barrier(fhfs_t *fs) { blkdev_flush(fs->dev); }

/* ------------------------------------------------------------------ */
/* allocation bitmap + adaptive allocator                              */
/* ------------------------------------------------------------------ */
static inline bool bm_test(fhfs_t *fs, uint64_t b) { return (fs->bm[b >> 6] >> (b & 63)) & 1; }
static inline void bm_setbit(fhfs_t *fs, uint64_t b) { fs->bm[b >> 6] |= 1ULL << (b & 63); }
static inline void bm_clrbit(fhfs_t *fs, uint64_t b) { fs->bm[b >> 6] &= ~(1ULL << (b & 63)); }
static void bm_mark(fhfs_t *fs, uint64_t b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) if (b + i < fs->total && !bm_test(fs, b + i)) { bm_setbit(fs, b + i); fs->used++; }
}
static void bm_unmark(fhfs_t *fs, uint64_t b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) if (b + i < fs->total && bm_test(fs, b + i)) { bm_clrbit(fs, b + i); fs->used--; }
}

static bool bm_scan(fhfs_t *fs, uint64_t lo, uint64_t hi, uint32_t n, uint32_t align, uint64_t *out) {
    if (align > 1) {
        for (uint64_t i = (lo + align - 1) / align * align; i + n <= hi; i += align) {
            uint32_t k = 0; while (k < n && !bm_test(fs, i + k)) k++;
            if (k == n) { *out = i; return true; }
        }
        return false;
    }
    uint64_t i = lo;
    while (i + n <= hi) {
        if ((i & 63) == 0 && fs->bm[i >> 6] == ~0ULL) { i += 64; continue; }
        if (bm_test(fs, i)) { i++; continue; }
        uint32_t k = 1; while (k < n && !bm_test(fs, i + k)) k++;
        if (k == n) { *out = i; return true; }
        i += k + 1;
    }
    return false;
}

static uint32_t fh_pow2ceil(uint32_t n) { uint32_t p = 1; while (p < n) p <<= 1; return p; }

static int fh_alloc(fhfs_t *fs, uint32_t nb, bool meta, uint64_t *out) {
    if (fs->ro) return FH_EROFS;
    uint64_t alloc_total = fs->data_end - fs->data_start;
    uint64_t reserve = alloc_total / 50; if (reserve < 64) reserve = 64;
    if (!meta && fs->used + nb + reserve > alloc_total) return FH_ENOSPC;
    if (fs->used + nb > alloc_total) return FH_ENOSPC;

    uint32_t align = 1;
    if (nb > 1) {
        if (fs->media == FH_MEDIA_FLASH) { align = fh_pow2ceil(nb); if (align > 32) align = 32; }
        else if (fs->media == FH_MEDIA_SSD) { align = fh_pow2ceil(nb); if (align > 8) align = 8; }
    }
    uint64_t found;
    uint64_t *cur; uint64_t lo, hi;
    if (meta && fs->meta_hi > fs->meta_lo) { cur = &fs->cur_meta; lo = fs->meta_lo; hi = fs->meta_hi; }
    else                                  { cur = &fs->cur_data; lo = fs->dstart;   hi = fs->data_end; }
    uint64_t from = *cur; if (from < lo || from >= hi) from = lo;
    bool ok = bm_scan(fs, from, hi, nb, align, &found) || bm_scan(fs, lo, from + nb < hi ? from + nb : hi, nb, align, &found);
    if (!ok && cur == &fs->cur_meta) {                     /* meta zone full: spill into data zone */
        cur = &fs->cur_data; lo = fs->dstart; hi = fs->data_end; from = *cur; if (from < lo || from >= hi) from = lo;
        ok = bm_scan(fs, from, hi, nb, align, &found) || bm_scan(fs, lo, from + nb < hi ? from + nb : hi, nb, align, &found);
    }
    if (!ok) return FH_ENOSPC;
    bm_mark(fs, found, nb);
    *cur = found + nb;
    *out = found;
    return FH_OK;
}

/* ---- block holders (only blocks with >= 2 holders are stored) ---- */
typedef struct { uint64_t blk; uint32_t rc, pad; } fh_rc_ent_t;
static inline uint32_t rc_hash(uint64_t b, uint32_t cap) { return (uint32_t)((b * 0x9E3779B97F4A7C15ULL) >> 33) & (cap - 1); }
static fh_rc_ent_t *rc_find(fhfs_t *fs, uint64_t blk) {
    if (!fs->rc_cap) return NULL;
    fh_rc_ent_t *t = (fh_rc_ent_t *)fs->rc;
    for (uint32_t i = rc_hash(blk, fs->rc_cap);; i = (i + 1) & (fs->rc_cap - 1)) {
        if (t[i].blk == blk) return &t[i];
        if (t[i].blk == 0) return NULL;
    }
}
static int rc_insert(fhfs_t *fs, uint64_t blk, uint32_t rc) {
    if ((fs->rc_n + 1) * 2 > fs->rc_cap) {                      /* keep load factor <= 50% */
        uint32_t nc = fs->rc_cap ? fs->rc_cap * 2 : 1024;
        fh_rc_ent_t *nt = (fh_rc_ent_t *)fh_zalloc((uint64_t)nc * sizeof(fh_rc_ent_t));
        if (!nt) return FH_ENOMEM;
        fh_rc_ent_t *ot = (fh_rc_ent_t *)fs->rc;
        for (uint32_t i = 0; i < fs->rc_cap; i++) if (ot[i].blk) {
            uint32_t j = rc_hash(ot[i].blk, nc); while (nt[j].blk) j = (j + 1) & (nc - 1); nt[j] = ot[i];
        }
        if (ot) kfree(ot);
        fs->rc = (void *)nt; fs->rc_cap = nc;
    }
    fh_rc_ent_t *t = (fh_rc_ent_t *)fs->rc;
    uint32_t j = rc_hash(blk, fs->rc_cap); while (t[j].blk) j = (j + 1) & (fs->rc_cap - 1);
    t[j].blk = blk; t[j].rc = rc; fs->rc_n++;
    return FH_OK;
}
/* one more holder of a block that already has at least one */
static int rc_inc(fhfs_t *fs, uint64_t blk) {
    fh_rc_ent_t *e = rc_find(fs, blk);
    if (e) { e->rc++; return FH_OK; }
    return rc_insert(fs, blk, 2);
}
/* one holder less. Returns true if somebody else still holds the block. */
static bool rc_release(fhfs_t *fs, uint64_t blk) {
    fh_rc_ent_t *e = rc_find(fs, blk);
    if (!e) return false;
    if (--e->rc > 1) return true;
    fh_rc_ent_t *t = (fh_rc_ent_t *)fs->rc; uint32_t mask = fs->rc_cap - 1;
    uint32_t i = (uint32_t)(e - t), j = i;                       /* backward-shift deletion */
    for (;;) {
        j = (j + 1) & mask;
        if (!t[j].blk) break;
        uint32_t k = rc_hash(t[j].blk, fs->rc_cap);
        if ((j > i && (k <= i || k > j)) || (j < i && (k <= i && k > j))) { t[i] = t[j]; i = j; }
    }
    t[i].blk = 0; fs->rc_n--;
    return true;
}

static inline uint32_t bp_nblk(const fh_bp_t *bp) { return bp->blk ? (bp->clen + BLK - 1) / BLK : 0; }

static void fh_free_bp(fhfs_t *fs, const fh_bp_t *bp) {
    if (!bp->blk) return;
    if (rc_release(fs, bp->blk)) return;                    /* still used by a clone / snapshot */
    uint32_t n = bp_nblk(bp); if (!n) n = 1;
    if (bp->birth == (uint32_t)fs->open_txg) { bm_unmark(fs, bp->blk, n); return; }  /* never committed */
    if (fs->npend == fs->cappend) {
        uint32_t nc = fs->cappend ? fs->cappend * 2 : 1024;
        fh_pend_t *np = (fh_pend_t *)kmalloc((uint64_t)nc * sizeof(fh_pend_t));
        if (!np) return;                                    /* leak a block rather than corrupt */
        if (fs->pend) { fh_cpy(np, fs->pend, (uint64_t)fs->npend * sizeof(fh_pend_t)); kfree(fs->pend); }
        fs->pend = np; fs->cappend = nc;
    }
    fs->pend[fs->npend].blk = bp->blk; fs->pend[fs->npend].n = n; fs->npend++;
}

/* ------------------------------------------------------------------ */
/* block pointers: read / write with checksum + compression            */
/* ------------------------------------------------------------------ */
static int fh_read_bp(fhfs_t *fs, const fh_bp_t *bp, void *out) {
    if (!bp->blk) { fh_set(out, 0, bp->lsize); return FH_OK; }
    uint32_t nb = bp_nblk(bp);
    int r = fh_bread(fs, bp->blk, nb, fs->iobuf);
    if (r) return r;
    if (fh_cksum(fs->iobuf, bp->clen) != bp->cksum) { fs->cksum_errors++; return FH_ECORRUPT; }
    if (bp->comp) return fh_lz_decompress(fs->iobuf, bp->clen, (uint8_t *)out, bp->lsize);
    fh_cpy(out, fs->iobuf, bp->lsize);
    return FH_OK;
}

static int fh_write_bp(fhfs_t *fs, const void *data, uint32_t lsize, bool meta, bool allow_comp, fh_bp_t *out) {
    const uint8_t *src = (const uint8_t *)data;
    uint32_t clen = lsize; uint8_t comp = 0;
    if (allow_comp && fs->compress && lsize >= 512) {
        uint32_t raw_blocks = (lsize + BLK - 1) / BLK;
        uint32_t cap = raw_blocks * BLK;
        if (cap > 512) cap -= 512;                 /* must save at least one block to be worth it */
        uint32_t c = fh_lz_compress(src, lsize, fs->cbuf, cap, fs->lz_ht, &fs->lz_base);
        if (c && (c + BLK - 1) / BLK < raw_blocks) { src = fs->cbuf; clen = c; comp = 1; }
    }
    uint32_t nb = (clen + BLK - 1) / BLK;
    uint64_t blk;
    int r = fh_alloc(fs, nb, meta, &blk);
    if (r) return r;
    uint8_t *w = fs->iobuf;
    fh_cpy(w, src, clen);
    if (nb * BLK > clen) fh_set(w + clen, 0, nb * BLK - clen);
    r = fh_bwrite(fs, blk, nb, w);
    if (r) { bm_unmark(fs, blk, nb); return r; }
    out->blk = blk; out->clen = clen; out->lsize = lsize;
    out->cksum = fh_cksum(w, clen);
    out->birth = (uint32_t)fs->open_txg; out->comp = comp; out->flags = 0; out->pad = 0;
    return FH_OK;
}

/* ------------------------------------------------------------------ */
/* RoW radix trees (fan-out 128). depth 0 = the root pointer is a leaf */
/* ------------------------------------------------------------------ */
static inline uint64_t tree_cap(uint32_t depth) { return depth == 0 ? 1 : ((uint64_t)1 << (7 * depth)); }

static fh_tnode_t *tn_new(uint8_t level) {
    fh_tnode_t *n = (fh_tnode_t *)fh_zalloc(sizeof(fh_tnode_t));
    if (n) { n->level = level; n->dirty = true; }
    return n;
}

static fh_tnode_t *tn_load(fhfs_t *fs, const fh_bp_t *bp, uint8_t level) {
    fh_tnode_t *n = (fh_tnode_t *)fh_zalloc(sizeof(fh_tnode_t));
    if (!n) return NULL;
    if (fh_bread(fs, bp->blk, 1, fs->tmpbuf)) { kfree(n); return NULL; }
    if (fh_cksum(fs->tmpbuf, BLK) != bp->cksum) { fs->cksum_errors++; kfree(n); return NULL; }
    fh_cpy(n->ptr, fs->tmpbuf, BLK);
    n->bp = *bp; n->level = level;
    return n;
}

static void tn_free(fh_tnode_t *n) {
    if (!n) return;
    if (n->level > 1) for (uint32_t i = 0; i < FANOUT; i++) tn_free(n->kid[i]);
    kfree(n);
}

static int tree_root_node(fhfs_t *fs, fh_tree_t *t) {
    if (t->depth == 0 || t->rnode) return FH_OK;
    if (!t->root.blk) { t->rnode = tn_new((uint8_t)t->depth); return t->rnode ? FH_OK : FH_ENOMEM; }
    t->rnode = tn_load(fs, &t->root, (uint8_t)t->depth);
    return t->rnode ? FH_OK : FH_ECORRUPT;
}

static int tree_lookup(fhfs_t *fs, fh_tree_t *t, uint64_t idx, fh_bp_t *out) {
    fh_set(out, 0, sizeof(*out));
    if (idx >= tree_cap(t->depth)) return FH_OK;
    if (t->depth == 0) { *out = t->root; return FH_OK; }
    if (!t->rnode && !t->root.blk) return FH_OK;
    int r = tree_root_node(fs, t); if (r) return r;
    fh_tnode_t *n = t->rnode;
    for (uint32_t lvl = t->depth; lvl >= 1; lvl--) {
        uint32_t slot = (uint32_t)((idx >> (7 * (lvl - 1))) & 127);
        if (lvl == 1) { *out = n->ptr[slot]; return FH_OK; }
        fh_tnode_t *c = n->kid[slot];
        if (!c) {
            if (!n->ptr[slot].blk) return FH_OK;
            c = tn_load(fs, &n->ptr[slot], (uint8_t)(lvl - 1));
            if (!c) return FH_ECORRUPT;
            n->kid[slot] = c;
        }
        n = c;
    }
    return FH_OK;
}

static int tree_set(fhfs_t *fs, fh_tree_t *t, uint64_t idx, const fh_bp_t *bp, fh_bp_t *old) {
    if (old) fh_set(old, 0, sizeof(*old));
    if (t->depth == 0 && idx == 0) { if (old) *old = t->root; t->root = *bp; return FH_OK; }
    if (!bp->blk && idx >= tree_cap(t->depth)) return FH_OK;        /* clearing beyond the end */
    while (idx >= tree_cap(t->depth)) {                              /* grow upwards */
        fh_tnode_t *nn = tn_new((uint8_t)(t->depth + 1));
        if (!nn) return FH_ENOMEM;
        if (t->depth == 0) nn->ptr[0] = t->root;
        else { int r = tree_root_node(fs, t); if (r) { kfree(nn); return r; } nn->kid[0] = t->rnode; nn->ptr[0] = t->rnode->bp; }
        t->rnode = nn; t->depth++;
    }
    int r = tree_root_node(fs, t); if (r) return r;
    fh_tnode_t *n = t->rnode;
    for (uint32_t lvl = t->depth; lvl >= 2; lvl--) {
        uint32_t slot = (uint32_t)((idx >> (7 * (lvl - 1))) & 127);
        n->dirty = true;
        fh_tnode_t *c = n->kid[slot];
        if (!c) {
            c = n->ptr[slot].blk ? tn_load(fs, &n->ptr[slot], (uint8_t)(lvl - 1)) : tn_new((uint8_t)(lvl - 1));
            if (!c) return FH_ENOMEM;
            n->kid[slot] = c;
        }
        n = c;
    }
    uint32_t slot = (uint32_t)(idx & 127);
    n->dirty = true;
    if (old) *old = n->ptr[slot];
    n->ptr[slot] = *bp;
    return FH_OK;
}

static int tn_flush(fhfs_t *fs, fh_tnode_t *n) {
    if (!n->dirty) return FH_OK;
    if (n->level > 1) {
        for (uint32_t i = 0; i < FANOUT; i++)
            if (n->kid[i] && n->kid[i]->dirty) {
                int r = tn_flush(fs, n->kid[i]); if (r) return r;
                n->ptr[i] = n->kid[i]->bp;
            }
    }
    uint64_t blk;
    int r = fh_alloc(fs, 1, true, &blk); if (r) return r;
    r = fh_bwrite(fs, blk, 1, n->ptr);
    if (r) { bm_unmark(fs, blk, 1); return r; }
    fh_free_bp(fs, &n->bp);
    n->bp.blk = blk; n->bp.clen = BLK; n->bp.lsize = BLK; n->bp.cksum = fh_cksum(n->ptr, BLK);
    n->bp.birth = (uint32_t)fs->open_txg; n->bp.comp = 0; n->bp.flags = 0; n->bp.pad = 0;
    n->dirty = false;
    return FH_OK;
}

static int tree_flush(fhfs_t *fs, fh_tree_t *t) {
    if (t->depth == 0 || !t->rnode) return FH_OK;
    int r = tn_flush(fs, t->rnode); if (r) return r;
    t->root = t->rnode->bp;
    return FH_OK;
}

static void tn_release_blocks(fhfs_t *fs, fh_tnode_t *n) {
    for (uint32_t i = 0; i < FANOUT; i++) {
        if (n->level == 1) { if (n->ptr[i].blk) fh_free_bp(fs, &n->ptr[i]); }
        else if (n->kid[i]) tn_release_blocks(fs, n->kid[i]);
        else if (n->ptr[i].blk) {
            fh_tnode_t *c = tn_load(fs, &n->ptr[i], (uint8_t)(n->level - 1));
            if (c) { tn_release_blocks(fs, c); tn_free(c); }
        }
    }
    if (n->bp.blk) fh_free_bp(fs, &n->bp);
}

/* free every block of the tree (leaves + nodes) and reset it to empty */
static void tree_destroy(fhfs_t *fs, fh_tree_t *t) {
    if (t->depth == 0) { fh_free_bp(fs, &t->root); }
    else {
        if (!t->rnode && t->root.blk) t->rnode = tn_load(fs, &t->root, (uint8_t)t->depth);
        if (t->rnode) { tn_release_blocks(fs, t->rnode); tn_free(t->rnode); }
    }
    fh_set(t, 0, sizeof(*t));
}

/* ------------------------------------------------------------------ */
/* forward declarations                                                */
/* ------------------------------------------------------------------ */
static int  fh_commit(fhfs_t *fs);
static int  fh_snap_write(fhfs_t *fs);
static void fh_iput_l(fhfs_inode_t *ip);
static bool fh_umount_core(fhfs_t *fs);

static void fh_dirty(fhfs_inode_t *ip) { ip->dirty = true; ip->fs->gdirty = true; }
static uint32_t fh_rec_of(const fhfs_inode_t *ip) { return 1u << ip->d.rec_shift; }

/* ------------------------------------------------------------------ */
/* inode cache                                                         */
/* ------------------------------------------------------------------ */
static fhfs_inode_t *ic_find(fhfs_t *fs, uint64_t ino) {
    for (fhfs_inode_t *p = fs->ihash[ino % IHASH]; p; p = p->hnext) if (p->ino == ino) return p;
    return NULL;
}
static void ic_unlink_free(fhfs_t *fs, fhfs_inode_t *ip) {
    fhfs_inode_t **pp = &fs->ihash[ip->ino % IHASH];
    while (*pp && *pp != ip) pp = &(*pp)->hnext;
    if (*pp) *pp = ip->hnext;
    if (ip->tree.rnode) tn_free(ip->tree.rnode);
    kfree(ip);
    fs->icount--;
}
static void ic_trim(fhfs_t *fs) {
    for (uint32_t b = 0; b < IHASH && fs->icount > ICACHE_MAX * 3 / 4; b++) {
        fhfs_inode_t *p = fs->ihash[b];
        while (p) {
            fhfs_inode_t *nx = p->hnext;
            if (p->ref == 0 && !p->dirty && !p->tdirty) ic_unlink_free(fs, p);
            p = nx;
        }
    }
}

static uint32_t fh_dinode_ck(const fh_dinode_t *d) {
    return (uint32_t)fh_cksum(d, sizeof(*d) - 4);
}

static fhfs_inode_t *fh_iget(fhfs_t *fs, uint64_t ino, int *err) {
    *err = FH_OK;
    fhfs_inode_t *ip = ic_find(fs, ino);
    if (ip) { ip->ref++; return ip; }
    if (fs->icount >= ICACHE_MAX) { ic_trim(fs); if (fs->icount >= ICACHE_MAX && fs->gdirty) { fh_commit(fs); ic_trim(fs); } }
    ip = (fhfs_inode_t *)fh_zalloc(sizeof(*ip));
    if (!ip) { *err = FH_ENOMEM; return NULL; }
    fh_bp_t bp;
    int r = tree_lookup(fs, &fs->itree, ino / 16, &bp);
    if (!r && bp.blk) {
        r = fh_read_bp(fs, &bp, fs->tmpbuf);
        if (!r) {
            fh_dinode_t *d = &((fh_dinode_t *)(void *)fs->tmpbuf)[ino % 16];
            if (d->mode && d->cksum != fh_dinode_ck(d)) { fs->cksum_errors++; r = FH_ECORRUPT; }
            else ip->d = *d;
        }
    }
    if (r) { kfree(ip); *err = r; return NULL; }
    ip->ino = ino; ip->fs = fs; ip->ref = 1;
    ip->tree.root = ip->d.data_root; ip->tree.depth = ip->d.data_depth;
    ip->hnext = fs->ihash[ino % IHASH]; fs->ihash[ino % IHASH] = ip; fs->icount++;
    return ip;
}

/* ------------------------------------------------------------------ */
/* hot record cache (read cache + write-back buffer in one)            */
/* ------------------------------------------------------------------ */
static int hot_flush(fhfs_t *fs, fh_hot_t *h) {
    if (!h->used || !h->dirty) return FH_OK;
    fhfs_inode_t *ip = h->ip;
    uint32_t rs = fh_rec_of(ip);
    int64_t base = (int64_t)(h->idx << ip->d.rec_shift);
    int64_t remain = (int64_t)ip->d.size - base;
    h->dirty = false;
    fh_bp_t bp, old;
    if (remain <= 0) { fh_set(&bp, 0, sizeof(bp)); }
    else {
        uint32_t lsize = remain > (int64_t)rs ? rs : (uint32_t)remain;
        bool meta = FH_S_ISDIR(ip->d.mode) || FH_S_ISLNK(ip->d.mode);
        if (fh_is_zero(h->buf, lsize)) fh_set(&bp, 0, sizeof(bp));      /* sparse: store a hole */
        else {
            int r = fh_write_bp(fs, h->buf, lsize, meta, true, &bp);
            if (r) { h->dirty = true; return r; }
        }
    }
    int r = tree_set(fs, &ip->tree, (uint64_t)h->idx, &bp, &old);
    if (r) { fh_free_bp(fs, &bp); h->dirty = true; return r; }
    int64_t delta = (int64_t)bp_nblk(&bp) - (int64_t)bp_nblk(&old);
    ip->d.blocks4k = (uint64_t)((int64_t)ip->d.blocks4k + delta);
    fh_free_bp(fs, &old);
    ip->tdirty = true; fh_dirty(ip);
    return FH_OK;
}

static int hot_get(fhfs_t *fs, fhfs_inode_t *ip, int64_t idx, bool load, fh_hot_t **out) {
    fh_hot_t *free_slot = NULL, *lru = NULL;
    for (int i = 0; i < HOT_SLOTS; i++) {
        fh_hot_t *h = &fs->hot[i];
        if (h->used && h->ip == ip && h->idx == idx) { h->stamp = ++fs->stamp; *out = h; return FH_OK; }
        if (!h->used) { if (!free_slot) free_slot = h; }
        else if (!lru || h->stamp < lru->stamp) lru = h;
    }
    fh_hot_t *h = free_slot;
    if (!h) {
        h = lru;
        int r = hot_flush(fs, h); if (r) return r;
        fhfs_inode_t *old = h->ip; h->used = false; h->ip = NULL;
        fh_iput_l(old);
    }
    if (!h->buf) { h->buf = (uint8_t *)kmalloc(fs->rec_size); if (!h->buf) return FH_ENOMEM; }
    fh_set(h->buf, 0, fs->rec_size);
    if (load) {
        fh_bp_t bp;
        int r = tree_lookup(fs, &ip->tree, (uint64_t)idx, &bp); if (r) return r;
        if (bp.blk) { r = fh_read_bp(fs, &bp, h->buf); if (r) return r; }
    }
    h->ip = ip; ip->ref++; h->idx = idx; h->used = true; h->dirty = false; h->stamp = ++fs->stamp;
    *out = h;
    return FH_OK;
}

static int hot_flush_ip(fhfs_t *fs, fhfs_inode_t *ip) {
    for (int i = 0; i < HOT_SLOTS; i++)
        if (fs->hot[i].used && fs->hot[i].ip == ip) { int r = hot_flush(fs, &fs->hot[i]); if (r) return r; }
    return FH_OK;
}

/* drop (without writing) every cached record with idx >= from */
static void hot_drop(fhfs_t *fs, fhfs_inode_t *ip, int64_t from) {
    for (int i = 0; i < HOT_SLOTS; i++) {
        fh_hot_t *h = &fs->hot[i];
        if (h->used && h->ip == ip && h->idx >= from) { h->used = false; h->dirty = false; h->ip = NULL; ip->ref--; }
    }
}

/* ------------------------------------------------------------------ */
/* file data                                                           */
/* ------------------------------------------------------------------ */
static int64_t fh_file_read(fhfs_t *fs, fhfs_inode_t *ip, uint64_t off, uint64_t len, void *buf) {
    if (off >= ip->d.size) return 0;
    if (len > ip->d.size - off) len = ip->d.size - off;
    uint32_t rs = fh_rec_of(ip), sh = ip->d.rec_shift;
    uint8_t *dst = (uint8_t *)buf; uint64_t done = 0;
    while (done < len) {
        uint64_t pos = off + done;
        uint32_t roff = (uint32_t)(pos & (rs - 1));
        uint64_t n = rs - roff; if (n > len - done) n = len - done;
        fh_hot_t *h; int r = hot_get(fs, ip, (int64_t)(pos >> sh), true, &h);
        if (r) return done ? (int64_t)done : r;
        fh_cpy(dst + done, h->buf + roff, n);
        done += n;
    }
    return (int64_t)done;
}

static int64_t fh_file_write(fhfs_t *fs, fhfs_inode_t *ip, uint64_t off, uint64_t len, const void *buf) {
    if (fs->ro) return FH_EROFS;
    if (!len) return 0;
    if (off + len < off) return FH_EINVAL;
    uint32_t rs = fh_rec_of(ip), sh = ip->d.rec_shift;
    const uint8_t *src = (const uint8_t *)buf; uint64_t done = 0;
    uint64_t old_size = ip->d.size;
    if (off + len > ip->d.size) ip->d.size = off + len;
    while (done < len) {
        uint64_t pos = off + done;
        uint32_t roff = (uint32_t)(pos & (rs - 1));
        uint64_t n = rs - roff; if (n > len - done) n = len - done;
        bool full = (roff == 0 && n == rs);
        fh_hot_t *h; int r = hot_get(fs, ip, (int64_t)(pos >> sh), !full, &h);
        if (r) { if (!done) ip->d.size = old_size; return done ? (int64_t)done : r; }
        fh_cpy(h->buf + roff, src + done, n);
        if (!h->dirty) { h->dirty = true; }
        fs->dirty_bytes += n;
        done += n;
    }
    uint64_t now = fhfs_time_ns();
    ip->d.mtime = now; ip->d.ctime = now;
    fh_dirty(ip);
    if (fs->dirty_bytes >= DIRTY_LIMIT) fh_commit(fs);
    return (int64_t)done;
}

static int fh_file_truncate(fhfs_t *fs, fhfs_inode_t *ip, uint64_t nsize) {
    if (fs->ro) return FH_EROFS;
    uint64_t osize = ip->d.size;
    uint32_t rs = fh_rec_of(ip), sh = ip->d.rec_shift;
    if (nsize < osize) {
        uint64_t first_free = (nsize + rs - 1) >> sh;       /* first record that disappears */
        uint64_t last = (osize + rs - 1) >> sh;
        if (nsize & (rs - 1)) {                              /* zero the tail of the boundary record */
            fh_hot_t *h; int r = hot_get(fs, ip, (int64_t)(nsize >> sh), true, &h); if (r) return r;
            uint32_t from = (uint32_t)(nsize & (rs - 1));
            fh_set(h->buf + from, 0, rs - from);
            h->dirty = true;
        }
        hot_drop(fs, ip, (int64_t)first_free);
        for (uint64_t i = first_free; i < last; i++) {
            fh_bp_t bp, old; fh_set(&bp, 0, sizeof(bp));
            int r = tree_lookup(fs, &ip->tree, i, &old); if (r) return r;
            if (!old.blk) continue;
            r = tree_set(fs, &ip->tree, i, &bp, &old); if (r) return r;
            ip->d.blocks4k -= bp_nblk(&old);
            fh_free_bp(fs, &old);
            ip->tdirty = true;
        }
    }
    ip->d.size = nsize;
    uint64_t now = fhfs_time_ns();
    ip->d.mtime = now; ip->d.ctime = now;
    fh_dirty(ip);
    return FH_OK;
}

/* ------------------------------------------------------------------ */
/* directories: array of variable-size entries inside 4K records       */
/* ------------------------------------------------------------------ */
typedef struct __attribute__((packed)) {
    uint64_t ino; uint16_t reclen; uint8_t namelen, type; char name[];
} fh_dent_t;
#define DENT_HDR 12u
static inline uint32_t dent_need(uint32_t nlen) { return (DENT_HDR + nlen + 7u) & ~7u; }

static uint8_t fh_dt(uint32_t mode) {
    switch (mode & FH_S_IFMT) {
        case FH_S_IFDIR: return 4; case FH_S_IFREG: return 8; case FH_S_IFLNK: return 10;
        case FH_S_IFCHR: return 2; case FH_S_IFBLK: return 6; case FH_S_IFIFO: return 1;
        case FH_S_IFSOCK: return 12; default: return 0;
    }
}

static int fh_name_ok(const char *name, uint32_t *nlen) {
    uint32_t n = fh_slen(name);
    if (n == 0) return FH_EINVAL;
    if (n > FHFS_NAME_MAX) return FH_ENAMETOOLONG;
    for (uint32_t i = 0; i < n; i++) if (name[i] == '/') return FH_EINVAL;
    *nlen = n; return FH_OK;
}

static int dir_find(fhfs_t *fs, fhfs_inode_t *dir, const char *name, uint32_t nlen,
                    uint64_t *ino, uint8_t *type) {
    uint64_t nb = dir->d.size / BLK;
    for (uint64_t b = 0; b < nb; b++) {
        fh_hot_t *h; int r = hot_get(fs, dir, (int64_t)b, true, &h); if (r) return r;
        uint32_t off = 0;
        while (off < BLK) {
            fh_dent_t *e = (fh_dent_t *)(void *)(h->buf + off);
            if (e->reclen < DENT_HDR || (e->reclen & 7) || off + e->reclen > BLK) return FH_ECORRUPT;
            if (e->ino && e->namelen == nlen && fh_cmp(e->name, name, nlen) == 0) {
                if (ino) *ino = e->ino;
                if (type) *type = e->type;
                return FH_OK;
            }
            off += e->reclen;
        }
    }
    return FH_ENOENT;
}

static int dir_add(fhfs_t *fs, fhfs_inode_t *dir, const char *name, uint32_t nlen, uint64_t ino, uint8_t type) {
    uint32_t need = dent_need(nlen);
    uint64_t nb = dir->d.size / BLK;
    fh_hot_t *h;
    for (uint64_t b = 0; b <= nb; b++) {
        bool newblk = (b == nb);
        int r = hot_get(fs, dir, (int64_t)b, !newblk, &h); if (r) return r;
        if (newblk) {                                         /* one empty entry covering the block */
            fh_dent_t *e = (fh_dent_t *)(void *)h->buf;
            e->ino = 0; e->reclen = BLK; e->namelen = 0; e->type = 0;
        }
        uint32_t off = 0;
        while (off < BLK) {
            fh_dent_t *e = (fh_dent_t *)(void *)(h->buf + off);
            if (e->reclen < DENT_HDR || (e->reclen & 7) || off + e->reclen > BLK) return FH_ECORRUPT;
            uint32_t actual = e->ino ? dent_need(e->namelen) : 0;
            if (e->reclen - actual >= need) {
                fh_dent_t *ne = e;
                if (e->ino) {
                    ne = (fh_dent_t *)(void *)(h->buf + off + actual);
                    ne->reclen = (uint16_t)(e->reclen - actual);
                    e->reclen = (uint16_t)actual;
                }
                ne->ino = ino; ne->namelen = (uint8_t)nlen; ne->type = type;
                fh_cpy(ne->name, name, nlen);
                h->dirty = true; fs->dirty_bytes += 64;
                if (newblk) { dir->d.size += BLK; }
                return FH_OK;
            }
            off += e->reclen;
        }
        if (newblk) return FH_EINVAL;
    }
    return FH_EINVAL;
}

static int dir_remove(fhfs_t *fs, fhfs_inode_t *dir, const char *name, uint32_t nlen) {
    uint64_t nb = dir->d.size / BLK;
    for (uint64_t b = 0; b < nb; b++) {
        fh_hot_t *h; int r = hot_get(fs, dir, (int64_t)b, true, &h); if (r) return r;
        uint32_t off = 0, prev = 0xFFFFFFFFu;
        while (off < BLK) {
            fh_dent_t *e = (fh_dent_t *)(void *)(h->buf + off);
            if (e->reclen < DENT_HDR || (e->reclen & 7) || off + e->reclen > BLK) return FH_ECORRUPT;
            if (e->ino && e->namelen == nlen && fh_cmp(e->name, name, nlen) == 0) {
                e->ino = 0;
                if (prev != 0xFFFFFFFFu) {
                    fh_dent_t *p = (fh_dent_t *)(void *)(h->buf + prev);
                    p->reclen = (uint16_t)(p->reclen + e->reclen);
                }
                h->dirty = true; fs->dirty_bytes += 64;
                return FH_OK;
            }
            prev = off; off += e->reclen;
        }
    }
    return FH_ENOENT;
}

static int dir_nth(fhfs_t *fs, fhfs_inode_t *dir, uint32_t index, char *name, size_t name_sz,
                   uint64_t *ino, uint8_t *type) {
    uint64_t nb = dir->d.size / BLK; uint32_t seen = 0;
    for (uint64_t b = 0; b < nb; b++) {
        fh_hot_t *h; int r = hot_get(fs, dir, (int64_t)b, true, &h); if (r) return r;
        uint32_t off = 0;
        while (off < BLK) {
            fh_dent_t *e = (fh_dent_t *)(void *)(h->buf + off);
            if (e->reclen < DENT_HDR || (e->reclen & 7) || off + e->reclen > BLK) return FH_ECORRUPT;
            if (e->ino) {
                if (seen == index) {
                    uint32_t n = e->namelen; if (n + 1 > name_sz) n = (uint32_t)name_sz - 1;
                    fh_cpy(name, e->name, n); name[n] = 0;
                    if (ino) *ino = e->ino; if (type) *type = e->type;
                    return FH_OK;
                }
                seen++;
            }
            off += e->reclen;
        }
    }
    return FH_ENOENT;
}

static bool dir_is_empty(fhfs_t *fs, fhfs_inode_t *dir) {
    char nm[8]; return dir_nth(fs, dir, 0, nm, sizeof(nm), NULL, NULL) == FH_ENOENT;
}

/* ------------------------------------------------------------------ */
/* extended attributes (one 4K block per inode) + POSIX ACL            */
/* ------------------------------------------------------------------ */
#define ACL_ACCESS  "system.posix_acl_access"
#define ACL_DEFAULT "system.posix_acl_default"
#define ACL_VERSION 2u
#define ACL_USER_OBJ 0x01
#define ACL_USER     0x02
#define ACL_GROUP_OBJ 0x04
#define ACL_GROUP    0x08
#define ACL_MASK     0x10
#define ACL_OTHER    0x20
#define ACL_MAX_VAL  (4u + 8u * 64u)

static int xa_read(fhfs_t *fs, fhfs_inode_t *ip, uint8_t *blk) {
    fh_set(blk, 0, BLK);
    if (!ip->d.xattr_bp.blk) return FH_OK;
    return fh_read_bp(fs, &ip->d.xattr_bp, blk);
}
static int xa_write(fhfs_t *fs, fhfs_inode_t *ip, const uint8_t *blk) {
    fh_bp_t nb, old = ip->d.xattr_bp;
    if (blk[0] == 0 && blk[1] == 0) { fh_set(&nb, 0, sizeof(nb)); }
    else { int r = fh_write_bp(fs, blk, BLK, true, false, &nb); if (r) return r; }
    ip->d.xattr_bp = nb;
    fh_free_bp(fs, &old);
    ip->d.ctime = fhfs_time_ns();
    fh_dirty(ip);
    return FH_OK;
}
/* returns entry offset or -1 */
static int xa_find(const uint8_t *blk, const char *name, uint32_t nlen, uint32_t *vlen, uint32_t *voff, uint32_t *end) {
    uint32_t off = 0;
    while (off + 4 <= BLK) {
        uint32_t nl = (uint32_t)blk[off] | ((uint32_t)blk[off + 1] << 8);
        uint32_t vl = (uint32_t)blk[off + 2] | ((uint32_t)blk[off + 3] << 8);
        if (nl == 0) { if (end) *end = off; return -1; }
        uint32_t tot = (4 + nl + vl + 3u) & ~3u;
        if (off + tot > BLK) return -2;
        if (nl == nlen && fh_cmp(blk + off + 4, name, nlen) == 0) {
            if (vlen) *vlen = vl; if (voff) *voff = off + 4 + nl; if (end) *end = off; return (int)off;
        }
        off += tot;
    }
    if (end) *end = off;
    return -1;
}

static int xa_get_int(fhfs_t *fs, fhfs_inode_t *ip, const char *name, void *out, size_t cap) {
    if (!ip->d.xattr_bp.blk) return FH_ENODATA;
    uint8_t *blk = (uint8_t *)kmalloc(BLK); if (!blk) return FH_ENOMEM;
    int r = xa_read(fs, ip, blk);
    if (!r) {
        uint32_t vl, vo;
        int e = xa_find(blk, name, fh_slen(name), &vl, &vo, NULL);
        if (e < 0) r = (e == -2) ? FH_ECORRUPT : FH_ENODATA;
        else if (!out || !cap) r = (int)vl;
        else if (vl > cap) r = FH_ERANGE;
        else { fh_cpy(out, blk + vo, vl); r = (int)vl; }
    }
    kfree(blk);
    return r;
}

static int xa_set_int(fhfs_t *fs, fhfs_inode_t *ip, const char *name, const void *val, size_t sz, int flags) {
    uint32_t nl = fh_slen(name);
    if (nl == 0 || nl > 255) return FH_EINVAL;
    if (sz > 3800) return FH_E2BIG;
    uint8_t *blk = (uint8_t *)kmalloc(BLK); if (!blk) return FH_ENOMEM;
    int r = xa_read(fs, ip, blk);
    if (r) { kfree(blk); return r; }
    uint32_t vl, vo, end;
    int e = xa_find(blk, name, nl, &vl, &vo, &end);
    if (e == -2) { kfree(blk); return FH_ECORRUPT; }
    if (e >= 0 && (flags & 1)) { kfree(blk); return FH_EEXIST; }      /* XATTR_CREATE */
    if (e < 0 && (flags & 2)) { kfree(blk); return FH_ENODATA; }      /* XATTR_REPLACE */
    if (e >= 0) {                                                    /* remove the old entry first */
        uint32_t tot = (4 + nl + vl + 3u) & ~3u;
        fh_move(blk + e, blk + e + tot, BLK - (uint32_t)e - tot);
        fh_set(blk + BLK - tot, 0, tot);
        xa_find(blk, name, nl, NULL, NULL, &end);
    }
    uint32_t tot = (4 + nl + (uint32_t)sz + 3u) & ~3u;
    if (end + tot + 4 > BLK) { kfree(blk); return FH_ENOSPC; }
    blk[end] = (uint8_t)nl; blk[end + 1] = (uint8_t)(nl >> 8);
    blk[end + 2] = (uint8_t)sz; blk[end + 3] = (uint8_t)(sz >> 8);
    fh_cpy(blk + end + 4, name, nl); fh_cpy(blk + end + 4 + nl, val, sz);
    r = xa_write(fs, ip, blk);
    kfree(blk);
    return r;
}

static int xa_remove_int(fhfs_t *fs, fhfs_inode_t *ip, const char *name) {
    if (!ip->d.xattr_bp.blk) return FH_ENODATA;
    uint32_t nl = fh_slen(name);
    uint8_t *blk = (uint8_t *)kmalloc(BLK); if (!blk) return FH_ENOMEM;
    int r = xa_read(fs, ip, blk);
    if (!r) {
        uint32_t vl, vo, end;
        int e = xa_find(blk, name, nl, &vl, &vo, &end);
        if (e < 0) r = (e == -2) ? FH_ECORRUPT : FH_ENODATA;
        else {
            uint32_t tot = (4 + nl + vl + 3u) & ~3u;
            fh_move(blk + e, blk + e + tot, BLK - (uint32_t)e - tot);
            fh_set(blk + BLK - tot, 0, tot);
            r = xa_write(fs, ip, blk);
        }
    }
    kfree(blk);
    return r;
}

typedef struct { uint16_t tag, perm; uint32_t id; } fh_acl_ent_t;

/* parse a Linux posix_acl xattr value; returns entry count or negative error */
static int acl_parse(const uint8_t *v, uint32_t len, fh_acl_ent_t *ents, uint32_t max) {
    if (len < 4 || ((len - 4) & 7)) return FH_EINVAL;
    uint32_t ver = (uint32_t)v[0] | ((uint32_t)v[1] << 8) | ((uint32_t)v[2] << 16) | ((uint32_t)v[3] << 24);
    if (ver != ACL_VERSION) return FH_EINVAL;
    uint32_t n = (len - 4) / 8; if (n > max) return FH_EINVAL;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *p = v + 4 + i * 8;
        ents[i].tag = (uint16_t)(p[0] | (p[1] << 8)); ents[i].perm = (uint16_t)(p[2] | (p[3] << 8));
        ents[i].id = (uint32_t)p[4] | ((uint32_t)p[5] << 8) | ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
        if (ents[i].perm & ~7u) return FH_EINVAL;
    }
    return (int)n;
}
static uint32_t acl_emit(const fh_acl_ent_t *ents, uint32_t n, uint8_t *v) {
    v[0] = ACL_VERSION; v[1] = v[2] = v[3] = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *p = v + 4 + i * 8;
        p[0] = (uint8_t)ents[i].tag; p[1] = (uint8_t)(ents[i].tag >> 8);
        p[2] = (uint8_t)ents[i].perm; p[3] = (uint8_t)(ents[i].perm >> 8);
        p[4] = (uint8_t)ents[i].id; p[5] = (uint8_t)(ents[i].id >> 8);
        p[6] = (uint8_t)(ents[i].id >> 16); p[7] = (uint8_t)(ents[i].id >> 24);
    }
    return 4 + n * 8;
}
/* strict validation like Linux posix_acl_valid() */
static bool acl_valid(const fh_acl_ent_t *e, uint32_t n) {
    int uo = 0, go = 0, ot = 0, mk = 0, nu = 0, ng = 0;
    for (uint32_t i = 0; i < n; i++) {
        switch (e[i].tag) {
            case ACL_USER_OBJ: uo++; break; case ACL_GROUP_OBJ: go++; break; case ACL_OTHER: ot++; break;
            case ACL_MASK: mk++; break; case ACL_USER: nu++; break; case ACL_GROUP: ng++; break;
            default: return false;
        }
    }
    if (uo != 1 || go != 1 || ot != 1 || mk > 1) return false;
    if ((nu || ng) && !mk) return false;
    return true;
}
static uint32_t acl_to_mode(const fh_acl_ent_t *e, uint32_t n, uint32_t old_mode) {
    uint32_t uo = 0, go = 0, ot = 0, mk = 0; bool has_mask = false;
    for (uint32_t i = 0; i < n; i++) {
        if (e[i].tag == ACL_USER_OBJ) uo = e[i].perm; else if (e[i].tag == ACL_GROUP_OBJ) go = e[i].perm;
        else if (e[i].tag == ACL_OTHER) ot = e[i].perm; else if (e[i].tag == ACL_MASK) { mk = e[i].perm; has_mask = true; }
    }
    return (old_mode & ~0777u) | (uo << 6) | ((has_mask ? mk : go) << 3) | ot;
}

static bool cred_in_group(const fhfs_cred_t *cr, uint32_t gid) {
    if (cr->gid == gid) return true;
    for (uint32_t i = 0; i < cr->ngroups && i < FHFS_MAX_GROUPS; i++) if (cr->groups[i] == gid) return true;
    return false;
}

static int fh_perm_l(fhfs_t *fs, fhfs_inode_t *ip, const fhfs_cred_t *cr, uint32_t want) {
    if (!cr) return FH_OK;
    uint32_t mode = ip->d.mode;
    if (cr->uid == 0) {
        if ((want & FH_MAY_EXEC) && !FH_S_ISDIR(mode) && !(mode & 0111)) return FH_EACCES;
        return FH_OK;
    }
    uint8_t v[ACL_MAX_VAL]; fh_acl_ent_t ents[64]; int n = -1;
    if (ip->d.xattr_bp.blk) {
        int l = xa_get_int(fs, ip, ACL_ACCESS, v, sizeof(v));
        if (l > 0) n = acl_parse(v, (uint32_t)l, ents, 64);
    }
    uint32_t perm = 0;
    if (n < 0) {
        if (cr->uid == ip->d.uid) perm = (mode >> 6) & 7;
        else if (cred_in_group(cr, ip->d.gid)) perm = (mode >> 3) & 7;
        else perm = mode & 7;
        return ((perm & want) == want) ? FH_OK : FH_EACCES;
    }
    uint32_t mask = 7;
    for (int i = 0; i < n; i++) if (ents[i].tag == ACL_MASK) mask = ents[i].perm;
    if (cr->uid == ip->d.uid) {
        for (int i = 0; i < n; i++) if (ents[i].tag == ACL_USER_OBJ) perm = ents[i].perm, i = n;
        return ((perm & want) == want) ? FH_OK : FH_EACCES;
    }
    for (int i = 0; i < n; i++)
        if (ents[i].tag == ACL_USER && ents[i].id == cr->uid) return (((ents[i].perm & mask) & want) == want) ? FH_OK : FH_EACCES;
    bool matched = false;
    for (int i = 0; i < n; i++) {
        bool hit = (ents[i].tag == ACL_GROUP_OBJ && cred_in_group(cr, ip->d.gid)) ||
                   (ents[i].tag == ACL_GROUP && cred_in_group(cr, ents[i].id));
        if (hit) { matched = true; if ((((ents[i].perm & mask) & want) == want)) return FH_OK; }
    }
    if (matched) return FH_EACCES;
    perm = mode & 7;
    for (int i = 0; i < n; i++) if (ents[i].tag == ACL_OTHER) perm = ents[i].perm;
    return ((perm & want) == want) ? FH_OK : FH_EACCES;
}

/* ------------------------------------------------------------------ */
/* inode life cycle                                                    */
/* ------------------------------------------------------------------ */
static void fh_destroy_storage(fhfs_t *fs, fhfs_inode_t *ip) {
    hot_drop(fs, ip, 0);
    tree_destroy(fs, &ip->tree);
    fh_free_bp(fs, &ip->d.xattr_bp);
    uint64_t ino = ip->ino;
    fh_set(&ip->d, 0, sizeof(ip->d));
    ip->tdirty = false;
    fh_dirty(ip);
    if (ino < fs->ino_hint) fs->ino_hint = ino;
}

/* returns true if the whole fs object was freed (dead fs, last inode) */
static bool fh_iput_k(fhfs_inode_t *ip) {
    fhfs_t *fs = ip->fs;
    if (ip->ref == 0) return false;
    ip->ref--;
    if (fs->dead) {
        if (ip->ref == 0) { ic_unlink_free(fs, ip); if (fs->icount == 0) { kfree(fs); return true; } }
        return false;
    }
    /* an unlinked file is destroyed as soon as only our own record cache still holds it */
    if (ip->d.mode && ip->d.nlink == 0) {
        uint32_t h = 0;
        for (int i = 0; i < HOT_SLOTS; i++) if (fs->hot[i].used && fs->hot[i].ip == ip) h++;
        if (ip->ref == h) fh_destroy_storage(fs, ip);
    }
    return false;
}
static void fh_iput_l(fhfs_inode_t *ip) { fh_iput_k(ip); }

static fhfs_inode_t *fh_ialloc(fhfs_t *fs, int *err) {
    for (uint64_t ino = fs->ino_hint ? fs->ino_hint : 2; ino < (1ULL << 40); ino++) {
        fhfs_inode_t *ip = fh_iget(fs, ino, err);
        if (!ip) return NULL;
        if (ip->d.mode == 0 && ip->ref == 1) { fs->ino_hint = ino + 1; return ip; }
        fh_iput_l(ip);
    }
    *err = FH_ENOSPC; return NULL;
}

static int fh_acl_default_inherit(fhfs_t *fs, fhfs_inode_t *dir, fhfs_inode_t *ip, bool isdir) {
    if (!dir->d.xattr_bp.blk) return FH_OK;
    uint8_t v[ACL_MAX_VAL]; fh_acl_ent_t e[64];
    int l = xa_get_int(fs, dir, ACL_DEFAULT, v, sizeof(v));
    if (l <= 0) return FH_OK;
    int n = acl_parse(v, (uint32_t)l, e, 64);
    if (n < 3 || !acl_valid(e, (uint32_t)n)) return FH_OK;
    if (isdir) { int r = xa_set_int(fs, ip, ACL_DEFAULT, v, (size_t)l, 0); if (r) return r; }
    uint32_t m = ip->d.mode;
    bool has_mask = false; for (int i = 0; i < n; i++) if (e[i].tag == ACL_MASK) has_mask = true;
    for (int i = 0; i < n; i++) {
        if (e[i].tag == ACL_USER_OBJ) e[i].perm &= (m >> 6) & 7;
        else if (e[i].tag == ACL_MASK) e[i].perm &= (m >> 3) & 7;
        else if (e[i].tag == ACL_GROUP_OBJ && !has_mask) e[i].perm &= (m >> 3) & 7;
        else if (e[i].tag == ACL_OTHER) e[i].perm &= m & 7;
    }
    ip->d.mode = acl_to_mode(e, (uint32_t)n, m);
    if (n > 3) { uint32_t ol = acl_emit(e, (uint32_t)n, v); return xa_set_int(fs, ip, ACL_ACCESS, v, ol, 0); }
    return FH_OK;
}

static int fh_new_inode(fhfs_t *fs, fhfs_inode_t *dir, uint32_t mode, uint32_t uid, uint32_t gid,
                        uint64_t rdev, fhfs_inode_t **out) {
    int err; fhfs_inode_t *ip = fh_ialloc(fs, &err);
    if (!ip) return err;
    bool isdir = FH_S_ISDIR(mode);
    if (dir->d.mode & FH_S_ISGID) { gid = dir->d.gid; if (isdir) mode |= FH_S_ISGID; }
    uint64_t now = fhfs_time_ns();
    fh_set(&ip->d, 0, sizeof(ip->d));
    ip->d.mode = mode; ip->d.uid = uid; ip->d.gid = gid; ip->d.rdev = rdev;
    ip->d.nlink = isdir ? 2 : 1;
    ip->d.atime = ip->d.mtime = ip->d.ctime = ip->d.btime = now;
    ip->d.gen = (uint32_t)fs->open_txg;
    ip->d.rec_shift = (isdir || FH_S_ISLNK(mode)) ? 12 : (uint8_t)fs->rec_shift;
    ip->d.parent = dir->ino;
    fh_set(&ip->tree, 0, sizeof(ip->tree));
    fh_dirty(ip);
    if (!FH_S_ISLNK(mode)) {
        int r = fh_acl_default_inherit(fs, dir, ip, isdir);
        if (r) { ip->d.nlink = 0; fh_iput_l(ip); return r; }
    }
    *out = ip;
    return FH_OK;
}

static int fh_dir_check(fhfs_t *fs, fhfs_inode_t *dir, const fhfs_cred_t *cr, uint32_t want) {
    if (!FH_S_ISDIR(dir->d.mode)) return FH_ENOTDIR;
    if (dir->d.nlink == 0) return FH_ENOENT;
    return fh_perm_l(fs, dir, cr, want);
}

static int fh_create_l(fhfs_t *fs, fhfs_inode_t *dir, const char *name, uint32_t mode, uint32_t uid,
                       uint32_t gid, uint64_t rdev, const fhfs_cred_t *cr, fhfs_inode_t **out) {
    if (fs->ro) return FH_EROFS;
    uint32_t nl; int r = fh_name_ok(name, &nl); if (r) return r;
    if ((nl == 1 && name[0] == '.') || (nl == 2 && name[0] == '.' && name[1] == '.')) return FH_EEXIST;
    r = fh_dir_check(fs, dir, cr, FH_MAY_WRITE | FH_MAY_EXEC); if (r) return r;
    r = dir_find(fs, dir, name, nl, NULL, NULL);
    if (r == FH_OK) return FH_EEXIST;
    if (r != FH_ENOENT) return r;
    if (FH_S_ISDIR(mode) && dir->d.nlink >= FH_LINK_MAX) return FH_EMLINK;
    fhfs_inode_t *ip;
    r = fh_new_inode(fs, dir, mode, uid, gid, rdev, &ip); if (r) return r;
    r = dir_add(fs, dir, name, nl, ip->ino, fh_dt(mode));
    if (r) { ip->d.nlink = 0; fh_iput_l(ip); return r; }
    uint64_t now = fhfs_time_ns();
    if (FH_S_ISDIR(mode)) dir->d.nlink++;
    dir->d.mtime = dir->d.ctime = now;
    fh_dirty(dir);
    if (out) *out = ip; else fh_iput_l(ip);
    return FH_OK;
}

static int fh_lookup_l(fhfs_t *fs, fhfs_inode_t *dir, const char *name, const fhfs_cred_t *cr, fhfs_inode_t **out) {
    int r = fh_dir_check(fs, dir, cr, FH_MAY_EXEC); if (r) return r;
    uint32_t nl = fh_slen(name);
    if (nl == 1 && name[0] == '.') { dir->ref++; *out = dir; return FH_OK; }
    if (nl == 2 && name[0] == '.' && name[1] == '.') {
        int e; fhfs_inode_t *p = fh_iget(fs, dir->d.parent ? dir->d.parent : dir->ino, &e);
        if (!p) return e; *out = p; return FH_OK;
    }
    if (nl == 0) return FH_EINVAL;
    if (nl > FHFS_NAME_MAX) return FH_ENAMETOOLONG;
    uint64_t ino; r = dir_find(fs, dir, name, nl, &ino, NULL); if (r) return r;
    int e; fhfs_inode_t *ip = fh_iget(fs, ino, &e);
    if (!ip) return e;
    if (ip->d.mode == 0) { fh_iput_l(ip); return FH_ECORRUPT; }
    *out = ip; return FH_OK;
}

static int fh_sticky_ok(fhfs_inode_t *dir, fhfs_inode_t *victim, const fhfs_cred_t *cr) {
    if (!cr || cr->uid == 0 || !(dir->d.mode & FH_S_ISVTX)) return FH_OK;
    return (cr->uid == victim->d.uid || cr->uid == dir->d.uid) ? FH_OK : FH_EPERM;
}

static int fh_unlink_l(fhfs_t *fs, fhfs_inode_t *dir, const char *name, const fhfs_cred_t *cr, bool rmdir) {
    if (fs->ro) return FH_EROFS;
    uint32_t nl; int r = fh_name_ok(name, &nl); if (r) return r;
    if (nl == 1 && name[0] == '.') return rmdir ? FH_EINVAL : FH_EISDIR;
    if (nl == 2 && name[0] == '.' && name[1] == '.') return rmdir ? FH_ENOTEMPTY : FH_EISDIR;
    r = fh_dir_check(fs, dir, cr, FH_MAY_WRITE | FH_MAY_EXEC); if (r) return r;
    uint64_t ino; r = dir_find(fs, dir, name, nl, &ino, NULL); if (r) return r;
    int e; fhfs_inode_t *ip = fh_iget(fs, ino, &e); if (!ip) return e;
    r = fh_sticky_ok(dir, ip, cr);
    if (!r) {
        bool isdir = FH_S_ISDIR(ip->d.mode);
        if (rmdir && !isdir) r = FH_ENOTDIR;
        else if (!rmdir && isdir) r = FH_EISDIR;
        else if (rmdir && !dir_is_empty(fs, ip)) r = FH_ENOTEMPTY;
    }
    if (!r) r = dir_remove(fs, dir, name, nl);
    if (!r) {
        uint64_t now = fhfs_time_ns();
        if (FH_S_ISDIR(ip->d.mode)) { ip->d.nlink = 0; dir->d.nlink--; }
        else if (ip->d.nlink) ip->d.nlink--;
        ip->d.ctime = now; dir->d.mtime = dir->d.ctime = now;
        fh_dirty(ip); fh_dirty(dir);
    }
    fh_iput_l(ip);
    return r;
}

static int fh_link_l(fhfs_t *fs, fhfs_inode_t *dir, const char *name, fhfs_inode_t *tgt, const fhfs_cred_t *cr) {
    if (fs->ro) return FH_EROFS;
    uint32_t nl; int r = fh_name_ok(name, &nl); if (r) return r;
    if (FH_S_ISDIR(tgt->d.mode)) return FH_EPERM;
    if (tgt->d.nlink == 0) return FH_ENOENT;
    if (tgt->d.nlink >= FH_LINK_MAX) return FH_EMLINK;
    r = fh_dir_check(fs, dir, cr, FH_MAY_WRITE | FH_MAY_EXEC); if (r) return r;
    r = dir_find(fs, dir, name, nl, NULL, NULL);
    if (r == FH_OK) return FH_EEXIST;
    if (r != FH_ENOENT) return r;
    r = dir_add(fs, dir, name, nl, tgt->ino, fh_dt(tgt->d.mode)); if (r) return r;
    uint64_t now = fhfs_time_ns();
    tgt->d.nlink++; tgt->d.ctime = now; dir->d.mtime = dir->d.ctime = now;
    fh_dirty(tgt); fh_dirty(dir);
    return FH_OK;
}

static int fh_rename_l(fhfs_t *fs, fhfs_inode_t *od, const char *on, fhfs_inode_t *nd, const char *nn,
                       const fhfs_cred_t *cr) {
    if (fs->ro) return FH_EROFS;
    uint32_t ol, nl; int r;
    if ((r = fh_name_ok(on, &ol)) || (r = fh_name_ok(nn, &nl))) return r;
    if ((ol == 1 && on[0] == '.') || (ol == 2 && on[0] == '.' && on[1] == '.') ||
        (nl == 1 && nn[0] == '.') || (nl == 2 && nn[0] == '.' && nn[1] == '.')) return FH_EINVAL;
    if ((r = fh_dir_check(fs, od, cr, FH_MAY_WRITE | FH_MAY_EXEC))) return r;
    if ((r = fh_dir_check(fs, nd, cr, FH_MAY_WRITE | FH_MAY_EXEC))) return r;

    uint64_t sino, dino = 0; uint8_t stype;
    if ((r = dir_find(fs, od, on, ol, &sino, &stype))) return r;
    int e; fhfs_inode_t *si = fh_iget(fs, sino, &e); if (!si) return e;
    fhfs_inode_t *di = NULL;
    r = fh_sticky_ok(od, si, cr);
    bool sdir = FH_S_ISDIR(si->d.mode);

    if (!r) {
        int fr = dir_find(fs, nd, nn, nl, &dino, NULL);
        if (fr == FH_OK) {
            di = fh_iget(fs, dino, &e);
            if (!di) r = e;
            else if (di == si) { fh_iput_l(di); fh_iput_l(si); return FH_OK; }
            else {
                bool ddir = FH_S_ISDIR(di->d.mode);
                if (sdir && !ddir) r = FH_ENOTDIR;
                else if (!sdir && ddir) r = FH_EISDIR;
                else if (ddir && !dir_is_empty(fs, di)) r = FH_ENOTEMPTY;
                else r = fh_sticky_ok(nd, di, cr);
            }
        } else if (fr != FH_ENOENT) r = fr;
    }
    if (!r && sdir) {                          /* refuse to move a directory into itself */
        fhfs_inode_t *a = nd; a->ref++;
        for (int guard = 0; guard < 4096; guard++) {
            if (a->ino == si->ino) { r = FH_EINVAL; break; }
            if (a->ino == FH_ROOT_INO || !a->d.parent || a->d.parent == a->ino) break;
            fhfs_inode_t *p = fh_iget(fs, a->d.parent, &e);
            fh_iput_l(a);
            if (!p) { a = NULL; r = e; break; }
            a = p;
        }
        if (a) fh_iput_l(a);
    }
    if (!r && sdir && od != nd && nd->d.nlink >= FH_LINK_MAX) r = FH_EMLINK;
    if (!r && di) {
        r = dir_remove(fs, nd, nn, nl);
        if (!r) {
            if (FH_S_ISDIR(di->d.mode)) { di->d.nlink = 0; nd->d.nlink--; }
            else if (di->d.nlink) di->d.nlink--;
            di->d.ctime = fhfs_time_ns(); fh_dirty(di);
        }
    }
    if (!r) r = dir_add(fs, nd, nn, nl, si->ino, stype);
    if (!r) r = dir_remove(fs, od, on, ol);
    if (!r) {
        uint64_t now = fhfs_time_ns();
        if (sdir && od != nd) { si->d.parent = nd->ino; od->d.nlink--; nd->d.nlink++; }
        si->d.ctime = now;
        od->d.mtime = od->d.ctime = now; nd->d.mtime = nd->d.ctime = now;
        fh_dirty(si); fh_dirty(od); fh_dirty(nd);
    }
    if (di) fh_iput_l(di);
    fh_iput_l(si);
    return r;
}

/* ------------------------------------------------------------------ */
/* atomic commit                                                       */
/* ------------------------------------------------------------------ */
static int fh_write_uber(fhfs_t *fs, uint64_t txg) {
    fh_uber_t *u = (fh_uber_t *)(void *)fs->tmpbuf;
    fh_set(u, 0, BLK);
    u->magic = FH_UB_MAGIC; u->txg = txg; u->time_ns = fhfs_time_ns();
    u->inode_root = fs->itree.root; u->inode_depth = fs->itree.depth;
    u->used_blocks = fs->used; u->snap_root = fs->snap_bp;
    u->cksum = fh_cksum(u, offsetof(fh_uber_t, cksum));
    uint32_t slot = (uint32_t)(txg % UB_SLOTS);
    int r = fh_bwrite(fs, 1 + slot, 1, u);
    if (r) return r;
    return fh_bwrite(fs, fs->total - UB_SLOTS + slot, 1, u);
}

static void fh_sort_inodes(fhfs_inode_t **a, uint32_t n) {
    for (uint32_t gap = n / 2; gap > 0; gap /= 2)
        for (uint32_t i = gap; i < n; i++) {
            fhfs_inode_t *t = a[i]; uint32_t j = i;
            while (j >= gap && a[j - gap]->ino > t->ino) { a[j] = a[j - gap]; j -= gap; }
            a[j] = t;
        }
}

#define FH_SNAP_MAGIC 0x50414E53584F46ULL
typedef struct __attribute__((packed)) {
    char name[48]; uint64_t id, txg, time_ns; fh_bp_t root; uint32_t depth, flags;
} fh_snap_disk_t;
typedef struct __attribute__((packed)) { uint64_t magic; uint32_t count, pad; uint64_t next_id; fh_snap_disk_t s[36]; } fh_snap_tab_t;

static int fh_snap_write(fhfs_t *fs) {
    fh_bp_t old = fs->snap_bp, nb;
    fh_set(&nb, 0, sizeof(nb));
    if (fs->nsnaps) {
        fh_snap_tab_t *t = (fh_snap_tab_t *)fh_zalloc(BLK); if (!t) return FH_ENOMEM;
        t->magic = FH_SNAP_MAGIC; t->count = fs->nsnaps; t->next_id = fs->snap_next_id;
        for (uint32_t i = 0; i < fs->nsnaps; i++) {
            fh_cpy(t->s[i].name, fs->snaps[i].name, 48); t->s[i].id = fs->snaps[i].id; t->s[i].txg = fs->snaps[i].txg;
            t->s[i].time_ns = fs->snaps[i].time_ns; t->s[i].root = fs->snaps[i].root; t->s[i].depth = fs->snaps[i].depth;
        }
        int r = fh_write_bp(fs, t, BLK, true, false, &nb);
        kfree(t);
        if (r) return r;
    }
    fs->snap_bp = nb; fs->snap_dirty = false;
    fh_free_bp(fs, &old);
    return FH_OK;
}

static int fh_commit(fhfs_t *fs) {
    if (fs->ro || fs->dead) return FH_OK;
    if (!fs->gdirty && !fs->npend && fs->txg > 0) return FH_OK;
    int r;
    for (int i = 0; i < HOT_SLOTS; i++) { r = hot_flush(fs, &fs->hot[i]); if (r) return r; }

    uint32_t nd = 0;
    for (uint32_t b = 0; b < IHASH; b++)
        for (fhfs_inode_t *p = fs->ihash[b]; p; p = p->hnext) if (p->dirty || p->tdirty) nd++;
    fhfs_inode_t **arr = nd ? (fhfs_inode_t **)kmalloc((uint64_t)nd * sizeof(*arr)) : NULL;
    if (nd && !arr) return FH_ENOMEM;
    uint32_t k = 0;
    for (uint32_t b = 0; b < IHASH; b++)
        for (fhfs_inode_t *p = fs->ihash[b]; p; p = p->hnext) if ((p->dirty || p->tdirty) && k < nd) arr[k++] = p;
    nd = k;

    for (uint32_t i = 0; i < nd; i++) {
        fhfs_inode_t *p = arr[i];
        if (p->tdirty) {
            r = tree_flush(fs, &p->tree); if (r) goto fail;
            p->d.data_root = p->tree.root; p->d.data_depth = (uint8_t)p->tree.depth;
            p->tdirty = false; p->dirty = true;
        }
    }
    fh_sort_inodes(arr, nd);
    for (uint32_t i = 0; i < nd;) {
        uint64_t leaf = arr[i]->ino / 16;
        fh_bp_t bp, old;
        r = tree_lookup(fs, &fs->itree, leaf, &bp); if (r) goto fail;
        uint8_t *lb = (uint8_t *)kmalloc(BLK); if (!lb) { r = FH_ENOMEM; goto fail; }
        fh_set(lb, 0, BLK);
        if (bp.blk) { r = fh_read_bp(fs, &bp, lb); if (r) { kfree(lb); goto fail; } }
        uint32_t j = i;
        while (j < nd && arr[j]->ino / 16 == leaf) {
            fh_dinode_t *d = &((fh_dinode_t *)(void *)lb)[arr[j]->ino % 16];
            *d = arr[j]->d;
            d->cksum = arr[j]->d.mode ? fh_dinode_ck(d) : 0;
            j++;
        }
        fh_bp_t nbp;
        if (fh_is_zero(lb, BLK)) fh_set(&nbp, 0, sizeof(nbp));
        else { r = fh_write_bp(fs, lb, BLK, true, false, &nbp); if (r) { kfree(lb); goto fail; } }
        kfree(lb);
        r = tree_set(fs, &fs->itree, leaf, &nbp, &old);
        if (r) goto fail;
        fh_free_bp(fs, &old);
        for (uint32_t m = i; m < j; m++) arr[m]->dirty = false;
        i = j;
    }
    r = tree_flush(fs, &fs->itree); if (r) goto fail;
    if (fs->snap_dirty) { r = fh_snap_write(fs); if (r) goto fail; }

    fh_barrier(fs);                                /* all data + metadata durable BEFORE the switch */
    r = fh_write_uber(fs, fs->open_txg); if (r) goto fail;
    fh_barrier(fs);

    fs->txg = fs->open_txg; fs->open_txg++;
    for (uint32_t i = 0; i < fs->npend; i++) bm_unmark(fs, fs->pend[i].blk, fs->pend[i].n);
    fs->npend = 0;
    fs->gdirty = false; fs->dirty_bytes = 0; fs->last_commit_ms = fh_now_ms();
    if (arr) kfree(arr);
    return FH_OK;
fail:
    if (arr) kfree(arr);
    fs->gdirty = true;
    return r;
}

/* ------------------------------------------------------------------ */
/* media detection + geometry                                          */
/* ------------------------------------------------------------------ */
static bool fh_prefix(const char *s, const char *p) { while (*p) { if (*s++ != *p++) return false; } return true; }

static uint32_t fh_probe_media(blkdev_t *dev) {
    if (fh_prefix(dev->name, "nv")) return FH_MEDIA_SSD;
    if (fh_prefix(dev->name, "ud")) return FH_MEDIA_FLASH;
    if (fh_prefix(dev->name, "vd")) return FH_MEDIA_SSD;
    uint64_t per = usb_tsc_per_ms();
    if (!per || dev->sector_count < 2048) return FH_MEDIA_SSD;
    /* random-read latency probe: a spinning disk needs milliseconds per seek */
    uint8_t *buf = (uint8_t *)kmalloc(4096); if (!buf) return FH_MEDIA_SSD;
    uint64_t seed = 0x9E3779B97F4A7C15ULL, total = 0; int n = 0;
    for (int i = 0; i < 12; i++) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        uint64_t lba = (seed >> 16) % (dev->sector_count - 8);
        uint64_t t0 = fh_rdtsc();
        if (blkdev_read(dev, lba, 1, buf) != VFS_OK) continue;
        if (i >= 2) { total += fh_rdtsc() - t0; n++; }       /* first reads warm the path */
    }
    kfree(buf);
    if (!n) return FH_MEDIA_SSD;
    return (total / (uint64_t)n) > per * 2 ? FH_MEDIA_HDD : FH_MEDIA_SSD;
}

static void fh_geometry(fh_label_t *lab, uint64_t total, uint32_t media) {
    lab->media = media;
    lab->rec_shift = media == FH_MEDIA_HDD ? 17 : (media == FH_MEDIA_SSD ? 15 : 16);
    lab->seg_blocks = media == FH_MEDIA_HDD ? 16384 : (media == FH_MEDIA_SSD ? 1024 : 2048);
    lab->data_start = 256; lab->data_end = total - UB_SLOTS;
    uint64_t at = lab->data_end - lab->data_start;
    lab->meta_start = lab->data_start; lab->meta_blocks = 0;
    if (media == FH_MEDIA_HDD) {                  /* metadata zone: ~3% at the start, data streams after it */
        uint64_t mb = at / 32; if (mb < 256) mb = 256; if (mb > 262144) mb = 262144;
        if (mb * 4 < at) lab->meta_blocks = mb;
    }
}

static int fh_setup(fhfs_t *fs, blkdev_t *dev, const fh_label_t *lab) {
    fs->dev = dev; fs->spb = BLK / dev->sector_size; fs->lab = *lab;
    fs->total = lab->total_blocks; fs->media = lab->media; fs->rec_shift = lab->rec_shift;
    fs->rec_size = 1u << lab->rec_shift;
    fs->data_start = lab->data_start; fs->data_end = lab->data_end;
    fs->meta_lo = lab->meta_start; fs->meta_hi = lab->meta_start + lab->meta_blocks;
    fs->dstart = fs->meta_hi > fs->meta_lo ? fs->meta_hi : fs->data_start;
    fs->cur_meta = fs->meta_lo; fs->cur_data = fs->dstart;
    fs->compress = true;
    fs->bm_words = (fs->total + 63) / 64;
    fs->bm = (uint64_t *)fh_zalloc(fs->bm_words * 8);
    fs->iobuf = (uint8_t *)kmalloc(fs->rec_size + 2 * BLK);
    fs->cbuf = (uint8_t *)kmalloc(fs->rec_size + 2 * BLK);
    fs->tmpbuf = (uint8_t *)kmalloc(BLK);
    fs->lz_ht = (uint32_t *)fh_zalloc((1u << LZ_HBITS) * 4);
    fs->ino_hint = 2;
    if (!fs->bm || !fs->iobuf || !fs->cbuf || !fs->tmpbuf || !fs->lz_ht) return FH_ENOMEM;
    return FH_OK;
}

static void fh_teardown_buffers(fhfs_t *fs) {
    for (int i = 0; i < HOT_SLOTS; i++) if (fs->hot[i].buf) { kfree(fs->hot[i].buf); fs->hot[i].buf = NULL; }
    if (fs->bm) kfree(fs->bm); if (fs->iobuf) kfree(fs->iobuf); if (fs->cbuf) kfree(fs->cbuf);
    if (fs->tmpbuf) kfree(fs->tmpbuf); if (fs->lz_ht) kfree(fs->lz_ht); if (fs->pend) kfree(fs->pend);
    if (fs->rc) { kfree(fs->rc); fs->rc = NULL; fs->rc_cap = fs->rc_n = 0; }
    fs->bm = NULL; fs->iobuf = fs->cbuf = fs->tmpbuf = NULL; fs->lz_ht = NULL; fs->pend = NULL;
}

/* ------------------------------------------------------------------ */
/* format                                                              */
/* ------------------------------------------------------------------ */
int fhfs_format(blkdev_t *dev, const char *label, uint32_t media) {
    fh_crc_init();
    if (!dev || dev->sector_size == 0 || dev->sector_size > BLK || (BLK % dev->sector_size)) return FH_EINVAL;
    if (dev->readonly) return FH_EROFS;
    uint64_t total = dev->sector_count * dev->sector_size / BLK;
    if (total < 1024) return FH_EINVAL;
    if (!media) media = fh_probe_media(dev);

    fh_label_t lab; fh_set(&lab, 0, sizeof(lab));
    lab.magic = FH_MAGIC; lab.version = FH_VERSION; lab.blk_size = BLK; lab.total_blocks = total;
    fh_geometry(&lab, total, media);
    lab.created_ns = fhfs_time_ns();
    uint64_t r1 = fh_rdtsc() * 0x9E3779B97F4A7C15ULL, r2 = lab.created_ns ^ (r1 >> 7);
    fh_cpy(lab.uuid, &r1, 8); fh_cpy(lab.uuid + 8, &r2, 8);
    if (label) { uint32_t n = fh_slen(label); if (n > 31) n = 31; fh_cpy(lab.label, label, n); }
    lab.cksum = fh_cksum(&lab, offsetof(fh_label_t, cksum));

    fhfs_t *fs = (fhfs_t *)fh_zalloc(sizeof(*fs));
    if (!fs) return FH_ENOMEM;
    int r = fh_setup(fs, dev, &lab);
    if (r) { fh_teardown_buffers(fs); kfree(fs); return r; }
    fs->txg = 0; fs->open_txg = 1;

    uint8_t *z = (uint8_t *)fh_zalloc(BLK * UB_SLOTS);
    if (!z) { fh_teardown_buffers(fs); kfree(fs); return FH_ENOMEM; }
    r = fh_bwrite(fs, 0, 1, z);                                          /* kill any old label first */
    if (!r) r = fh_bwrite(fs, 1, UB_SLOTS, z);
    if (!r) r = fh_bwrite(fs, total - UB_SLOTS, UB_SLOTS, z);
    kfree(z);

    fhfs_inode_t *root = NULL;
    if (!r) {
        int e; root = fh_iget(fs, FH_ROOT_INO, &e);
        if (!root) r = e;
    }
    if (!r) {
        uint64_t now = fhfs_time_ns();
        fh_set(&root->d, 0, sizeof(root->d));
        root->d.mode = FH_S_IFDIR | 0755; root->d.nlink = 2; root->d.rec_shift = 12; root->d.parent = FH_ROOT_INO;
        root->d.atime = root->d.mtime = root->d.ctime = root->d.btime = now;
        fh_dirty(root);
        r = fh_commit(fs);
    }
    if (!r) {
        uint8_t *lb = (uint8_t *)fh_zalloc(BLK);
        if (!lb) r = FH_ENOMEM;
        else { fh_cpy(lb, &lab, sizeof(lab)); r = fh_bwrite(fs, 0, 1, lb); kfree(lb); fh_barrier(fs); }
    }
    if (root) fh_iput_l(root);
    fh_umount_core(fs);                                                  /* frees fs */
    return r;
}

/* ------------------------------------------------------------------ */
/* mount: pick the newest valid uberblock, rebuild the allocation map  */
/* ------------------------------------------------------------------ */
static fhfs_t *g_mounted[4];

typedef struct {
    uint64_t first_free, next_leaf;
    uint64_t orph[128]; uint32_t norph;
    bool     have_ff;
    bool     live;                /* scanning the live tree (not a snapshot) */
    bool     release;             /* drop one holder of every block instead of counting it */
} fh_scan_t;

/* count one holder of the block: first holder marks it used, every other one is a share */
static int fh_visit(fhfs_t *fs, const fh_bp_t *bp) {
    if (bm_test(fs, bp->blk)) return rc_inc(fs, bp->blk);
    bm_mark(fs, bp->blk, bp_nblk(bp));
    return FH_OK;
}
static int scan_ref(fhfs_t *fs, const fh_bp_t *bp, fh_scan_t *sc) {
    if (sc->release) { fh_free_bp(fs, bp); return FH_OK; }
    return fh_visit(fs, bp);
}

static int scan_walk(fhfs_t *fs, const fh_bp_t *bp, uint32_t level, uint64_t base, bool inode_tree, fh_scan_t *sc);

static int scan_inode_leaf(fhfs_t *fs, const fh_bp_t *bp, uint64_t leaf, fh_scan_t *sc) {
    uint8_t *buf = (uint8_t *)kmalloc(BLK); if (!buf) return FH_ENOMEM;
    int r = fh_read_bp(fs, bp, buf);
    if (r) { kfree(buf); return r; }
    if (sc->live && leaf > sc->next_leaf && !sc->have_ff) { sc->first_free = sc->next_leaf ? sc->next_leaf * 16 : 1; sc->have_ff = true; }
    sc->next_leaf = leaf + 1;
    for (uint32_t s = 0; s < 16; s++) {
        uint64_t ino = leaf * 16 + s;
        fh_dinode_t *d = &((fh_dinode_t *)(void *)buf)[s];
        if (ino == 0) continue;
        if (!d->mode) { if (sc->live && !sc->have_ff) { sc->first_free = ino; sc->have_ff = true; } continue; }
        if (d->cksum != fh_dinode_ck(d)) { fs->cksum_errors++; continue; }
        if (d->nlink == 0) { if (sc->live && sc->norph < 128) sc->orph[sc->norph++] = ino; continue; }
        if (d->xattr_bp.blk) { fh_bp_t xb = d->xattr_bp; r = scan_ref(fs, &xb, sc); if (r) { kfree(buf); return r; } }
        if (d->data_root.blk) {
            fh_bp_t root = d->data_root;
            r = scan_walk(fs, &root, d->data_depth, 0, false, sc);
            if (r) { kfree(buf); return r; }
        }
    }
    kfree(buf);
    return FH_OK;
}

static int scan_walk(fhfs_t *fs, const fh_bp_t *bp, uint32_t level, uint64_t base, bool inode_tree, fh_scan_t *sc) {
    if (!bp->blk) return FH_OK;
    if (bp->blk < fs->data_start || bp->blk + bp_nblk(bp) > fs->data_end) return FH_ECORRUPT;
    { int vr = scan_ref(fs, bp, sc); if (vr) return vr; }
    if (level == 0) return inode_tree ? scan_inode_leaf(fs, bp, base, sc) : FH_OK;
    uint8_t *node = (uint8_t *)kmalloc(BLK); if (!node) return FH_ENOMEM;
    int r = fh_bread(fs, bp->blk, 1, node);
    if (!r && fh_cksum(node, BLK) != bp->cksum) { fs->cksum_errors++; r = FH_ECORRUPT; }
    for (uint32_t i = 0; !r && i < FANOUT; i++) {
        fh_bp_t c = ((fh_bp_t *)(void *)node)[i];
        if (c.blk) r = scan_walk(fs, &c, level - 1, base + ((uint64_t)i << (7 * (level - 1))), inode_tree, sc);
    }
    kfree(node);
    return r;
}

static bool fh_label_ok(const fh_label_t *l, blkdev_t *dev) {
    if (l->magic != FH_MAGIC || l->version != FH_VERSION || l->blk_size != BLK) return false;
    if (l->cksum != fh_cksum(l, offsetof(fh_label_t, cksum))) return false;
    if (l->total_blocks * BLK > dev->sector_count * dev->sector_size) return false;
    if (l->data_start >= l->data_end || l->data_end + UB_SLOTS > l->total_blocks) return false;
    return true;
}

bool fhfs_probe(blkdev_t *dev) {
    fh_crc_init();
    if (!dev || dev->sector_size == 0 || dev->sector_size > BLK || (BLK % dev->sector_size)) return false;
    uint8_t *b = (uint8_t *)kmalloc(BLK); if (!b) return false;
    bool ok = (blkdev_read(dev, 0, BLK / dev->sector_size, b) == VFS_OK) && fh_label_ok((fh_label_t *)(void *)b, dev);
    kfree(b);
    return ok;
}

fhfs_t *fhfs_mount_dev(blkdev_t *dev, uint32_t flags, int *err) {
    int dummy; if (!err) err = &dummy;
    fh_crc_init();
    *err = FH_EINVAL;
    if (!dev || dev->sector_size == 0 || dev->sector_size > BLK || (BLK % dev->sector_size)) return NULL;
    fhfs_t *fs = (fhfs_t *)fh_zalloc(sizeof(*fs));
    if (!fs) { *err = FH_ENOMEM; return NULL; }
    fs->dev = dev; fs->spb = BLK / dev->sector_size;

    uint8_t *lb = (uint8_t *)kmalloc(BLK);
    if (!lb) { kfree(fs); *err = FH_ENOMEM; return NULL; }
    int r = fh_bread(fs, 0, 1, lb);
    fh_label_t lab = *(fh_label_t *)(void *)lb;
    kfree(lb);
    if (r) { kfree(fs); *err = r; return NULL; }
    if (!fh_label_ok(&lab, dev)) { kfree(fs); *err = FH_EINVAL; return NULL; }
    r = fh_setup(fs, dev, &lab);
    if (r) { fh_teardown_buffers(fs); kfree(fs); *err = r; return NULL; }
    fs->ro = dev->readonly || (flags & MS_RDONLY);
    if (flags & FHFS_MNT_NOCOMPRESS) fs->compress = false;

    /* newest valid uberblock of the 2 x 16 slots */
    fh_uber_t best; bool have = false;
    for (uint32_t i = 0; i < 2 * UB_SLOTS; i++) {
        uint64_t blk = (i < UB_SLOTS) ? 1 + i : fs->total - UB_SLOTS + (i - UB_SLOTS);
        if (fh_bread(fs, blk, 1, fs->tmpbuf)) continue;
        fh_uber_t *u = (fh_uber_t *)(void *)fs->tmpbuf;
        if (u->magic != FH_UB_MAGIC || u->cksum != fh_cksum(u, offsetof(fh_uber_t, cksum))) continue;
        if (!have || u->txg > best.txg) { best = *u; have = true; }
    }
    if (!have) { fh_teardown_buffers(fs); kfree(fs); *err = FH_ECORRUPT; return NULL; }
    fs->txg = best.txg; fs->open_txg = best.txg + 1;
    fs->itree.root = best.inode_root; fs->itree.depth = best.inode_depth;

    fh_scan_t *sc = (fh_scan_t *)fh_zalloc(sizeof(*sc));
    if (!sc) { fh_teardown_buffers(fs); kfree(fs); *err = FH_ENOMEM; return NULL; }
    sc->live = true;
    r = scan_walk(fs, &fs->itree.root, fs->itree.depth, 0, true, sc);
    if (!r && best.snap_root.blk) {                                  /* snapshots: every tree is one more holder */
        fh_snap_tab_t *tab = (fh_snap_tab_t *)kmalloc(BLK);
        if (!tab) r = FH_ENOMEM;
        else {
            r = fh_read_bp(fs, &best.snap_root, tab);
            if (!r && (tab->magic != FH_SNAP_MAGIC || tab->count > 36)) r = FH_ECORRUPT;
            if (!r) r = fh_visit(fs, &best.snap_root);
            for (uint32_t i = 0; !r && i < tab->count; i++) {
                fh_cpy(fs->snaps[i].name, tab->s[i].name, 48); fs->snaps[i].id = tab->s[i].id; fs->snaps[i].txg = tab->s[i].txg;
                fs->snaps[i].time_ns = tab->s[i].time_ns; fs->snaps[i].root = tab->s[i].root; fs->snaps[i].depth = tab->s[i].depth;
                fh_scan_t *s2 = (fh_scan_t *)fh_zalloc(sizeof(*s2));
                if (!s2) { r = FH_ENOMEM; break; }
                r = scan_walk(fs, &fs->snaps[i].root, fs->snaps[i].depth, 0, true, s2);
                kfree(s2);
            }
            if (!r) { fs->nsnaps = tab->count; fs->snap_next_id = tab->next_id; fs->snap_bp = best.snap_root; }
            kfree(tab);
        }
    }
    if (r) { kfree(sc); fh_teardown_buffers(fs); kfree(fs); *err = r; return NULL; }
    fs->ino_hint = sc->have_ff ? sc->first_free : (sc->next_leaf ? sc->next_leaf * 16 : 2);
    if (fs->ino_hint < 2) fs->ino_hint = 2;

    int e; fhfs_inode_t *root = fh_iget(fs, FH_ROOT_INO, &e);
    if (!root || !FH_S_ISDIR(root->d.mode)) {
        if (root) fh_iput_l(root);
        kfree(sc); fs->dead = true; fh_umount_core(fs);
        *err = root ? FH_ECORRUPT : e; return NULL;
    }
    fh_iput_l(root);
    /* unlinked-but-open files of a crashed session: free them now */
    for (uint32_t i = 0; i < sc->norph; i++) {
        fhfs_inode_t *o = fh_iget(fs, sc->orph[i], &e);
        if (!o) continue;
        fh_set(&o->d, 0, sizeof(o->d)); fh_dirty(o); fh_iput_l(o);
    }
    kfree(sc);
    fs->last_commit_ms = fh_now_ms();
    for (int i = 0; i < 4; i++) if (!g_mounted[i]) { g_mounted[i] = fs; break; }
    *err = FH_OK;
    return fs;
}

/* flush + free all caches.  Returns true when the fs object itself was freed. */
static bool fh_umount_core(fhfs_t *fs) {
    for (int i = 0; i < 4; i++) if (g_mounted[i] == fs) g_mounted[i] = NULL;
    for (int i = 0; i < HOT_SLOTS; i++) {
        fh_hot_t *h = &fs->hot[i];
        if (h->used) { h->used = false; h->dirty = false; if (h->ip) h->ip->ref--; h->ip = NULL; }
    }
    for (uint32_t b = 0; b < IHASH; b++) {
        fhfs_inode_t *p = fs->ihash[b];
        while (p) { fhfs_inode_t *nx = p->hnext; if (p->ref == 0) ic_unlink_free(fs, p); p = nx; }
    }
    if (fs->itree.rnode) { tn_free(fs->itree.rnode); fs->itree.rnode = NULL; }
    fh_teardown_buffers(fs);
    fs->dead = true;
    if (fs->icount == 0) { kfree(fs); return true; }
    return false;
}

int fhfs_umount_fs(fhfs_t *fs) {
    fhfs_t *parent = fs->parent;
    fh_lock(fs);
    if (fs->snap_mounts) { fh_unlock(fs); return FH_EBUSY; }
    int r = fh_commit(fs);
    if (!fh_umount_core(fs)) fh_unlock(fs);
    if (parent) { fh_lock(parent); parent->snap_mounts--; fh_unlock(parent); }
    return r;
}

int fhfs_sync(fhfs_t *fs) { fh_lock(fs); int r = fh_commit(fs); fh_unlock(fs); return r; }

void fhfs_statfs(fhfs_t *fs, fhfs_stat_t *o) {
    fh_lock(fs);
    uint64_t at = fs->data_end - fs->data_start;
    o->total_blocks = at; o->used_blocks = fs->used; o->free_blocks = at > fs->used ? at - fs->used : 0;
    o->block_size = BLK; o->record_size = fs->rec_size; o->media = fs->media; o->txg = fs->txg;
    o->checksum_errors = fs->cksum_errors;
    fh_unlock(fs);
}

void fhfs_tick(void) {
    uint64_t now = fh_now_ms();
    for (int i = 0; i < 4; i++) {
        fhfs_t *fs = g_mounted[i];
        if (!fs || fs->ro || fs->dead || !fs->gdirty) continue;
        uint64_t iv = fs->media == FH_MEDIA_FLASH ? 15000 : (fs->media == FH_MEDIA_HDD ? 5000 : 3000);
        if (now - fs->last_commit_ms < iv) continue;
        if (__atomic_exchange_n(&fs->lock, 1, __ATOMIC_ACQUIRE) == 0) { fh_commit(fs); fh_unlock(fs); }
    }
}

extern int  sched_create_kthread(void (*fn)(void *), void *arg, const char *name, int prio);
extern void sched_sleep_ns(uint64_t ns);
static void fhfs_syncd(void *a) { (void)a; for (;;) { sched_sleep_ns(1000000000ULL); fhfs_tick(); } }
int fhfs_start_syncd(void) { return sched_create_kthread(fhfs_syncd, NULL, "fhfs-sync", 0); }

/* ------------------------------------------------------------------ */
/* public API (all entry points take the fs lock)                      */
/* ------------------------------------------------------------------ */
#define LOCKED(ip, expr) do { fhfs_t *_f = (ip)->fs; fh_lock(_f); r = (expr); fh_unlock(_f); } while (0)

fhfs_inode_t *fhfs_root(fhfs_t *fs) { int e; fh_lock(fs); fhfs_inode_t *r = fh_iget(fs, FH_ROOT_INO, &e); fh_unlock(fs); return r; }
fhfs_inode_t *fhfs_iref(fhfs_inode_t *ip) { fh_lock(ip->fs); ip->ref++; fh_unlock(ip->fs); return ip; }
void fhfs_iput(fhfs_inode_t *ip) {
    fhfs_t *fs = ip->fs; fh_lock(fs);
    if (!fh_iput_k(ip)) fh_unlock(fs);
}
uint64_t fhfs_ino(fhfs_inode_t *ip) { return ip->ino; }
uint32_t fhfs_mode(fhfs_inode_t *ip) { return ip->d.mode; }
uint64_t fhfs_size(fhfs_inode_t *ip) { return ip->d.size; }

int fhfs_lookup(fhfs_inode_t *dir, const char *name, const fhfs_cred_t *cr, fhfs_inode_t **out) {
    int r; LOCKED(dir, fh_lookup_l(dir->fs, dir, name, cr, out)); return r;
}
int fhfs_create(fhfs_inode_t *dir, const char *name, uint32_t mode, uint32_t uid, uint32_t gid,
                uint64_t rdev, const fhfs_cred_t *cr, fhfs_inode_t **out) {
    int r; LOCKED(dir, fh_create_l(dir->fs, dir, name, mode, uid, gid, rdev, cr, out)); return r;
}
static int fh_symlink_l(fhfs_t *fs, fhfs_inode_t *dir, const char *name, const char *target,
                        uint32_t uid, uint32_t gid, const fhfs_cred_t *cr, fhfs_inode_t **out) {
    uint32_t tl = fh_slen(target);
    if (tl == 0 || tl > 4095) return FH_EINVAL;
    fhfs_inode_t *ip;
    int r = fh_create_l(fs, dir, name, FH_S_IFLNK | 0777, uid, gid, 0, cr, &ip);
    if (r) return r;
    int64_t w = fh_file_write(fs, ip, 0, tl, target);
    if (w != (int64_t)tl) {
        uint32_t nl; if (!fh_name_ok(name, &nl)) dir_remove(fs, dir, name, nl);
        ip->d.nlink = 0; fh_iput_l(ip);
        return w < 0 ? (int)w : FH_EIO;
    }
    if (out) *out = ip; else fh_iput_l(ip);
    return FH_OK;
}
int fhfs_symlink(fhfs_inode_t *dir, const char *name, const char *target, uint32_t uid, uint32_t gid,
                 const fhfs_cred_t *cr, fhfs_inode_t **out) {
    int r; LOCKED(dir, fh_symlink_l(dir->fs, dir, name, target, uid, gid, cr, out)); return r;
}
int fhfs_readlink(fhfs_inode_t *ip, char *buf, size_t sz) {
    int r; fhfs_t *fs = ip->fs; fh_lock(fs);
    if (!FH_S_ISLNK(ip->d.mode)) r = FH_EINVAL;
    else { int64_t n = fh_file_read(fs, ip, 0, sz < ip->d.size ? sz : ip->d.size, buf); r = n < 0 ? (int)n : (int)n; }
    fh_unlock(fs); return r;
}
int fhfs_link(fhfs_inode_t *dir, const char *name, fhfs_inode_t *t, const fhfs_cred_t *cr) {
    int r; LOCKED(dir, fh_link_l(dir->fs, dir, name, t, cr)); return r;
}
int fhfs_unlink(fhfs_inode_t *dir, const char *name, const fhfs_cred_t *cr) {
    int r; LOCKED(dir, fh_unlink_l(dir->fs, dir, name, cr, false)); return r;
}
int fhfs_rmdir(fhfs_inode_t *dir, const char *name, const fhfs_cred_t *cr) {
    int r; LOCKED(dir, fh_unlink_l(dir->fs, dir, name, cr, true)); return r;
}
int fhfs_rename(fhfs_inode_t *od, const char *on, fhfs_inode_t *nd, const char *nn, const fhfs_cred_t *cr) {
    int r; LOCKED(od, fh_rename_l(od->fs, od, on, nd, nn, cr)); return r;
}
int64_t fhfs_read(fhfs_inode_t *ip, uint64_t off, uint64_t len, void *buf) {
    int64_t r; fhfs_t *fs = ip->fs; fh_lock(fs);
    r = FH_S_ISDIR(ip->d.mode) ? FH_EISDIR : fh_file_read(fs, ip, off, len, buf);
    fh_unlock(fs); return r;
}
int64_t fhfs_write(fhfs_inode_t *ip, uint64_t off, uint64_t len, const void *buf) {
    int64_t r; fhfs_t *fs = ip->fs; fh_lock(fs);
    r = FH_S_ISDIR(ip->d.mode) ? FH_EISDIR : fh_file_write(fs, ip, off, len, buf);
    fh_unlock(fs); return r;
}
int fhfs_truncate(fhfs_inode_t *ip, uint64_t size) {
    int r; fhfs_t *fs = ip->fs; fh_lock(fs);
    r = FH_S_ISDIR(ip->d.mode) ? FH_EISDIR : fh_file_truncate(fs, ip, size);
    fh_unlock(fs); return r;
}
int fhfs_fsync(fhfs_inode_t *ip) { return fhfs_sync(ip->fs); }

int fhfs_readdir(fhfs_inode_t *dir, uint32_t index, char *name, size_t name_sz, uint64_t *ino, uint8_t *type) {
    int r; fhfs_t *fs = dir->fs; fh_lock(fs);
    r = FH_S_ISDIR(dir->d.mode) ? dir_nth(fs, dir, index, name, name_sz, ino, type) : FH_ENOTDIR;
    fh_unlock(fs); return r;
}

int fhfs_getattr(fhfs_inode_t *ip, fhfs_attr_t *o) {
    fh_lock(ip->fs);
    o->ino = ip->ino; o->mode = ip->d.mode; o->uid = ip->d.uid; o->gid = ip->d.gid; o->nlink = ip->d.nlink;
    o->size = ip->d.size; o->blocks = ip->d.blocks4k * 8; o->rdev = ip->d.rdev;
    o->atime_ns = ip->d.atime; o->mtime_ns = ip->d.mtime; o->ctime_ns = ip->d.ctime; o->btime_ns = ip->d.btime;
    o->flags = ip->d.flags; o->blksize = BLK;
    fh_unlock(ip->fs);
    return FH_OK;
}

int fhfs_permission(fhfs_inode_t *ip, const fhfs_cred_t *cr, uint32_t want) {
    int r; LOCKED(ip, fh_perm_l(ip->fs, ip, cr, want)); return r;
}

static bool fh_streq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return !*a && !*b; }

static int acl_sync_chmod(fhfs_t *fs, fhfs_inode_t *ip) {
    uint8_t v[ACL_MAX_VAL]; fh_acl_ent_t e[64];
    if (!ip->d.xattr_bp.blk) return FH_OK;
    int l = xa_get_int(fs, ip, ACL_ACCESS, v, sizeof(v));
    if (l <= 0) return FH_OK;
    int n = acl_parse(v, (uint32_t)l, e, 64);
    if (n < 3) return FH_OK;
    bool has_mask = false; for (int i = 0; i < n; i++) if (e[i].tag == ACL_MASK) has_mask = true;
    for (int i = 0; i < n; i++) {
        if (e[i].tag == ACL_USER_OBJ) e[i].perm = (ip->d.mode >> 6) & 7;
        else if (e[i].tag == ACL_MASK) e[i].perm = (ip->d.mode >> 3) & 7;
        else if (e[i].tag == ACL_GROUP_OBJ && !has_mask) e[i].perm = (ip->d.mode >> 3) & 7;
        else if (e[i].tag == ACL_OTHER) e[i].perm = ip->d.mode & 7;
    }
    uint32_t ol = acl_emit(e, (uint32_t)n, v);
    return xa_set_int(fs, ip, ACL_ACCESS, v, ol, 0);
}

static int fh_setattr_l(fhfs_t *fs, fhfs_inode_t *ip, const fhfs_setattr_t *sa, const fhfs_cred_t *cr) {
    if (fs->ro) return FH_EROFS;
    bool root = !cr || cr->uid == 0;
    uint64_t now = fhfs_time_ns();
    if ((sa->valid & FH_SET_MODE) && !root && cr->uid != ip->d.uid) return FH_EPERM;
    if ((sa->valid & FH_SET_UID) && !root && sa->uid != ip->d.uid) return FH_EPERM;
    if ((sa->valid & FH_SET_GID) && !root) {
        if (cr->uid != ip->d.uid) return FH_EPERM;
        if (sa->gid != ip->d.gid && !cred_in_group(cr, sa->gid)) return FH_EPERM;
    }
    if (sa->valid & (FH_SET_ATIME | FH_SET_MTIME)) {
        if (!root && cr->uid != ip->d.uid && fh_perm_l(fs, ip, cr, FH_MAY_WRITE)) return FH_EACCES;
    }
    if (sa->valid & FH_SET_SIZE) {
        if (FH_S_ISDIR(ip->d.mode)) return FH_EISDIR;
        if (cr && fh_perm_l(fs, ip, cr, FH_MAY_WRITE)) return FH_EACCES;
        int r = fh_file_truncate(fs, ip, sa->size); if (r) return r;
    }
    bool owner_changed = false;
    if (sa->valid & FH_SET_UID) { if (ip->d.uid != sa->uid) owner_changed = true; ip->d.uid = sa->uid; }
    if (sa->valid & FH_SET_GID) { if (ip->d.gid != sa->gid) owner_changed = true; ip->d.gid = sa->gid; }
    if (owner_changed && cr && cr->uid != 0 && FH_S_ISREG(ip->d.mode)) ip->d.mode &= ~(FH_S_ISUID | FH_S_ISGID);
    if (sa->valid & FH_SET_MODE) {
        uint32_t nm = (ip->d.mode & FH_S_IFMT) | (sa->mode & 07777u);
        if (!root && !cred_in_group(cr, ip->d.gid)) nm &= ~FH_S_ISGID;
        ip->d.mode = nm;
        int r = acl_sync_chmod(fs, ip); if (r) return r;
    }
    if (sa->valid & FH_SET_ATIME) ip->d.atime = sa->atime_ns;
    if (sa->valid & FH_SET_MTIME) ip->d.mtime = sa->mtime_ns;
    ip->d.ctime = now;
    fh_dirty(ip);
    return FH_OK;
}
int fhfs_setattr(fhfs_inode_t *ip, const fhfs_setattr_t *sa, const fhfs_cred_t *cr) {
    int r; LOCKED(ip, fh_setattr_l(ip->fs, ip, sa, cr)); return r;
}

/* ---- xattr API: POSIX ACLs keep the mode bits in sync ---- */
int fhfs_getxattr(fhfs_inode_t *ip, const char *name, void *buf, size_t sz) {
    int r; LOCKED(ip, xa_get_int(ip->fs, ip, name, buf, sz)); return r;
}
static int fh_setxattr_l(fhfs_t *fs, fhfs_inode_t *ip, const char *name, const void *val, size_t sz, int flags) {
    if (fs->ro) return FH_EROFS;
    bool acc = fh_streq(name, ACL_ACCESS), def = fh_streq(name, ACL_DEFAULT);
    if (def && !FH_S_ISDIR(ip->d.mode)) return FH_EACCES;
    if (acc || def) {
        if (sz == 0) { int r = xa_remove_int(fs, ip, name); return r == FH_ENODATA ? FH_OK : r; }
        fh_acl_ent_t e[64];
        int n = acl_parse((const uint8_t *)val, (uint32_t)sz, e, 64);
        if (n < 0) return n;
        if (!acl_valid(e, (uint32_t)n)) return FH_EINVAL;
        if (acc) {
            ip->d.mode = acl_to_mode(e, (uint32_t)n, ip->d.mode);
            if (n == 3) { int r = xa_remove_int(fs, ip, name); if (r && r != FH_ENODATA) return r; fh_dirty(ip); return FH_OK; }
        }
    }
    return xa_set_int(fs, ip, name, val, sz, flags);
}
int fhfs_setxattr(fhfs_inode_t *ip, const char *name, const void *val, size_t sz, int flags) {
    int r; LOCKED(ip, fh_setxattr_l(ip->fs, ip, name, val, sz, flags)); return r;
}
int fhfs_removexattr(fhfs_inode_t *ip, const char *name) {
    int r; LOCKED(ip, ip->fs->ro ? FH_EROFS : xa_remove_int(ip->fs, ip, name)); return r;
}
static int fh_listxattr_l(fhfs_t *fs, fhfs_inode_t *ip, char *out, size_t sz) {
    if (!ip->d.xattr_bp.blk) return 0;
    uint8_t *blk = (uint8_t *)kmalloc(BLK); if (!blk) return FH_ENOMEM;
    int r = xa_read(fs, ip, blk);
    uint32_t need = 0;
    if (!r) {
        uint32_t off = 0;
        while (off + 4 <= BLK) {
            uint32_t nl = (uint32_t)blk[off] | ((uint32_t)blk[off + 1] << 8);
            uint32_t vl = (uint32_t)blk[off + 2] | ((uint32_t)blk[off + 3] << 8);
            if (!nl) break;
            uint32_t tot = (4 + nl + vl + 3u) & ~3u;
            if (off + tot > BLK) { r = FH_ECORRUPT; break; }
            if (out && sz) { if (need + nl + 1 > sz) { r = FH_ERANGE; break; } fh_cpy(out + need, blk + off + 4, nl); out[need + nl] = 0; }
            need += nl + 1; off += tot;
        }
    }
    kfree(blk);
    return r ? r : (int)need;
}
int fhfs_listxattr(fhfs_inode_t *ip, char *buf, size_t sz) {
    int r; LOCKED(ip, fh_listxattr_l(ip->fs, ip, buf, sz)); return r;
}

int fhfs_errno(int e) {
    switch (e) {
        case FH_OK: return 0;           case FH_EIO: case FH_ECORRUPT: return 5;
        case FH_ENOENT: return 2;       case FH_ENOSPC: return 28;
        case FH_ENOMEM: return 12;      case FH_EROFS: return 30;
        case FH_EISDIR: return 21;      case FH_ENOTDIR: return 20;
        case FH_EEXIST: return 17;      case FH_ENOTEMPTY: return 39;
        case FH_EINVAL: return 22;      case FH_EACCES: return 13;
        case FH_ENAMETOOLONG: return 36; case FH_EPERM: return 1;
        case FH_EMLINK: return 31;      case FH_ERANGE: return 34;
        case FH_ENODATA: return 61;     case FH_E2BIG: return 7;
        case FH_EBUSY: return 16;       default: return 5;
    }
}

/* ------------------------------------------------------------------ */
/* snapshots + reflink                                                 */
/* ------------------------------------------------------------------ */
static int fh_snap_find(fhfs_t *fs, const char *name) {
    for (uint32_t i = 0; i < fs->nsnaps; i++) {
        const char *a = fs->snaps[i].name, *b = name;
        while (*a && *a == *b) { a++; b++; }
        if (!*a && !*b) return (int)i;
    }
    return -1;
}

static int fh_snap_create_l(fhfs_t *fs, const char *name) {
    if (fs->ro || fs->parent) return FH_EROFS;
    uint32_t n = fh_slen(name);
    if (n == 0 || n > 47) return FH_EINVAL;
    for (uint32_t i = 0; i < n; i++) if (name[i] == '/' || name[i] == ' ') return FH_EINVAL;
    if (fh_snap_find(fs, name) >= 0) return FH_EEXIST;
    if (fs->nsnaps >= 36) return FH_ENOSPC;
    int r = fh_commit(fs);                         /* freeze a fully consistent on-disk state */
    if (r) return r;
    fh_scan_t *sc = (fh_scan_t *)fh_zalloc(sizeof(*sc)); if (!sc) return FH_ENOMEM;
    r = scan_walk(fs, &fs->itree.root, fs->itree.depth, 0, true, sc);   /* every block gets one more holder */
    kfree(sc);
    if (r) return r;
    struct fh_snap *sn = &fs->snaps[fs->nsnaps];
    fh_set(sn, 0, sizeof(*sn));
    fh_cpy(sn->name, name, n);
    sn->id = ++fs->snap_next_id; sn->txg = fs->txg; sn->time_ns = fhfs_time_ns();
    sn->root = fs->itree.root; sn->depth = fs->itree.depth;
    fs->nsnaps++; fs->snap_dirty = true; fs->gdirty = true;
    return fh_commit(fs);
}

static int fh_snap_delete_l(fhfs_t *fs, const char *name) {
    if (fs->ro || fs->parent) return FH_EROFS;
    int i = fh_snap_find(fs, name);
    if (i < 0) return FH_ENOENT;
    if (fs->snap_mounts) return FH_EBUSY;
    fh_scan_t *sc = (fh_scan_t *)fh_zalloc(sizeof(*sc)); if (!sc) return FH_ENOMEM;
    sc->release = true;
    struct fh_snap sn = fs->snaps[i];
    int r = scan_walk(fs, &sn.root, sn.depth, 0, true, sc);      /* drop this snapshot's holder of each block */
    kfree(sc);
    if (r) return r;
    for (uint32_t k = (uint32_t)i; k + 1 < fs->nsnaps; k++) fs->snaps[k] = fs->snaps[k + 1];
    fs->nsnaps--; fs->snap_dirty = true; fs->gdirty = true;
    return fh_commit(fs);
}

int fhfs_snapshot_create(fhfs_t *fs, const char *name) { fh_lock(fs); int r = fh_snap_create_l(fs, name); fh_unlock(fs); return r; }
int fhfs_snapshot_delete(fhfs_t *fs, const char *name) { fh_lock(fs); int r = fh_snap_delete_l(fs, name); fh_unlock(fs); return r; }
int fhfs_snapshot_list(fhfs_t *fs, uint32_t idx, char *name, uint64_t *txg, uint64_t *time_ns) {
    int r = FH_OK; fh_lock(fs);
    if (idx >= fs->nsnaps) r = FH_ENOENT;
    else { fh_cpy(name, fs->snaps[idx].name, 48); if (txg) *txg = fs->snaps[idx].txg; if (time_ns) *time_ns = fs->snaps[idx].time_ns; }
    fh_unlock(fs); return r;
}

/* read-only view of a snapshot: an independent fhfs_t that shares only the device */
fhfs_t *fhfs_snapshot_mount(fhfs_t *fs, const char *name, int *err) {
    int dummy; if (!err) err = &dummy;
    fh_lock(fs);
    int i = fh_snap_find(fs, name);
    if (i < 0) { fh_unlock(fs); *err = FH_ENOENT; return NULL; }
    fhfs_t *c = (fhfs_t *)fh_zalloc(sizeof(*c));
    if (!c) { fh_unlock(fs); *err = FH_ENOMEM; return NULL; }
    c->dev = fs->dev; c->spb = fs->spb; c->lab = fs->lab; c->total = fs->total;
    c->data_start = fs->data_start; c->data_end = fs->data_end; c->dstart = fs->dstart;
    c->meta_lo = fs->meta_lo; c->meta_hi = fs->meta_hi; c->media = fs->media;
    c->rec_shift = fs->rec_shift; c->rec_size = fs->rec_size;
    c->ro = true; c->compress = false; c->txg = fs->snaps[i].txg; c->open_txg = c->txg + 1;
    c->itree.root = fs->snaps[i].root; c->itree.depth = fs->snaps[i].depth; c->itree.rnode = NULL;
    c->parent = fs; c->ino_hint = 2;
    c->iobuf = (uint8_t *)kmalloc(c->rec_size + 2 * BLK); c->cbuf = (uint8_t *)kmalloc(c->rec_size + 2 * BLK);
    c->tmpbuf = (uint8_t *)kmalloc(BLK); c->lz_ht = (uint32_t *)fh_zalloc((1u << LZ_HBITS) * 4);
    if (!c->iobuf || !c->cbuf || !c->tmpbuf || !c->lz_ht) { fh_teardown_buffers(c); kfree(c); fh_unlock(fs); *err = FH_ENOMEM; return NULL; }
    fs->snap_mounts++;
    fh_unlock(fs);
    int e; fhfs_inode_t *root = fh_iget(c, FH_ROOT_INO, &e);
    if (!root || !FH_S_ISDIR(root->d.mode)) {
        if (root) fh_iput_l(root);
        fh_lock(fs); fs->snap_mounts--; fh_unlock(fs);
        c->parent = NULL; c->dead = true; fh_umount_core(c);
        *err = root ? FH_ECORRUPT : e; return NULL;
    }
    fh_iput_l(root);
    *err = FH_OK;
    return c;
}

/* whole-file clone: dst shares every block of src until either side is modified */
static int fh_reflink_l(fhfs_t *fs, fhfs_inode_t *src, fhfs_inode_t *dst) {
    if (fs->ro) return FH_EROFS;
    if (src == dst) return FH_EINVAL;
    if (!FH_S_ISREG(src->d.mode) || !FH_S_ISREG(dst->d.mode)) return FH_EINVAL;
    int r = hot_flush_ip(fs, src); if (r) return r;
    r = tree_flush(fs, &src->tree); if (r) return r;
    src->d.data_root = src->tree.root; src->d.data_depth = (uint8_t)src->tree.depth;
    hot_drop(fs, dst, 0);                           /* forget whatever dst had */
    tree_destroy(fs, &dst->tree);
    dst->d.size = 0; dst->d.blocks4k = 0;
    if (src->tree.root.blk) {
        fh_scan_t *sc = (fh_scan_t *)fh_zalloc(sizeof(*sc)); if (!sc) return FH_ENOMEM;
        fh_bp_t root = src->tree.root;
        r = scan_walk(fs, &root, src->tree.depth, 0, false, sc);   /* one more holder of every block */
        kfree(sc);
        if (r) return r;
    }
    dst->tree.root = src->tree.root; dst->tree.depth = src->tree.depth; dst->tree.rnode = NULL;
    dst->d.rec_shift = src->d.rec_shift; dst->d.size = src->d.size; dst->d.blocks4k = src->d.blocks4k;
    uint64_t now = fhfs_time_ns(); dst->d.mtime = dst->d.ctime = now;
    dst->tdirty = true; fh_dirty(dst); fh_dirty(src);
    return FH_OK;
}
int fhfs_reflink(fhfs_inode_t *src, fhfs_inode_t *dst) {
    if (src->fs != dst->fs) return FH_EINVAL;
    int r; LOCKED(src, fh_reflink_l(src->fs, src, dst)); return r;
}

/* ------------------------------------------------------------------ */
/* VFS glue                                                            */
/* ------------------------------------------------------------------ */
static int fh_to_vfs(int e) {
    switch (e) {
        case FH_OK: return VFS_OK;
        case FH_ENOENT: return VFS_ERR_NOENT;   case FH_ENOSPC: return VFS_ERR_NOSPC;
        case FH_ENOMEM: return VFS_ERR_NOMEM;   case FH_EROFS: return VFS_ERR_ROFS;
        case FH_EISDIR: return VFS_ERR_ISDIR;   case FH_ENOTDIR: return VFS_ERR_NOTDIR;
        case FH_EEXIST: return VFS_ERR_EXIST;   case FH_ENOTEMPTY: return VFS_ERR_NOTEMPTY;
        case FH_EBUSY: return VFS_ERR_BUSY;     case FH_EINVAL: return VFS_ERR_INVAL;
        case FH_EACCES: return VFS_ERR_ACCES;   case FH_EPERM: return VFS_ERR_PERM;
        case FH_ENAMETOOLONG: return VFS_ERR_NAMETOOLONG;
        case FH_ERANGE: return VFS_ERR_RANGE;   case FH_ENODATA: return VFS_ERR_NODATA;
        case FH_EMLINK: return VFS_ERR_MLINK;   case FH_E2BIG: return VFS_ERR_2BIG;
        case FH_ECORRUPT: return VFS_ERR_CORRUPT;
        default: return VFS_ERR_IO;
    }
}

static vfs_node_ops_t g_fh_vops;

static void fh_vfill(vfs_node_t *n, fhfs_inode_t *ip) {
    n->stat.type = FH_S_ISDIR(ip->d.mode) ? VFS_TYPE_DIR : FH_S_ISLNK(ip->d.mode) ? VFS_TYPE_LINK : VFS_TYPE_FILE;
    n->stat.size = ip->d.size;
    n->stat.alloc_size = ip->d.blocks4k * BLK;
    n->stat.attr = ip->d.mode;
    n->stat.cluster = (uint32_t)ip->ino;
}
/* takes over the caller's inode reference */
static vfs_node_t *fh_vnode(fhfs_inode_t *ip) {
    vfs_node_t *n = (vfs_node_t *)fh_zalloc(sizeof(*n));
    if (!n) { fhfs_iput(ip); return NULL; }
    n->ops = &g_fh_vops; n->fs_priv = ip; n->refcount = 1;
    fh_vfill(n, ip);
    return n;
}

static int64_t fh_v_read(vfs_node_t *n, uint64_t off, uint64_t size, void *buf) {
    int64_t r = fhfs_read((fhfs_inode_t *)n->fs_priv, off, size, buf);
    return r < 0 ? fh_to_vfs((int)r) : r;
}
static int64_t fh_v_write(vfs_node_t *n, uint64_t off, uint64_t size, const void *buf) {
    fhfs_inode_t *ip = (fhfs_inode_t *)n->fs_priv;
    int64_t r = fhfs_write(ip, off, size, buf);
    if (r < 0) return fh_to_vfs((int)r);
    n->stat.size = fhfs_size(ip);
    return r;
}
static int fh_v_readdir(vfs_node_t *n, uint32_t index, vfs_dirent_t *out) {
    fhfs_inode_t *dir = (fhfs_inode_t *)n->fs_priv; fhfs_t *fs = dir->fs;
    char name[FHFS_NAME_MAX + 1]; uint64_t ino; uint8_t type;
    int r = fhfs_readdir(dir, index, name, sizeof(name), &ino, &type);
    if (r) return fh_to_vfs(r);
    uint32_t nl = fh_slen(name); if (nl > VFS_MAX_NAME) nl = VFS_MAX_NAME;
    fh_cpy(out->name, name, nl); out->name[nl] = 0;
    out->type = (type == 4) ? VFS_TYPE_DIR : (type == 10) ? VFS_TYPE_LINK : VFS_TYPE_FILE;
    out->size = 0; out->cluster = (uint32_t)ino;
    fh_lock(fs);
    int e; fhfs_inode_t *ip = fh_iget(fs, ino, &e);
    if (ip) { out->size = ip->d.size; fh_iput_l(ip); }
    fh_unlock(fs);
    return VFS_OK;
}
static vfs_node_t *fh_v_lookup(vfs_node_t *parent, const char *name) {
    fhfs_inode_t *ip;
    if (fhfs_lookup((fhfs_inode_t *)parent->fs_priv, name, NULL, &ip)) return NULL;
    return fh_vnode(ip);
}
static vfs_node_t *fh_v_create(vfs_node_t *parent, const char *name, uint8_t type) {
    fhfs_inode_t *ip;
    uint32_t mode = (type == VFS_TYPE_DIR) ? (FH_S_IFDIR | 0755) : (FH_S_IFREG | 0644);
    if (fhfs_create((fhfs_inode_t *)parent->fs_priv, name, mode, 0, 0, 0, NULL, &ip)) return NULL;
    return fh_vnode(ip);
}
static int fh_v_unlink(vfs_node_t *parent, const char *name) {
    fhfs_inode_t *dir = (fhfs_inode_t *)parent->fs_priv, *t;
    int r = fhfs_lookup(dir, name, NULL, &t);
    if (r) return fh_to_vfs(r);
    bool isdir = FH_S_ISDIR(fhfs_mode(t));
    fhfs_iput(t);
    r = isdir ? fhfs_rmdir(dir, name, NULL) : fhfs_unlink(dir, name, NULL);
    return fh_to_vfs(r);
}
static int fh_v_sync(vfs_node_t *n) { return fh_to_vfs(fhfs_fsync((fhfs_inode_t *)n->fs_priv)); }
static int fh_v_truncate(vfs_node_t *n, uint64_t size) {
    fhfs_inode_t *ip = (fhfs_inode_t *)n->fs_priv;
    int r = fhfs_truncate(ip, size);
    if (!r) n->stat.size = fhfs_size(ip);
    return fh_to_vfs(r);
}
static void fh_v_release(vfs_node_t *n) {
    if (!n) return;
    if (n->refcount > 1) { n->refcount--; return; }
    n->refcount = 0;
    if (n->fs_priv) fhfs_iput((fhfs_inode_t *)n->fs_priv);
    kfree(n);
}


/* ---- POSIX extensions (credentials, ownership, links, xattr, clones) ---- */
static fhfs_cred_t *fh_cred(const vfs_cred_t *c, fhfs_cred_t *o) {
    if (!c) return NULL;
    o->uid = c->euid; o->gid = c->egid;
    o->ngroups = c->ngroups > FHFS_MAX_GROUPS ? FHFS_MAX_GROUPS : c->ngroups;
    for (uint32_t i = 0; i < o->ngroups; i++) o->groups[i] = c->groups[i];
    return o;
}
static fhfs_inode_t *fh_ip(vfs_node_t *n) { return (fhfs_inode_t *)n->fs_priv; }
static void fh_vrefresh(vfs_node_t *n) {
    fhfs_attr_t a; if (fhfs_getattr(fh_ip(n), &a) == 0) { n->stat.size = a.size; n->stat.alloc_size = a.blocks * 512; n->stat.attr = a.mode; }
}

static int fh_x_getattr(vfs_node_t *n, vfs_attr_t *o) {
    fhfs_attr_t a; int r = fhfs_getattr(fh_ip(n), &a); if (r) return fh_to_vfs(r);
    o->ino = a.ino; o->size = a.size; o->blocks = a.blocks; o->rdev = a.rdev;
    o->atime_ns = a.atime_ns; o->mtime_ns = a.mtime_ns; o->ctime_ns = a.ctime_ns; o->btime_ns = a.btime_ns;
    o->mode = a.mode; o->uid = a.uid; o->gid = a.gid; o->nlink = a.nlink; o->blksize = a.blksize; o->dev = 2;
    return VFS_OK;
}
static int fh_x_setattr(vfs_node_t *n, const vfs_setattr_t *sa, const vfs_cred_t *cr) {
    fhfs_cred_t fc; fhfs_setattr_t s;
    s.valid = sa->valid; s.mode = sa->mode; s.uid = sa->uid; s.gid = sa->gid; s.size = sa->size;
    s.atime_ns = sa->atime_ns; s.mtime_ns = sa->mtime_ns;
    int r = fhfs_setattr(fh_ip(n), &s, fh_cred(cr, &fc));
    if (!r) fh_vrefresh(n);
    return fh_to_vfs(r);
}
static int fh_x_permission(vfs_node_t *n, const vfs_cred_t *cr, uint32_t want) {
    fhfs_cred_t fc; return fh_to_vfs(fhfs_permission(fh_ip(n), fh_cred(cr, &fc), want));
}
static int fh_x_create2(vfs_node_t *p, const char *name, uint32_t mode, uint64_t rdev, const vfs_cred_t *cr, vfs_node_t **out) {
    fhfs_cred_t fc; fhfs_inode_t *ip;
    int r = fhfs_create(fh_ip(p), name, mode, cr ? cr->euid : 0, cr ? cr->egid : 0, rdev, fh_cred(cr, &fc), &ip);
    if (r) return fh_to_vfs(r);
    vfs_node_t *v = fh_vnode(ip); if (!v) return VFS_ERR_NOMEM;
    *out = v; return VFS_OK;
}
static int fh_x_symlink(vfs_node_t *p, const char *name, const char *target, const vfs_cred_t *cr, vfs_node_t **out) {
    fhfs_cred_t fc; fhfs_inode_t *ip;
    int r = fhfs_symlink(fh_ip(p), name, target, cr ? cr->euid : 0, cr ? cr->egid : 0, fh_cred(cr, &fc), &ip);
    if (r) return fh_to_vfs(r);
    vfs_node_t *v = fh_vnode(ip); if (!v) return VFS_ERR_NOMEM;
    *out = v; return VFS_OK;
}
static int fh_x_readlink(vfs_node_t *n, char *buf, size_t sz) { int r = fhfs_readlink(fh_ip(n), buf, sz); return r < 0 ? fh_to_vfs(r) : r; }
static int fh_x_link(vfs_node_t *p, const char *name, vfs_node_t *t, const vfs_cred_t *cr) {
    fhfs_cred_t fc; return fh_to_vfs(fhfs_link(fh_ip(p), name, fh_ip(t), fh_cred(cr, &fc)));
}
static int fh_x_unlink2(vfs_node_t *p, const char *name, const vfs_cred_t *cr, int rmdir) {
    fhfs_cred_t fc; fhfs_cred_t *c = fh_cred(cr, &fc);
    return fh_to_vfs(rmdir ? fhfs_rmdir(fh_ip(p), name, c) : fhfs_unlink(fh_ip(p), name, c));
}
static int fh_x_rename(vfs_node_t *od, const char *on, vfs_node_t *nd, const char *nn, const vfs_cred_t *cr) {
    fhfs_cred_t fc; return fh_to_vfs(fhfs_rename(fh_ip(od), on, fh_ip(nd), nn, fh_cred(cr, &fc)));
}
static int fh_x_getxattr(vfs_node_t *n, const char *nm, void *b, size_t sz) { int r = fhfs_getxattr(fh_ip(n), nm, b, sz); return r < 0 ? fh_to_vfs(r) : r; }
static int fh_x_setxattr(vfs_node_t *n, const char *nm, const void *v, size_t sz, int fl) { int r = fhfs_setxattr(fh_ip(n), nm, v, sz, fl); if (!r) fh_vrefresh(n); return fh_to_vfs(r); }
static int fh_x_listxattr(vfs_node_t *n, char *b, size_t sz) { int r = fhfs_listxattr(fh_ip(n), b, sz); return r < 0 ? fh_to_vfs(r) : r; }
static int fh_x_removexattr(vfs_node_t *n, const char *nm) { return fh_to_vfs(fhfs_removexattr(fh_ip(n), nm)); }
static int fh_x_reflink(vfs_node_t *s, vfs_node_t *d) {
    if (s->ops != d->ops) return VFS_ERR_XDEV;
    int r = fhfs_reflink(fh_ip(s), fh_ip(d)); if (!r) fh_vrefresh(d);
    return fh_to_vfs(r);
}
static const vfs_node_xops_t g_fh_xops = {
    .getattr = fh_x_getattr, .setattr = fh_x_setattr, .permission = fh_x_permission, .create2 = fh_x_create2,
    .symlink = fh_x_symlink, .readlink = fh_x_readlink, .link = fh_x_link, .unlink2 = fh_x_unlink2,
    .rename = fh_x_rename, .getxattr = fh_x_getxattr, .setxattr = fh_x_setxattr, .listxattr = fh_x_listxattr,
    .removexattr = fh_x_removexattr, .reflink = fh_x_reflink,
};

static vfs_node_ops_t g_fh_vops = {
    .x = &g_fh_xops,
    .read = fh_v_read, .write = fh_v_write, .readdir = fh_v_readdir, .lookup = fh_v_lookup,
    .create = fh_v_create, .unlink = fh_v_unlink, .sync = fh_v_sync, .release = fh_v_release,
    .truncate = fh_v_truncate,
};

static vfs_node_t *fh_v_mount(blkdev_t *dev, uint32_t flags) {
    int err; fhfs_t *fs = fhfs_mount_dev(dev, flags, &err);
    if (!fs) { DBG_VAL("FHFS", "mount failed, err", (uint64_t)(int64_t)err); return NULL; }
    fhfs_inode_t *root = fhfs_root(fs);
    if (!root) { fhfs_umount_fs(fs); return NULL; }
    DBG_VAL("FHFS", "mounted, txg", fs->txg);
    DBG_VAL("FHFS", "media (1=HDD 2=SSD 3=FLASH)", fs->media);
    return fh_vnode(root);
}
static void fh_v_umount(vfs_mount_t *mnt) {
    if (!mnt || !mnt->root) return;
    fhfs_inode_t *root = (fhfs_inode_t *)mnt->root->fs_priv;
    if (root) fhfs_umount_fs(root->fs);
}

static fs_ops_t g_fhfs_ops = { .mount = fh_v_mount, .umount = fh_v_umount, .name = "fhfs" };
int fhfs_register(void) { return vfs_register_fs(&g_fhfs_ops); }

int fhfs_node_statfs(vfs_node_t *n, fhfs_stat_t *o) {
    if (!n || n->ops != &g_fh_vops) return -1;
    fhfs_statfs(fh_ip(n)->fs, o);
    return 0;
}

/* ---- snapshot administration (root only) ----
   cmd 1 create(name)  2 delete(name)  3 list(idx -> out48)  4 mount(name at `at`, read-only)  */
int fhfs_vfs_snapshot(int cmd, const char *path, const char *name, const char *at, uint32_t idx, char *out48) {
    if (vfs_cred()->euid != 0) return VFS_ERR_ACCES;
    int err; vfs_node_t *n = vfs_lookup_node(path, 1, &err);
    if (!n) return err;
    if (n->ops != &g_fh_vops) { vfs_node_put(n); return VFS_ERR_UNSUP; }
    fhfs_t *fs = fh_ip(n)->fs;
    int r;
    switch (cmd) {
    case 1: r = fh_to_vfs(fhfs_snapshot_create(fs, name)); break;
    case 2: r = fh_to_vfs(fhfs_snapshot_delete(fs, name)); break;
    case 3: r = fh_to_vfs(fhfs_snapshot_list(fs, idx, out48, NULL, NULL)); break;
    case 4: {
        int e; fhfs_t *c = fhfs_snapshot_mount(fs, name, &e);
        if (!c) { r = fh_to_vfs(e); break; }
        fhfs_inode_t *root = fhfs_root(c);
        vfs_node_t *v = root ? fh_vnode(root) : NULL;
        if (!v) { fhfs_umount_fs(c); r = VFS_ERR_NOMEM; break; }
        r = vfs_attach(at, v, &g_fhfs_ops, NULL, MS_RDONLY);
        if (r) { fh_v_release(v); fhfs_umount_fs(c); }
        break;
    }
    default: r = VFS_ERR_INVAL;
    }
    vfs_node_put(n);
    return r;
}
