/* Fexox OS Project, EULA GPLv2 */
 













typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;

void *memcpy(void *d, const void *s, unsigned n) {
    u8 *dd = d; const u8 *ss = s;
    __asm__ volatile ("rep movsb" : "+D"(dd), "+S"(ss), "+c"(n) :: "memory");
    return d;
}
void *memset(void *d, int c, unsigned n) {
    u8 *dd = d;
    __asm__ volatile ("rep stosb" : "+D"(dd), "+c"(n) : "a"(c) : "memory");
    return d;
}

#include "stage2_core.h"

 
typedef struct __attribute__((packed)) {
    u32 eax, ebx, ecx, edx, esi, edi, ebp;
    u16 ds, es, fs, gs;
    u32 eflags;
} regs_t;
_Static_assert(sizeof(regs_t) == 40, "regs_t must be 40 bytes (see stage2_entry.S)");

extern void bios_int(u32 intno, regs_t *r);
extern void enter_long_mode(u32 pml4, u32 entry_lo, u32 entry_hi, u32 info, u32 stack);

 
#define LOW_DAP      0x7080u
#define LOW_E820     0x7090u
#define LOW_VBEINFO  0x7100u
#define LOW_VBEMODE  0x7300u
#define LOW_EDID     0x7400u
#define DISK_BUF     0x20000u
#define DISK_BUF_SEG 0x2000u

static u8 g_boot_drive;

static void call13(regs_t *r) { bios_int(0x13, r); }

 
static void putc_(char c) {
    regs_t r; memset(&r, 0, sizeof r);
    r.eax = 0x0E00u | (u8)c; r.ebx = 7;
    bios_int(0x10, &r);
}
static void puts_(const char *s) {
    while (*s) { if (*s == '\n') putc_('\r'); putc_(*s++); }
}
static void put_dec(u32 v) {
    char t[12]; int n = 0;
    if (!v) t[n++] = '0';
    while (v && n < 11) { t[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) putc_(t[--n]);
}
static void die(const char *msg) {
    puts_("\nFexox OS BIOS loader: ");
    puts_(msg);
    puts_("\n");
    for (;;) __asm__ volatile ("cli; hlt");
}
static void die_code(const char *msg, int code) {
    puts_("\nFexox OS BIOS loader: ");
    puts_(msg);
    puts_(" (code ");
    if (code < 0) { putc_('-'); code = -code; }
    put_dec((u32)code);
    puts_(")\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

 
typedef struct __attribute__((packed)) {
    u8 size, zero; u16 count, off, seg; u32 lba_lo, lba_hi;
} dap_t;

static int bios_read(u32 lba, u32 count, void *dst) {
    for (int attempt = 0; attempt < 4; attempt++) {
        dap_t *d = (dap_t *)LOW_DAP;
        d->size = 16; d->zero = 0; d->count = (u16)count;
        d->off = 0; d->seg = DISK_BUF_SEG; d->lba_lo = lba; d->lba_hi = 0;
        regs_t r; memset(&r, 0, sizeof r);
        r.eax = 0x4200; r.edx = g_boot_drive; r.esi = LOW_DAP;
        call13(&r);
        if (!(r.eflags & 1u)) {
            memcpy(dst, (void *)DISK_BUF, count * 512u);
            return 0;
        }
        memset(&r, 0, sizeof r);                  
        r.edx = g_boot_drive;
        call13(&r);
    }
    return -1;
}

 
static int cpu_has_long_mode(void) {
    u32 a, b, c, d;
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000000u));
    if (a < 0x80000001u) return 0;
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000001u));
    return (d >> 29) & 1u;
}

 
static int read_e820(e820_t *out, int max) {
    int n = 0;
    u32 cont = 0;
    for (int guard = 0; guard < 128 && n < max; guard++) {
        regs_t r; memset(&r, 0, sizeof r);
        memset((void *)LOW_E820, 0, 24);
        ((u32 *)LOW_E820)[5] = 1;                     
        r.eax = 0xE820; r.edx = 0x534D4150u; r.ecx = 24;
        r.ebx = cont; r.edi = LOW_E820; r.es = 0;
        bios_int(0x15, &r);
        if (r.eflags & 1u) break;
        if (r.eax != 0x534D4150u) break;
        const u32 *e = (const u32 *)LOW_E820;
        u32 attr = (r.ecx >= 24) ? e[5] : 1u;
        if ((attr & 1u) && (e[2] | e[3])) {           
            out[n].base = (u64)e[0] | ((u64)e[1] << 32);
            out[n].len  = (u64)e[2] | ((u64)e[3] << 32);
            out[n].type = e[4];
            n++;
        }
        cont = r.ebx;
        if (cont == 0) break;
    }
    if (n == 0) {                                     
        regs_t r; memset(&r, 0, sizeof r);
        r.eax = 0xE801;
        bios_int(0x15, &r);
        if (!(r.eflags & 1u)) {
            u32 kb_1m_16m = (r.eax & 0xFFFFu) ? (r.eax & 0xFFFFu) : (r.ecx & 0xFFFFu);
            u32 blk_16m   = (r.ebx & 0xFFFFu) ? (r.ebx & 0xFFFFu) : (r.edx & 0xFFFFu);
            out[0].base = 0x100000; out[0].len = (u64)kb_1m_16m * 1024u; out[0].type = 1;
            out[1].base = 0x1000000; out[1].len = (u64)blk_16m * 65536u; out[1].type = 1;
            n = 2;
        }
    }
    return n;
}

 
static int vbe_collect(vbe_mode_t *out, int max) {
    u8 *info = (u8 *)LOW_VBEINFO;
    memset(info, 0, 512);
    info[0] = 'V'; info[1] = 'B'; info[2] = 'E'; info[3] = '2';    
    regs_t r; memset(&r, 0, sizeof r);
    r.eax = 0x4F00; r.edi = LOW_VBEINFO; r.es = 0;
    bios_int(0x10, &r);
    if ((r.eax & 0xFFFFu) != 0x004F) return 0;
    if (info[0] != 'V' || info[1] != 'E' || info[2] != 'S' || info[3] != 'A') return 0;

    u32 lp = (u32)rd16(info + 14) + ((u32)rd16(info + 16) << 4);   
    const u16 *list = (const u16 *)lp;
    int n = 0;
    for (int i = 0; i < 512 && list[i] != 0xFFFF && n < max; i++) {
        u8 *mi = (u8 *)LOW_VBEMODE;
        memset(mi, 0, 256);
        regs_t q; memset(&q, 0, sizeof q);
        q.eax = 0x4F01; q.ecx = list[i]; q.edi = LOW_VBEMODE; q.es = 0;
        bios_int(0x10, &q);
        if ((q.eax & 0xFFFFu) != 0x004F) continue;
        u16 attr = rd16(mi + 0);
         
        if ((attr & 0x91u) != 0x91u) continue;
        u8 bpp = mi[25], model = mi[27];
        if (bpp != 32 || model != 6) continue;                     
        u32 phys = rd32(mi + 40);
        u16 w = rd16(mi + 18), h = rd16(mi + 20);
        u16 pitch = rd16(mi + 16);
        u16 lin = rd16(mi + 50);                                   
        if (lin >= (u16)(w * 4u)) pitch = lin;
        if (!phys || w < 640 || h < 480 || pitch < (u16)(w * 4u)) continue;
        out[n].mode = list[i]; out[n].w = w; out[n].h = h; out[n].pitch = pitch;
        out[n].bpp = bpp; out[n].phys = phys;
        out[n].rs = mi[31]; out[n].rp = mi[32];
        out[n].gs = mi[33]; out[n].gp = mi[34];
        out[n].bs = mi[35]; out[n].bp = mi[36];
        n++;
    }
    return n;
}

static void edid_native(u32 *w, u32 *h) {
    *w = *h = 0;
    u8 *ed = (u8 *)LOW_EDID;
    memset(ed, 0, 128);
    regs_t r; memset(&r, 0, sizeof r);
    r.eax = 0x4F15; r.ebx = 1; r.ecx = 0; r.edx = 0; r.edi = LOW_EDID; r.es = 0;
    bios_int(0x10, &r);
    if ((r.eax & 0xFFFFu) != 0x004F) return;
    static const u8 hdr[8] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };
    for (int i = 0; i < 8; i++) if (ed[i] != hdr[i]) return;
    if (!(ed[54] | ed[55])) return;                        
    *w = ((u32)(ed[58] & 0xF0) << 4) | ed[56];
    *h = ((u32)(ed[61] & 0xF0) << 4) | ed[59];
}

 
static fat_t g_fat;
static vbe_mode_t g_modes[128];
static e820_t g_e820[96];

void stage2_main(u32 boot_drive) {
    g_boot_drive = (u8)boot_drive;
    puts_("Fexox OS BIOS loader\n");

    if (!cpu_has_long_mode()) die("64-bit CPU required");

     
    u8 sec[512];
    if (bios_read(0, 1, sec) != 0) die("cannot read MBR");
    u32 part = mbr_pick_partition(sec);
    if (!part) die("no FAT32 partition in MBR");
    int rc = fat_mount(&g_fat, bios_read, part, sec);
    if (rc != 0) die_code("FAT32 mount failed", rc);

     
    u32 cl, size;
    if (fat_find_root(&g_fat, "KERNEL  BIN", &cl, &size) != 0) die("kernel.bin not found in root");
    if (size < 4096 || size > ELF_BUF_MAX - 4096) die("kernel.bin has unreasonable size");
    puts_("kernel.bin: "); put_dec(size); puts_(" bytes\n");
    if (fat_read_file(&g_fat, cl, size, ELF_BUF) != 0) die("kernel.bin read error");

    u64 entry = 0; u32 img_end = 0;
    rc = elf_load(ELF_BUF, size, &entry, &img_end);
    if (rc != 0) die_code("bad kernel ELF", rc);

     
    u32 want_w = 0, want_h = 0;
    if (fat_find_root(&g_fat, "DISPLAY CFG", &cl, &size) == 0 && size > 0 && size < 512) {
        u8 cfg[512 + 512];
        memset(cfg, 0, sizeof cfg);
        if (fat_read_file(&g_fat, cl, size, (u32)cfg) == 0) parse_wxh((const char *)cfg, size, &want_w, &want_h);
    }

     
    u32 ebda = (u32)(*(volatile u16 *)0x40E) << 4;
    u32 rsdp = 0;
    if (ebda >= 0x80000 && ebda < 0xA0000) rsdp = rsdp_scan(ebda, ebda + 1024);
    if (!rsdp) rsdp = rsdp_scan(0xE0000, 0x100000);

     
    int en = read_e820(g_e820, 96);
    if (en == 0) die("BIOS returned no memory map");
    int mn = build_memmap(g_e820, en, (efi_desc_t *)PHYS_PTR(MMAP_ADDR), 4096 / (int)sizeof(efi_desc_t));

     
    fbinfo_t fb; memset(&fb, 0, sizeof fb);
    fb.mode = FB_MODE_VGA_;
    int nm = vbe_collect(g_modes, 128);
    if (nm > 0) {
        u32 nat_w, nat_h;
        edid_native(&nat_w, &nat_h);
        for (int attempt = 0; attempt < 3; attempt++) {
            int k = vbe_pick(g_modes, nm, want_w, want_h, nat_w, nat_h, attempt);
            if (k < 0) break;
            const vbe_mode_t *m = &g_modes[k];
            regs_t r; memset(&r, 0, sizeof r);
            r.eax = 0x4F02; r.ebx = (u32)m->mode | 0x4000u;    
            bios_int(0x10, &r);
            if ((r.eax & 0xFFFFu) == 0x004F) {
                fb.mode = FB_MODE_LINEAR_;
                fb.width = m->w; fb.height = m->h; fb.pitch = m->pitch;
                fb.bpp = m->bpp; fb.phys_addr = m->phys;
                fb.red_mask = fb_mask(m->rs, m->rp);
                fb.green_mask = fb_mask(m->gs, m->gp);
                fb.blue_mask = fb_mask(m->bs, m->bp);
                break;
            }
        }
    }

     
    build_page_tables(PT_BASE);

    bootinfo_t *bi = (bootinfo_t *)PHYS_PTR(BOOTINFO_ADDR);
    memset(bi, 0, sizeof *bi);
    bi->magic        = BOOT_INFO_MAGIC_;
    bi->mem_map      = MMAP_ADDR;
    bi->mem_map_size = (u64)mn * sizeof(efi_desc_t);
    bi->desc_size    = sizeof(efi_desc_t);
    bi->desc_version = 1;
    bi->kernel_entry = entry;
    bi->pml4_phys    = PT_BASE;
    bi->fb           = fb;
    bi->rsdp_phys    = rsdp;

    enter_long_mode(PT_BASE, (u32)entry, (u32)(entry >> 32), BOOTINFO_ADDR, KSTACK_TOP);
    for (;;) __asm__ volatile ("cli; hlt");
}
