/* Fexox OS Project, EULA GPLv2 */
 









#ifndef STAGE2_CORE_H
#define STAGE2_CORE_H

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;

#ifdef HOST_TEST
#  include <string.h>
   extern u8 *g_phys_base;
#  define PHYS_PTR(a) ((void *)(g_phys_base + (u64)(a)))
#else
#  define PHYS_PTR(a) ((void *)(u32)(a))
#endif

 
#define KERNEL_PHYS_BASE 0x00200000u
#define KERNEL_LIMIT     0x00500000u    
#define PT_BASE          0x00500000u    
#define BOOTINFO_ADDR    0x00508000u
#define MMAP_ADDR        0x00509000u    
#define KSTACK_TOP       0x00510000u
#define ELF_BUF          0x00800000u    
#define ELF_BUF_MAX      0x00600000u    
#define USABLE_MIN       0x01000000ULL  
#define KERNEL_VIRT_BASE 0xFFFFFFFF80000000ULL

static inline u16 rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
static inline u32 rd32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static inline u64 rd64(const u8 *p) { return (u64)rd32(p) | ((u64)rd32(p + 4) << 32); }

 


typedef int (*rd_fn)(u32 lba, u32 count, void *dst);    

typedef struct {
    rd_fn rd;
    u32   part_lba, fat_lba, data_lba, root_cluster, spc;
    u32   fat_cache_lba;            
    u8    fat_cache[512];
} fat_t;

static int fat_mount(fat_t *f, rd_fn rd, u32 part_lba, u8 *scratch512) {
    f->rd = rd;
    f->part_lba = part_lba;
    f->fat_cache_lba = 0xFFFFFFFFu;
    if (rd(part_lba, 1, scratch512) != 0) return -1;
    const u8 *b = scratch512;
    if (b[510] != 0x55 || b[511] != 0xAA) return -2;
    if (rd16(b + 11) != 512) return -3;                        
    u32 spc = b[13];
    if (spc == 0 || (spc & (spc - 1))) return -4;
    u32 reserved = rd16(b + 14);
    u32 nfats    = b[16];
    if (nfats == 0) return -5;
    if (rd16(b + 17) != 0 || rd16(b + 22) != 0) return -6;     
    u32 fatsz = rd32(b + 36);
    if (fatsz == 0) return -7;
    f->spc          = spc;
    f->fat_lba      = part_lba + reserved;
    f->data_lba     = part_lba + reserved + nfats * fatsz;
    f->root_cluster = rd32(b + 44);
    if (f->root_cluster < 2) return -8;
    return 0;
}

static u32 fat_cluster_lba(const fat_t *f, u32 c) { return f->data_lba + (c - 2) * f->spc; }

static u32 fat_next(fat_t *f, u32 c) {
    u32 lba = f->fat_lba + (c >> 7);
    if (f->fat_cache_lba != lba) {
        if (f->rd(lba, 1, f->fat_cache) != 0) return 0x0FFFFFFFu;
        f->fat_cache_lba = lba;
    }
    return rd32(f->fat_cache + (c & 127) * 4) & 0x0FFFFFFFu;
}

 
static int fat_find_root(fat_t *f, const char *name11, u32 *first_cluster, u32 *size) {
    u8 sec[512];
    u32 c = f->root_cluster;
    for (u32 guard = 0; c >= 2 && c < 0x0FFFFFF8u && guard < 4096; guard++) {
        for (u32 s = 0; s < f->spc; s++) {
            if (f->rd(fat_cluster_lba(f, c) + s, 1, sec) != 0) return -1;
            for (int i = 0; i < 16; i++) {
                const u8 *e = sec + i * 32;
                if (e[0] == 0x00) return -2;                     
                if (e[0] == 0xE5) continue;
                if (e[11] == 0x0F) continue;                     
                if (e[11] & 0x18) continue;                      
                int same = 1;
                for (int k = 0; k < 11; k++) if ((char)e[k] != name11[k]) { same = 0; break; }
                if (!same) continue;
                *first_cluster = ((u32)rd16(e + 20) << 16) | rd16(e + 26);
                *size = rd32(e + 28);
                return 0;
            }
        }
        c = fat_next(f, c);
    }
    return -2;
}

 

static int fat_read_file(fat_t *f, u32 cluster, u32 size, u32 dst) {
    u32 remaining = size;
    u32 guard = 0;
    while (remaining > 0 && cluster >= 2 && cluster < 0x0FFFFFF8u && guard++ < (1u << 24)) {
        u32 lba = fat_cluster_lba(f, cluster);
        u32 left = f->spc;
        while (left > 0 && remaining > 0) {
            u32 n = left > 32 ? 32 : left;
            if (f->rd(lba, n, PHYS_PTR(dst)) != 0) return -1;
            u32 bytes = n * 512;
            dst += bytes; lba += n; left -= n;
            remaining = remaining > bytes ? remaining - bytes : 0;
        }
        if (remaining == 0) break;
        cluster = fat_next(f, cluster);
    }
    return remaining == 0 ? 0 : -2;
}

 


 

static u32 mbr_pick_partition(const u8 *mbr) {
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) return 0;
    u32 first_fat = 0;
    for (int i = 0; i < 4; i++) {
        const u8 *e = mbr + 0x1BE + 16 * i;
        u8 type = e[4];
        u32 lba = rd32(e + 8);
        if (lba == 0) continue;
        if (type != 0x0B && type != 0x0C && type != 0xEF) continue;
        if (e[0] == 0x80) return lba;
        if (!first_fat) first_fat = lba;
    }
    return first_fat;
}

 


static int elf_load(u32 file_addr, u32 file_size, u64 *entry, u32 *image_end) {
    const u8 *h = (const u8 *)PHYS_PTR(file_addr);
    if (file_size < 64) return -1;
    if (h[0] != 0x7F || h[1] != 'E' || h[2] != 'L' || h[3] != 'F') return -2;
    if (h[4] != 2 || h[5] != 1) return -3;                        
    if (rd16(h + 18) != 62) return -4;                            
    u64 phoff = rd64(h + 32);
    u32 phentsize = rd16(h + 54), phnum = rd16(h + 56);
    if (phentsize < 56 || phoff + (u64)phnum * phentsize > file_size) return -5;
    *entry = rd64(h + 24);
    u32 end = 0;
    for (u32 i = 0; i < phnum; i++) {
        const u8 *ph = h + (u32)phoff + i * phentsize;
        if (rd32(ph) != 1) continue;                              
        u64 off = rd64(ph + 8), paddr = rd64(ph + 24), filesz = rd64(ph + 32), memsz = rd64(ph + 40);
        if (memsz == 0) continue;
        if (paddr < 0x100000ULL || paddr + memsz > KERNEL_LIMIT) return -6;
        if (off + filesz > file_size || filesz > memsz) return -7;
        u8 *dst = (u8 *)PHYS_PTR((u32)paddr);
        memcpy(dst, h + (u32)off, (u32)filesz);
        memset(dst + (u32)filesz, 0, (u32)(memsz - filesz));
        if ((u32)(paddr + memsz) > end) end = (u32)(paddr + memsz);
    }
    if (end == 0) return -8;
    *image_end = end;
    return 0;
}

 


typedef struct { u64 base, len; u32 type; } e820_t;

typedef struct {
    u32 type, pad;
    u64 phys, virt, pages;
    u32 attr, pad2;
} efi_desc_t;                       

typedef struct { u64 a, b; } range_t;

#define EFI_T_LOADER_DATA 2
#define EFI_T_USABLE      7

static int range_subtract(range_t *w, int n, u64 ra, u64 rb, int cap) {
    range_t out[192];
    int m = 0;
    for (int i = 0; i < n && m + 2 <= 192; i++) {
        if (rb <= w[i].a || ra >= w[i].b) { out[m++] = w[i]; continue; }
        if (ra > w[i].a) { out[m].a = w[i].a; out[m].b = ra; m++; }
        if (rb < w[i].b) { out[m].a = rb; out[m].b = w[i].b; m++; }
    }
    if (m > cap) m = cap;
    for (int i = 0; i < m; i++) w[i] = out[i];
    return m;
}

static int memmap_add(efi_desc_t *out, int n, int max, u32 type, u64 base, u64 len) {
    if (n >= max || len == 0) return n;
    out[n].type = type; out[n].pad = 0;
    out[n].phys = base; out[n].virt = 0; out[n].pages = len >> 12;
    out[n].attr = 0; out[n].pad2 = 0;
    return n + 1;
}

 


static int build_memmap(const e820_t *e, int en, efi_desc_t *out, int max) {
    int n = 0;
    n = memmap_add(out, n, max, EFI_T_LOADER_DATA, 0, USABLE_MIN);
    for (int i = 0; i < en; i++) {
        if (e[i].type == 1 || e[i].len == 0) continue;
        u32 t = (e[i].type == 3) ? 9 : (e[i].type == 4) ? 10 : 0;
        u64 a = e[i].base & ~0xFFFULL;
        u64 b = (e[i].base + e[i].len + 0xFFFULL) & ~0xFFFULL;
        if (n + 8 >= max) break;                      
        n = memmap_add(out, n, max, t, a, b - a);
    }
    for (int i = 0; i < en; i++) {
        if (e[i].type != 1) continue;
        u64 a = (e[i].base + 0xFFFULL) & ~0xFFFULL;
        u64 b = (e[i].base + e[i].len) & ~0xFFFULL;
        if (a < USABLE_MIN) a = USABLE_MIN;
        if (b > (1ULL << 48)) b = (1ULL << 48);
        if (b <= a) continue;
        range_t w[64];
        int wn = 1;
        w[0].a = a; w[0].b = b;
        for (int j = 0; j < en && wn > 0; j++) {
            if (e[j].type == 1 || e[j].len == 0) continue;
            wn = range_subtract(w, wn, e[j].base, e[j].base + e[j].len, 64);
        }
        for (int k = 0; k < wn; k++) {
            u64 wa = (w[k].a + 0xFFFULL) & ~0xFFFULL, wb = w[k].b & ~0xFFFULL;
            if (wb > wa) n = memmap_add(out, n, max, EFI_T_USABLE, wa, wb - wa);
        }
    }
    for (int i = 1; i < n; i++) {                     
        efi_desc_t key = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].phys > key.phys) { out[j + 1] = out[j]; j--; }
        out[j + 1] = key;
    }
    return n;
}

 



static void build_page_tables(u32 base) {
    u64 *pml4    = (u64 *)PHYS_PTR(base);
    u64 *pdpt_id = (u64 *)PHYS_PTR(base + 0x1000);
    u64 *pdpt_hi = (u64 *)PHYS_PTR(base + 0x6000);
    u64 *pd_hi   = (u64 *)PHYS_PTR(base + 0x7000);
    memset(PHYS_PTR(base), 0, 0x8000);
    pml4[0] = (u64)(base + 0x1000) | 3;
    for (u32 i = 0; i < 4; i++) {
        u64 *pd = (u64 *)PHYS_PTR(base + 0x2000 + i * 0x1000);
        pdpt_id[i] = (u64)(base + 0x2000 + i * 0x1000) | 3;
        for (u32 j = 0; j < 512; j++)
            pd[j] = ((u64)(i * 512 + j) << 21) | 0x83;        
    }
    pml4[511]    = (u64)(base + 0x6000) | 3;
    pdpt_hi[510] = (u64)(base + 0x7000) | 3;
    for (u32 i = 0; i < 4; i++)
        pd_hi[i] = ((u64)KERNEL_PHYS_BASE + ((u64)i << 21)) | 0x83;
}

 


#define FB_MODE_VGA_    1
#define FB_MODE_LINEAR_ 2
#define BOOT_INFO_MAGIC_ 0x4B45524E454C424FULL

typedef struct __attribute__((packed)) {
    u8  mode; u8 pad0[3];
    u32 width, height, pitch;
    u8  bpp;  u8 pad1[7];
    u64 phys_addr;
    u32 red_mask, green_mask, blue_mask;
    u32 pad2;
} fbinfo_t;

typedef struct __attribute__((packed)) {
    u64 magic;
    u64 mem_map;
    u64 mem_map_size;
    u64 desc_size;
    u32 desc_version;
    u64 kernel_entry;
    u64 pml4_phys;
    u64 gdt_ptr;
    u64 idt_ptr;
    fbinfo_t fb;
    u64 rsdp_phys;
} bootinfo_t;

_Static_assert(sizeof(fbinfo_t) == 48, "fb_info_t layout");
_Static_assert(__builtin_offsetof(bootinfo_t, fb) == 68, "BOOT_INFO.fb offset");
_Static_assert(__builtin_offsetof(bootinfo_t, rsdp_phys) == 116, "BOOT_INFO.rsdp offset");
_Static_assert(sizeof(efi_desc_t) == 40, "efi_memory_desc_t layout");

 


static void parse_wxh(const char *s, u32 len, u32 *w, u32 *h) {
    u32 a = 0, b = 0, i = 0;
    int ha = 0, hb = 0;
    while (i < len && s[i] >= '0' && s[i] <= '9') { a = a * 10 + (u32)(s[i] - '0'); i++; ha = 1; }
    if (i < len && (s[i] == 'x' || s[i] == 'X')) {
        i++;
        while (i < len && s[i] >= '0' && s[i] <= '9') { b = b * 10 + (u32)(s[i] - '0'); i++; hb = 1; }
    }
    *w = (ha && hb) ? a : 0;
    *h = (ha && hb) ? b : 0;
}

typedef struct { u16 mode; u16 w, h, pitch; u8 bpp; u32 phys; u8 rs, rp, gs, gp, bs, bp; } vbe_mode_t;

 



static int vbe_pick(const vbe_mode_t *m, int n, u32 want_w, u32 want_h, u32 nat_w, u32 nat_h, int skip) {
     
    int order[128], cnt = 0;
    for (int i = 0; i < n && cnt < 128; i++) {
        if (m[i].bpp != 32) continue;
        order[cnt++] = i;
    }
     
    static const u32 EXACT_WANT = 0x40000000u, EXACT_NAT = 0x20000000u;
    u32 score[128];
    for (int k = 0; k < cnt; k++) {
        const vbe_mode_t *v = &m[order[k]];
        u32 area = (u32)v->w * v->h;
        u32 s = 0;
        if (want_w && v->w == want_w && v->h == want_h) s = EXACT_WANT;
        else if (nat_w && v->w == nat_w && v->h == nat_h) s = EXACT_NAT;
        else if (v->w <= 1920 && v->h <= 1200) s = area >> 4;
        score[k] = s;
    }
    int best = -1;
    for (int pass = 0; pass <= skip; pass++) {
        best = -1;
        for (int k = 0; k < cnt; k++) {
            if (score[k] == 0) continue;
            if (best < 0 || score[k] > score[best]) best = k;
        }
        if (best < 0) return -1;
        if (pass < skip) score[best] = 0;
    }
    return order[best];
}

static u32 fb_mask(u8 size, u8 pos) { return size ? (((size >= 32) ? 0xFFFFFFFFu : ((1u << size) - 1u)) << pos) : 0; }

 


static int rsdp_valid(const u8 *p) {
    static const char sig[8] = { 'R','S','D',' ','P','T','R',' ' };
    for (int i = 0; i < 8; i++) if ((char)p[i] != sig[i]) return 0;
    u8 sum = 0;
    for (int i = 0; i < 20; i++) sum = (u8)(sum + p[i]);
    return sum == 0;
}

static u32 rsdp_scan(u32 start, u32 end) {
    for (u32 a = start & ~15u; a + 20 <= end; a += 16)
        if (rsdp_valid((const u8 *)PHYS_PTR(a))) return a;
    return 0;
}

#endif  
