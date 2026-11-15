/* Fexox OS Project, EULA GPLv2 */





















#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "VFS.h"
#include "FexoxHybridFS.h"
#include "Scheduler.h"
#include "debug_out.h"
#include "USB.h"
#include "Storage.h"

extern void usb_hotplug_poll(void);
extern void *kmalloc(uint64_t size);
extern void  kfree(void *ptr);

#ifndef FEXOX_STORAGE_HOTPLUG_POLL
#define FEXOX_STORAGE_HOTPLUG_POLL 1     
#endif

static void (*g_log)(const char *) = 0;
static void slog(const char *s) { dbg_puts(s); if (g_log) g_log(s); }
static void slog2(const char *a, const char *b) { slog(a); slog(b); }

 
static size_t s_len(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static int s_eq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void s_cpy(char *d, const char *s, size_t max) {
    size_t i = 0; while (i + 1 < max && s[i]) { d[i] = s[i]; i++; } d[i] = 0;
}
static void s_cat(char *d, const char *s, size_t max) {
    size_t n = s_len(d); s_cpy(d + n, s, max > n ? max - n : 0);
}

 
typedef struct { blkdev_t *base; uint64_t first_lba; } part_priv_t;

static int part_read(blkdev_t *dev, uint64_t lba, uint32_t count, void *buf) {
    part_priv_t *p = (part_priv_t *)dev->priv;
    if (lba + count > dev->sector_count) return VFS_ERR_OVERFLOW;
    return p->base->ops->read_sectors(p->base, p->first_lba + lba, count, buf);
}
static int part_write(blkdev_t *dev, uint64_t lba, uint32_t count, const void *buf) {
    part_priv_t *p = (part_priv_t *)dev->priv;
    if (lba + count > dev->sector_count) return VFS_ERR_OVERFLOW;
    return p->base->ops->write_sectors(p->base, p->first_lba + lba, count, buf);
}
static void part_destroy(blkdev_t *dev) { (void)dev; }
static int part_flush(blkdev_t *dev) {
    part_priv_t *p = (part_priv_t *)dev->priv;
    return blkdev_flush(p->base);
}
static blkdev_ops_t g_part_ops = { part_read, part_write, part_destroy, part_flush };

#define ST_MAX_PARTS 16
static blkdev_t    g_pd[ST_MAX_PARTS];
static part_priv_t g_pp[ST_MAX_PARTS];
static int         g_pd_used = 0;

static bool is_part(const blkdev_t *d) { return d >= &g_pd[0] && d < &g_pd[ST_MAX_PARTS]; }

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32); }

static const uint8_t FEXOX_GPT_GUID[16] =
    { 0x46,0x45,0x58,0x4F,0x53,0x46,0x48,0x46, 0x53,0x50,0x41,0x52,0x54,0x00,0x00,0x01 };

typedef struct { uint64_t start, size; int dedicated; } pent_t;



static int fat_bpb(const uint8_t *s) {
    if (s[510] != 0x55 || s[511] != 0xAA) return 0;
    if (s[0] != 0xEB && s[0] != 0xE9) return 0;
    uint16_t bps = (uint16_t)(s[11] | (s[12] << 8));
    if (bps != 512 && bps != 1024 && bps != 2048 && bps != 4096) return 0;
    uint8_t spc = s[13];
    if (spc == 0 || (spc & (spc - 1))) return 0;
    if (s[14] == 0 && s[15] == 0) return 0;                  
    if (s[16] != 1 && s[16] != 2) return 0;                  
    if (s[17] != 0 || s[18] != 0) return 0;                  
    if (s[22] != 0 || s[23] != 0) return 0;                  
    if (!(s[82]=='F' && s[83]=='A' && s[84]=='T' && s[85]=='3' && s[86]=='2')) return 0;
    return 1;
}

 
static int read_parts(blkdev_t *bd, uint8_t *sec, pent_t *out, int max) {
    uint32_t ss = bd->sector_size;
    int n = 0, is_gpt = 0;
    for (int i = 0; i < 4; i++) if (sec[0x1BE + 16 * i + 4] == 0xEE) is_gpt = 1;

    if (!is_gpt) {
        for (int i = 0; i < 4 && n < max; i++) {
            const uint8_t *e = sec + 0x1BE + 16 * i;
            uint8_t type = e[4];
            uint32_t lba = rd32(e + 8), cnt = rd32(e + 12);
            if (type == 0 || lba == 0 || cnt == 0) continue;
            if (type == 0x05 || type == 0x0F || type == 0x85) continue;       
            out[n].start = lba; out[n].size = cnt; out[n].dedicated = (type == 0xDA); n++;
        }
        return n;
    }
    if (blkdev_read(bd, 1, 1, sec) != VFS_OK) return 0;
    if (!(sec[0]=='E'&&sec[1]=='F'&&sec[2]=='I'&&sec[3]==' '&&sec[4]=='P'&&sec[5]=='A'&&sec[6]=='R'&&sec[7]=='T'))
        return 0;
    uint64_t ent_lba = rd64(sec + 72);
    uint32_t nent = rd32(sec + 80), esz = rd32(sec + 84);
    if (nent > 128) nent = 128;
    if (esz < 128 || esz > 512 || ent_lba == 0) return 0;
    uint64_t cur = (uint64_t)-1;
    for (uint32_t i = 0; i < nent && n < max; i++) {
        uint64_t off = (uint64_t)i * esz, s_lba = ent_lba + off / ss;
        uint32_t in_s = (uint32_t)(off % ss);
        if (in_s + esz > ss) continue;
        if (s_lba != cur) { if (blkdev_read(bd, s_lba, 1, sec) != VFS_OK) break; cur = s_lba; }
        const uint8_t *e = sec + in_s;
        int zero = 1, ded = 1;
        for (int k = 0; k < 16; k++) { if (e[k]) zero = 0; if (e[k] != FEXOX_GPT_GUID[k]) ded = 0; }
        if (zero) continue;
        uint64_t first = rd64(e + 32), last = rd64(e + 40);
        if (first == 0 || last < first) continue;
        out[n].start = first; out[n].size = last - first + 1; out[n].dedicated = ded; n++;
    }
    return n;
}

 
enum { K_FHFS = 1, K_BLANK = 2, K_FAT = 3 };
typedef struct { blkdev_t *dev; int kind; int done; } cand_t;
#define ST_MAX_CAND 32
static cand_t g_c[ST_MAX_CAND];
static int    g_nc = 0;

#define ST_MAX_SCAN 48
static blkdev_t *g_scanned[ST_MAX_SCAN];
static uint8_t   g_tries[ST_MAX_SCAN];
static int       g_nscanned = 0;

static int g_root_kind = 0;         
static int g_boot_mounted = 0;      
static int g_seeded = 0;
static bool g_first_fat_seen = false;
static int  g_fat_wait = 0;

static void add_cand(blkdev_t *d, int kind) {
    if (g_nc >= ST_MAX_CAND) return;
    g_c[g_nc].dev = d; g_c[g_nc].kind = kind; g_c[g_nc].done = 0; g_nc++;
    slog2(kind == K_FHFS ? "  found FHFS volume: " : kind == K_BLANK ? "  found blank Fexox partition: "
                                                                   : "  found FAT volume: ", d->name);
    slog("\n");
}

static void make_part_dev(blkdev_t *pd, part_priv_t *pp, blkdev_t *base, const pent_t *e, const char *name) {
    pp->base = base; pp->first_lba = e->start;
    s_cpy(pd->name, name, sizeof(pd->name));
    pd->sector_size = base->sector_size; pd->sector_count = e->size;
    pd->priv = pp; pd->ops = &g_part_ops; pd->readonly = base->readonly;
}

static bool first_sectors_blank(blkdev_t *d) {
    uint32_t cnt = 8; if (d->sector_count < cnt) cnt = (uint32_t)d->sector_count;
    uint8_t *b = (uint8_t *)kmalloc((uint64_t)cnt * d->sector_size);
    if (!b) return false;
    bool ok = blkdev_read(d, 0, cnt, b) == VFS_OK;
    for (uint64_t i = 0; ok && i < (uint64_t)cnt * d->sector_size; i++) if (b[i]) ok = false;
    kfree(b);
    return ok;
}

static void dec_str(char *o, unsigned v) {
    char t[12]; int n = 0; if (!v) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + v % 10); v /= 10; }
    int k = 0; while (n) o[k++] = t[--n]; o[k] = 0;
}

 
static int scan_disk(blkdev_t *bd) {
    uint32_t ss = bd->sector_size ? bd->sector_size : 512u;
    if (ss > 4096u || bd->sector_count == 0) return 1;
    uint8_t *sec = (uint8_t *)kmalloc(4096);
    if (!sec) return 0;
    if (blkdev_read(bd, 0, 1, sec) != VFS_OK) { kfree(sec); return 0; }

    if (fhfs_probe(bd)) { add_cand(bd, K_FHFS); kfree(sec); return 1; }
    if (fat_bpb(sec))   { add_cand(bd, K_FAT);  kfree(sec); return 1; }
    if (sec[510] != 0x55 || sec[511] != 0xAA) { kfree(sec); return 1; }

    pent_t pe[ST_MAX_PARTS];
    int np = read_parts(bd, sec, pe, ST_MAX_PARTS);
    kfree(sec);
    for (int i = 0; i < np; i++) {
        if (pe[i].start + pe[i].size > bd->sector_count) continue;
        if (g_pd_used >= ST_MAX_PARTS) break;
         
        blkdev_t tmp; part_priv_t tp;
        make_part_dev(&tmp, &tp, bd, &pe[i], "tmp");
        int kind = 0;
        if (fhfs_probe(&tmp)) kind = K_FHFS;
        else {
            uint8_t *s0 = (uint8_t *)kmalloc(4096);
            if (s0) {
                if (blkdev_read(&tmp, 0, 1, s0) == VFS_OK && fat_bpb(s0)) kind = K_FAT;
                kfree(s0);
            }
            if (!kind && pe[i].dedicated) {
                if (first_sectors_blank(&tmp)) kind = K_BLANK;
                else slog2("  dedicated partition is not blank and has no FHFS label, left alone: ", bd->name);
            }
        }
        if (!kind) continue;
        char nm[40], num[12];
        s_cpy(nm, bd->name, sizeof(nm)); s_cat(nm, "p", sizeof(nm)); dec_str(num, (unsigned)(i + 1)); s_cat(nm, num, sizeof(nm));
        blkdev_t *pd = &g_pd[g_pd_used]; part_priv_t *pp = &g_pp[g_pd_used];
        g_pd_used++;
        make_part_dev(pd, pp, bd, &pe[i], nm);
        if (blkdev_register(pd) < 0) { slog("  blkdev_register failed for partition\n"); g_pd_used--; break; }
        add_cand(pd, kind);
    }
    return 1;
}

static void scan_new_disks(void) {
    int n = blkdev_count();
    for (int i = 0; i < n; i++) {
        blkdev_t *d = blkdev_at(i);
        if (!d || is_part(d)) continue;
        int known = -1;
        for (int k = 0; k < g_nscanned; k++) if (g_scanned[k] == d) { known = k; break; }
        if (known >= 0 && g_tries[known] == 0) continue;           
        if (known < 0) {
            if (g_nscanned >= ST_MAX_SCAN) continue;
            known = g_nscanned++; g_scanned[known] = d; g_tries[known] = 3;
            slog2("[STORAGE] scanning ", d->name); slog("\n");
        }
        if (scan_disk(d)) g_tries[known] = 0;
        else if (--g_tries[known] == 0) { slog2("  cannot read ", d->name); slog("\n"); g_tries[known] = 0; }
    }
}

 
static bool path_exists(const char *p) { vfs_stat_t st; return vfs_stat(p, &st) == VFS_OK; }

static void ensure_dir(const char *p, uint32_t mode) {
    if (!path_exists(p)) vfs_mkdir2(p, mode);
}

static bool has_marker(const char *prefix, const char *const *marks) {
    char p[96];
    for (int i = 0; marks[i]; i++) {
        s_cpy(p, prefix, sizeof(p)); s_cat(p, marks[i], sizeof(p));
        if (path_exists(p)) return true;
    }
    return false;
}
static const char *const MARK_SEED[]   = { "/kernel.bin", "/FEXOX.SEED", NULL };
static const char *const MARK_LEGACY[] = { "/kernel.bin", "/EFI", "/UI", "/APPS", "/FEXOX.SEED", NULL };

 
static void make_system_dirs(void) {
    ensure_dir("/boot", 0755); ensure_dir("/mnt", 0755); ensure_dir("/UI", 0755);
    ensure_dir("/APPS", 0755); ensure_dir("/tmp", 01777); ensure_dir("/etc", 0755);
}

static int mount_fhfs_root(blkdev_t *d) {
    int r = vfs_mount(d->name, "fhfs", "/", 0);
    if (r != VFS_OK) { slog2("  FHFS mount failed: ", d->name); slog("\n"); return 0; }
    g_root_kind = 1;
    slog2("[OK] / mounted (FHFS, dev=", d->name); slog(")\n");
    make_system_dirs();
    return 1;
}

static int try_fhfs_root(void) {
    for (int i = 0; i < g_nc; i++) {
        if (g_c[i].kind != K_FHFS || g_c[i].done) continue;
        g_c[i].done = 1;
        if (mount_fhfs_root(g_c[i].dev)) return 1;
    }
    for (int i = 0; i < g_nc; i++) {
        if (g_c[i].kind != K_BLANK || g_c[i].done) continue;
        g_c[i].done = 1;
        blkdev_t *d = g_c[i].dev;
        if (d->readonly) continue;
        slog2("[STORAGE] first boot: formatting FHFS on ", d->name); slog("\n");
        int fr = fhfs_format(d, "Fexox OS", 0);
        if (fr != FH_OK) { slog("  fhfs_format failed\n"); continue; }
        if (mount_fhfs_root(d)) return 1;
    }
    return 0;
}

static int try_legacy_root(void) {
    for (int i = 0; i < g_nc; i++) {
        if (g_c[i].kind != K_FAT || g_c[i].done) continue;
        blkdev_t *d = g_c[i].dev;
        if (vfs_mount(d->name, "fat32", "/", 0) != VFS_OK) { g_c[i].done = 1; continue; }
        if (!has_marker("", MARK_LEGACY)) { vfs_umount("/"); g_c[i].done = 1; continue; }
        g_c[i].done = 1; g_root_kind = 2;
        slog2("[WARN] no FHFS volume - legacy FAT32 root, dev=", d->name); slog("\n");
        return 1;
    }
    return 0;
}

 
static bool same_content(const char *a, const char *b) {
    bool same = true;
    uint8_t *x = (uint8_t *)kmalloc(16384), *y = (uint8_t *)kmalloc(16384);
    int fa = vfs_open(a, O_RDONLY), fb = vfs_open(b, O_RDONLY);
    if (!x || !y || fa < 0 || fb < 0) same = false;
    while (same) {
        int64_t ra = vfs_read(fa, x, 16384), rb = vfs_read(fb, y, 16384);
        if (ra != rb) { same = false; break; }
        if (ra <= 0) break;
        for (int64_t i = 0; i < ra; i++) if (x[i] != y[i]) { same = false; break; }
    }
    if (fa >= 0) vfs_close(fa);
    if (fb >= 0) vfs_close(fb);
    if (x) kfree(x);
    if (y) kfree(y);
    return same;
}

static bool copy_file(const char *src, const char *dst, uint32_t mode, bool overwrite) {
    vfs_stat_t ss, ds;
    if (vfs_stat(src, &ss) != VFS_OK || ss.type != VFS_TYPE_FILE) return true;
    bool have = vfs_stat(dst, &ds) == VFS_OK;
    if (have && !overwrite) return true;
    if (have && ds.size == ss.size && same_content(src, dst)) return true;

    uint8_t *buf = (uint8_t *)kmalloc(32768);
    if (!buf) return false;
    int sfd = vfs_open(src, O_RDONLY);
    int dfd = sfd >= 0 ? vfs_open2(dst, O_WRONLY | O_CREAT | O_TRUNC, mode) : -1;
    bool ok = sfd >= 0 && dfd >= 0;
    while (ok) {
        int64_t r = vfs_read(sfd, buf, 32768);
        if (r < 0) { ok = false; break; }
        if (r == 0) break;
        int64_t w = vfs_write(dfd, buf, (uint64_t)r);
        if (w != r) { ok = false; break; }
    }
    if (sfd >= 0) vfs_close(sfd);
    if (dfd >= 0) vfs_close(dfd);
    kfree(buf);
    if (ok) { slog2("  seeded ", dst); slog("\n"); }
    else    { slog2("  seed FAILED ", dst); slog("\n"); }
    return ok;
}



static void fold_name(const char *in, char *out, size_t max) {
    bool has_lower = false, has_upper = false;
    for (const char *p = in; *p; p++) { if (*p >= 'a' && *p <= 'z') has_lower = true; if (*p >= 'A' && *p <= 'Z') has_upper = true; }
    size_t i = 0;
    for (; in[i] && i + 1 < max; i++) {
        char c = in[i];
        out[i] = (has_upper && !has_lower && c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    out[i] = 0;
}

static void seed_dir(const char *src, const char *dst, bool overwrite, int depth) {
    if (depth > 3) return;
    int dfd = vfs_open(src, O_DIRECTORY);
    if (dfd < 0) return;
    ensure_dir(dst, 0755);
    vfs_dirent_t *de = (vfs_dirent_t *)kmalloc(sizeof(*de));
    if (!de) { vfs_close(dfd); return; }
    for (uint32_t i = 0; vfs_readdir(dfd, i, de) == VFS_OK; i++) {
        if (s_eq(de->name, ".") || s_eq(de->name, "..")) continue;
        char nm[96], sp[256], dp[256];
        if (s_len(de->name) > 90 || s_len(src) + s_len(dst) > 150) continue;
        fold_name(de->name, nm, sizeof(nm));
        s_cpy(sp, src, sizeof(sp)); s_cat(sp, "/", sizeof(sp)); s_cat(sp, de->name, sizeof(sp));
        s_cpy(dp, dst, sizeof(dp)); s_cat(dp, "/", sizeof(dp)); s_cat(dp, nm, sizeof(dp));
        if (de->type == VFS_TYPE_DIR) { seed_dir(sp, dp, overwrite, depth + 1); continue; }
        size_t l = s_len(nm);
        bool exe = overwrite || (l > 4 && s_eq(nm + l - 4, ".elf"));
        if (!copy_file(sp, dp, exe ? 0755 : 0644, overwrite)) break;      
    }
    kfree(de);
    vfs_close(dfd);
}

static void seed_from_boot(void) {
    if (g_seeded || g_root_kind != 1 || !g_boot_mounted) return;
    g_seeded = 1;
    slog("[STORAGE] seeding FHFS from /boot ...\n");
    seed_dir("/boot/UI",   "/UI",   false, 0);
    seed_dir("/boot/APPS", "/APPS", true,  0);
    copy_file("/boot/init", "/init", 0755, true);
    vfs_sync_all();
    slog("[STORAGE] seeding done\n");
}

 
static void mount_extra_volumes(void) {
    for (int i = 0; i < g_nc; i++) {
        cand_t *c = &g_c[i];
        if (c->done || c->kind == K_BLANK) continue;
        blkdev_t *d = c->dev;
        c->done = 1;
        const char *fs = c->kind == K_FAT ? "fat32" : "fhfs";

        if (c->kind == K_FAT && g_root_kind == 1 && !g_boot_mounted) {
            if (vfs_mount(d->name, fs, "/boot", 0) == VFS_OK) {
                if (has_marker("/boot", MARK_SEED)) {
                    g_boot_mounted = 1;
                    slog2("[OK] /boot mounted (FAT32, dev=", d->name); slog(")\n");
                    continue;
                }
                vfs_umount("/boot");
            }
        }
        char mp[64] = "/mnt/";
        s_cat(mp, d->name, sizeof(mp));
        if (g_root_kind == 1) ensure_dir(mp, 0755);
        if (vfs_mount(d->name, fs, mp, 0) == VFS_OK) {
            slog2("[OK] mounted ", mp); slog2(c->kind == K_FAT ? " (FAT32)\n" : " (FHFS)\n", "");
        } else {
            slog2("  could not mount ", d->name); slog("\n");
        }
    }
    seed_from_boot();
}


 


#ifndef FEXOX_STORAGE_BENCH
#define FEXOX_STORAGE_BENCH 1
#endif
#if FEXOX_STORAGE_BENCH
static uint64_t b_tsc(void) { uint32_t lo, hi; __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi)); return ((uint64_t)hi << 32) | lo; }
static void dec64s(char *o, uint64_t v) {
    char t[24]; int n = 0; if (!v) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + v % 10); v /= 10; }
    int k = 0; while (n) o[k++] = t[--n]; o[k] = 0;
}
static void b_report(const char *what, const char *path, uint64_t bytes, uint64_t tsc) {
    uint64_t per = usb_tsc_per_ms(); if (!per) per = 3000000ULL;
    uint64_t us = tsc * 1000 / per, ms = us / 1000;
    uint64_t mibs10 = us ? (bytes * 10 * 1000000ULL / us) / (1024 * 1024) : 0;      
    char a[24], b[24], c[24], d[24];
    dec64s(a, bytes / 1024); dec64s(b, ms); dec64s(c, mibs10 / 10); dec64s(d, mibs10 % 10);
    slog("  "); slog(what); slog(path); slog(": "); slog(a); slog(" KiB, "); slog(b); slog(" ms, ");
    slog(c); slog("."); slog(d); slog(" MiB/s\n");
}
static void b_read(const char *what, const char *path) {
    uint8_t *buf = (uint8_t *)kmalloc(65536);
    if (!buf) return;
    uint64_t t0 = b_tsc(), total = 0;
    int fd = vfs_open(path, O_RDONLY);
    if (fd >= 0) {
        for (;;) { int64_t r = vfs_read(fd, buf, 65536); if (r <= 0) break; total += (uint64_t)r; }
        vfs_close(fd);
    }
    b_report(what, path, total, b_tsc() - t0);
    kfree(buf);
}
static void storage_bench(void) {
    if (g_root_kind != 1 || !g_boot_mounted) return;
    int dfd = vfs_open("/boot/UI", O_DIRECTORY);
    if (dfd < 0) return;
    vfs_dirent_t *de = (vfs_dirent_t *)kmalloc(sizeof(*de));
    if (!de) { vfs_close(dfd); return; }
    char best[96] = ""; uint64_t bsz = 0;
    for (uint32_t i = 0; vfs_readdir(dfd, i, de) == VFS_OK; i++)
        if (de->type == VFS_TYPE_FILE && de->size > bsz && s_len(de->name) < 90) { bsz = de->size; s_cpy(best, de->name, sizeof(best)); }
    kfree(de); vfs_close(dfd);
    if (!bsz) return;
    char fp[128] = "/boot/UI/", hp[128] = "/UI/", nm[96];
    s_cat(fp, best, sizeof(fp)); fold_name(best, nm, sizeof(nm)); s_cat(hp, nm, sizeof(hp));
    slog("[BENCH] FAT32 (/boot) vs FHFS (/), same file:\n");
    b_read("FAT32 cold ", fp); b_read("FAT32 again", fp);
    b_read("FHFS  cold ", hp); b_read("FHFS  again", hp);

    uint8_t *buf = (uint8_t *)kmalloc(65536);
    if (!buf) return;
    for (int i = 0; i < 65536; i++) buf[i] = (uint8_t)(i * 31u + (i >> 8));       
    uint64_t t0 = b_tsc();
    int fd = vfs_open2("/tmp/bench.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    uint64_t total = 0;
    if (fd >= 0) { for (int i = 0; i < 64; i++) { if (vfs_write(fd, buf, 65536) != 65536) break; total += 65536; } vfs_close(fd); }
    b_report("FHFS write ", "/tmp/bench.bin (no commit)", total, b_tsc() - t0);
    t0 = b_tsc(); vfs_sync_all();
    b_report("FHFS commit", "", 0, b_tsc() - t0);
    b_read("FHFS read  ", "/tmp/bench.bin");
    vfs_unlink2("/tmp/bench.bin");
    kfree(buf);
}
#else
static void storage_bench(void) {}
#endif

 
int storage_init(void (*log)(const char *)) {
    g_log = log;
    fhfs_register();
    fat32_register();

    for (int round = 0; round < 80; round++) {
        scan_new_disks();
        if (!g_first_fat_seen) {
            for (int i = 0; i < g_nc; i++) if (g_c[i].kind == K_FAT) g_first_fat_seen = true;
        }
        if (try_fhfs_root()) break;
        if (g_first_fat_seen) {
             
            if (++g_fat_wait > 8 && try_legacy_root()) break;
        }
        if (round == 0) slog("[STORAGE] waiting for storage devices...\n");
        usb_delay_ms(250);
        usb_hotplug_poll();
    }
    if (g_root_kind == 0) return 0;
    mount_extra_volumes();
    storage_bench();
    return 1;
}

int storage_root_is_fhfs(void) { return g_root_kind == 1; }

void storage_poll(void) {
    scan_new_disks();
    if (g_root_kind == 0) return;
    mount_extra_volumes();
}

static void storage_daemon(void *arg) {
    (void)arg;
    for (;;) {
        sched_sleep_ns(2000000000ULL);
#if FEXOX_STORAGE_HOTPLUG_POLL
        usb_hotplug_poll();
#endif
        storage_poll();
    }
}

int storage_start_services(void) {
    int r = 0;
    if (g_root_kind == 1 || g_root_kind == 2) {
        if (fhfs_start_syncd() < 0) { slog("[WARN] fhfs sync thread not started\n"); r = -1; }
        if (sched_create_kthread(storage_daemon, NULL, "storaged", 0) < 0) r = -1;
    }
    return r;
}
