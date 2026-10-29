/* Fexox OS Project, EULA GPLv2 */
 


#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "debug_out.h"
#include "Scheduler.h"
#include "VFS.h"
#include "ELF64.h"
#include "Syscall.h"
#include "Framebuffer.h"
#include "ui_extra.h"
#include "PS2Mouse.h"
#include "USB.h"           
#include "Network/net.h"   
extern void ehci_diag_dump(void (*out)(const char *));    

 


static inline uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    uint32_t a = 0x80000000U | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (reg & 0xFC);
    __asm__ volatile ("outl %0,%1" :: "a"(a), "Nd"((uint16_t)0xCF8));
    uint32_t v; __asm__ volatile ("inl %1,%0" : "=a"(v) : "Nd"((uint16_t)0xCFC)); return v;
}

 




volatile uint64_t fexox_os_klog_head = 0;
char              fexox_os_klog_buf[FEXOX_OS_KLOG_SIZE];

 

static void fexox_os_klog_putc(char c) {
    if (c == '\r') return;
    uint64_t i = __atomic_fetch_add(&fexox_os_klog_head, 1, __ATOMIC_RELAXED);
    fexox_os_klog_buf[i & (FEXOX_OS_KLOG_SIZE - 1u)] = c;
}

 




long fexox_os_klog_read(uint64_t *pos_io, char *ubuf, uint64_t max) {
    uint64_t head = __atomic_load_n(&fexox_os_klog_head, __ATOMIC_RELAXED);
    uint64_t pos  = *pos_io;
    if (pos > head) pos = head;
    if (head - pos > FEXOX_OS_KLOG_SIZE) pos = head - FEXOX_OS_KLOG_SIZE;
    uint64_t n = head - pos;
    if (n > max) n = max;
    for (uint64_t i = 0; i < n; i++)
        ubuf[i] = fexox_os_klog_buf[(pos + i) & (FEXOX_OS_KLOG_SIZE - 1u)];
    *pos_io = pos + n;
    return (long)n;
}

 


extern int       ide_init(void);
extern blkdev_t *ide_find_drive(const char *name);

extern int       ahci_init(void);
extern blkdev_t *ahci_find_drive(const char *name);

extern int       nvme_init(void);
extern blkdev_t *nvme_find_drive(const char *name);

 






extern void usb_init(void);

typedef struct __attribute__((packed)) {
    uint64_t magic;
    void    *mem_map;
    uint64_t mem_map_size;
    uint64_t desc_size;
    uint32_t desc_version;
    uint64_t kernel_entry;
    uint64_t pml4_phys;
    void    *gdt_ptr;
    void    *idt_ptr;
    fb_info_t fb;
    uint64_t rsdp_phys;    
} BOOT_INFO;

#define BOOT_INFO_MAGIC 0x4B45524E454C424FULL

extern int  mem_control_init(const void *efi_map, uint64_t map_size, uint64_t desc_size,
                             uint64_t fb_phys, uint32_t fb_w, uint32_t fb_h,
                             uint32_t fb_pitch, uint8_t fb_mode);
extern int  interrupt_init(uint64_t ioapic_phys_addr, uint32_t timer_freq_hz);
extern int  acpi_init(uint64_t rsdp_phys);
extern void *kmalloc(uint64_t size);
extern void  kfree(void *ptr);

 


#define VGA_PHYS  ((uint16_t *)0xB8000ULL)
#define VGA_VIRT  ((uint16_t *)(0xFFFF880000000000ULL + 0xB8000ULL))

static uint16_t *vga = VGA_PHYS;
static int vga_row;
static int vga_col;

static void kprint(const char *str) {
    dbg_puts(str);
    while (*str) {
        if (*str == '\n') {
            vga_row++;
            vga_col = 0;
        } else {
            vga[vga_row * 80 + vga_col] = (uint16_t)(0x0700 | (uint8_t)*str);
            vga_col++;
            if (vga_col >= 80) { vga_col = 0; vga_row++; }
        }
        if (vga_row >= 25) {
            for (int i = 0; i < 24 * 80; i++) vga[i] = vga[i + 80];
            for (int i = 24 * 80; i < 25 * 80; i++) vga[i] = 0x0720;
            vga_row = 24;
        }
        str++;
    }
}

static void kprint_err(const char *msg, int code) {
    kprint(msg);
    dbg_puts(" code=");
    if (code < 0) { dbg_putc('-'); dbg_dec64((uint64_t)(-(int64_t)code)); }
    else           { dbg_dec64((uint64_t)code); }
    dbg_puts("\n");
}

 







static bool g_ui_desktop_shown = false;

static void kout(const char *str) {
    if (g_ui_desktop_shown) { dbg_puts(str); return; }
    if (fb_get_mode() == FB_MODE_LINEAR)
        fbcon_puts(str);
    else
        kprint(str);
}

 
static void kout_dec(const char *label, uint64_t v) {
    char t[24]; int n = 0;
    if (v == 0) t[n++] = '0';
    while (v > 0 && n < 22) { t[n++] = (char)('0' + (v % 10)); v /= 10; }
    char out[24]; int o = 0;
    while (n > 0) out[o++] = t[--n];
    out[o] = '\0';
    kout(label); kout(out); kout("\n");
}
static void kout_hex16(uint16_t v) {
    static const char hx[] = "0123456789ABCDEF";
    char out[5] = { hx[(v >> 12) & 15], hx[(v >> 8) & 15], hx[(v >> 4) & 15], hx[v & 15], '\0' };
    kout(out);
}

 






#ifndef FEXOX_OS_BOOT_MARKERS
#define FEXOX_OS_BOOT_MARKERS 1
#endif
static void boot_mark(const char *s) {
#if FEXOX_OS_BOOT_MARKERS
    if (fb_get_mode() != FB_MODE_LINEAR) return;
    int32_t y = (int32_t)fb_height() - 130;
    if (y < 0) return;
    fb_draw_string(10, y, "                                        ", FB_WHITE, FB_BLACK);
    fb_draw_string(10, y, s, FB_WHITE, FB_BLACK);
    fb_flip_rect(10, y, 40 * FB_FONT_W, FB_FONT_H);
#else
    (void)s;
#endif
}

 



 
extern void usb_msc_get_stats(uint32_t *errors, uint32_t *recovered, int *last_rc, uint64_t *last_lba);
extern void kmalloc_get_diag(uint32_t *fails, uint64_t *last_fail_size, uint64_t *heap_used);
extern volatile int32_t fexox_os_dbg_wp_reason, fexox_os_dbg_wp_rc, fexox_os_dbg_spawn_rc;
extern bool usb_hid_get_kbd_stats(uint32_t *ok, uint32_t *fail, uint32_t *keys, uint8_t rep[8], char *mode);

static char *dbg_s(char *d, const char *s) { while (*s) *d++ = *s++; return d; }
static char *dbg_u(char *d, uint64_t v) {
    char t[24]; int n = 0;
    if (v == 0) t[n++] = '0';
    while (v > 0 && n < 22) { t[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n > 0) *d++ = t[--n];
    return d;
}
static char *dbg_i(char *d, int64_t v) {
    if (v < 0) { *d++ = '-'; v = -v; }
    return dbg_u(d, (uint64_t)v);
}
static void dbg_line(int32_t y, const char *text) {
    fb_draw_string(10, y, "                                                                                ", FB_WHITE, FB_BLACK);
    fb_draw_string(10, y, text, FB_WHITE, FB_BLACK);
    fb_flip_rect(10, y, 80 * FB_FONT_W, FB_FONT_H);
}

 







void fexox_os_isr_heartbeat(void) {
#if FEXOX_OS_BOOT_MARKERS
    static uint32_t ticks = 0;
    if ((++ticks % 100u) != 0) return;
    if (fb_get_mode() != FB_MODE_LINEAR) return;
    int32_t y0 = (int32_t)fb_height() - 130 + 18;
    if (y0 < 0) return;

    char b[256]; char *d;

    d = dbg_s(b, "ISR ticks: "); d = dbg_u(d, ticks);
    {
         



        static const char hx[] = "0123456789ABCDEF";
        uint32_t kok = 0, kfail = 0, kkeys = 0; uint8_t rep[8] = {0};
        char kmode = '?';
        if (usb_hid_get_kbd_stats(&kok, &kfail, &kkeys, rep, &kmode)) {
            d = dbg_s(d, "  kbd["); *d++ = kmode; d = dbg_s(d, "] ok="); d = dbg_u(d, kok);
            d = dbg_s(d, " f="); d = dbg_u(d, kfail);
            d = dbg_s(d, " k="); d = dbg_u(d, kkeys);
            d = dbg_s(d, " r=");
            for (int i = 0; i < 8; i++) { *d++ = hx[rep[i] >> 4]; *d++ = hx[rep[i] & 15]; if (i < 7) *d++ = ' '; }
        } else {
            d = dbg_s(d, "  kbd: none");
        }
    }
    *d = '\0';
    dbg_line(y0, b);

    uint32_t me = 0, mr = 0; int mrc = 0; uint64_t mlba = 0;
    usb_msc_get_stats(&me, &mr, &mrc, &mlba);
    d = dbg_s(b, "msc err="); d = dbg_u(d, me);
    d = dbg_s(d, " recovered="); d = dbg_u(d, mr);
    d = dbg_s(d, " last_rc="); d = dbg_i(d, mrc);
    d = dbg_s(d, " lba="); d = dbg_u(d, mlba); *d = '\0';
    dbg_line(y0 + 18, b);

    uint32_t kf = 0; uint64_t ks = 0, ku = 0;
    kmalloc_get_diag(&kf, &ks, &ku);
    d = dbg_s(b, "km fail="); d = dbg_u(d, kf);
    d = dbg_s(d, " sz="); d = dbg_u(d, ks);
    d = dbg_s(d, " used="); d = dbg_u(d, ku / 1024u); d = dbg_s(d, "K");
    d = dbg_s(d, " wp="); d = dbg_i(d, fexox_os_dbg_wp_reason);
    d = dbg_s(d, "/"); d = dbg_i(d, fexox_os_dbg_wp_rc);
    d = dbg_s(d, " spawn="); d = dbg_i(d, fexox_os_dbg_spawn_rc); *d = '\0';
    dbg_line(y0 + 36, b);

    net_status_text(b, sizeof(b));
    dbg_line(y0 + 54, b);

     








    {
        const usb_stats_t *us = usb_get_stats();
        d = dbg_s(b, "usb hc="); d = dbg_u(d, us->hcds);
        d = dbg_s(d, " dev="); d = dbg_u(d, us->devices);
        d = dbg_s(d, " hub="); d = dbg_u(d, us->hubs);
        d = dbg_s(d, " hid="); d = dbg_u(d, us->hid);
        d = dbg_s(d, " msc="); d = dbg_u(d, us->msc);
        d = dbg_s(d, " split="); d = dbg_u(d, us->split_skipped);
        d = dbg_s(d, " fail="); d = dbg_u(d, us->enum_fail);
        d = dbg_s(d, " st="); d = dbg_u(d, us->fail_step);
        d = dbg_s(d, " rc="); d = dbg_i(d, us->fail_rc);
         



        {
            extern void ehci_int_dbg(uint32_t out[5]);
            uint32_t ei[5]; ehci_int_dbg(ei);
            d = dbg_s(d, " | ei c="); d = dbg_u(d, ei[0]);
            d = dbg_s(d, " p="); d = dbg_u(d, ei[1]);
            d = dbg_s(d, " d="); d = dbg_u(d, ei[2]);
            d = dbg_s(d, " h="); d = dbg_u(d, ei[3]);
            d = dbg_s(d, " t="); d = dbg_u(d, ei[4]);
        }
        *d = '\0';
        dbg_line(y0 + 72, b);
    }
#endif
}

 



static int ps2_controller_present(void) {
    uint8_t st;
    __asm__ volatile ("inb %1,%0" : "=a"(st) : "Nd"((uint16_t)0x64));
    return st != 0xFF;
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
static blkdev_ops_t g_part_ops = { part_read, part_write, part_destroy };

#define BOOT_MAX_PARTS 4
static blkdev_t    g_part_dev[BOOT_MAX_PARTS * 2];
static part_priv_t g_part_priv[BOOT_MAX_PARTS * 2];
static int         g_part_used = 0;

static uint32_t rd_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd_le64(const uint8_t *p) {
    return (uint64_t)rd_le32(p) | ((uint64_t)rd_le32(p + 4) << 32);
}

 
static int sector_is_fat_bpb(const uint8_t *s) {
    if (s[510] != 0x55 || s[511] != 0xAA) return 0;
    if (s[0] != 0xEB && s[0] != 0xE9) return 0;               
    uint16_t bps = (uint16_t)(s[11] | (s[12] << 8));
    if (bps != 512 && bps != 1024 && bps != 2048 && bps != 4096) return 0;
    uint8_t spc = s[13];
    if (spc == 0 || (spc & (spc - 1))) return 0;               
    if (s[16] == 0) return 0;                                  
    return 1;
}

static int root_has_boot_marker(void) {
    vfs_stat_t st;
    static const char *marks[] = { "/kernel.bin", "/EFI", "/UI", "/APPS", NULL };
    for (int i = 0; marks[i]; i++)
        if (vfs_stat(marks[i], &st) == VFS_OK) return 1;
    return 0;
}

 
static int try_mount_root(blkdev_t *dev) {
    int r = vfs_mount(dev->name, "fat32", "/", 0);
    if (r != VFS_OK) {
        kout("  FAT32 mount failed: "); kout(dev->name); kout("\n");
        return 0;
    }
    if (!root_has_boot_marker()) {
        kout("  no kernel.bin/EFI/UI on "); kout(dev->name); kout(" - not our boot volume, unmounted\n");
        vfs_umount("/");
        return 0;
    }
    kout("[OK] / mounted (FAT32, dev="); kout(dev->name); kout(")\n");
    return 1;
}

static int mount_boot_volume(blkdev_t *bd) {
    uint32_t ss = bd->sector_size ? bd->sector_size : 512u;
    if (ss > 4096u) { kout("  sector size > 4096, skip\n"); return 0; }

    uint8_t *sec = (uint8_t *)kmalloc(4096);
    if (!sec) return 0;

    if (blkdev_read(bd, 0, 1, sec) != VFS_OK) {
        kout("  read LBA 0 failed on "); kout(bd->name); kout("\n");
        kfree(sec);
        return 0;
    }

    int mounted = 0;

    if (sector_is_fat_bpb(sec)) {
         
        kout("  layout: FAT32 without partition table\n");
        kfree(sec);
        return try_mount_root(bd);
    }

    if (sec[510] != 0x55 || sec[511] != 0xAA) {
        kout("  no FAT and no MBR/GPT signature on "); kout(bd->name); kout("\n");
        kfree(sec);
        return 0;
    }

     
    uint64_t starts[BOOT_MAX_PARTS], sizes[BOOT_MAX_PARTS];
    int ncand = 0;
    int is_gpt = 0;
    for (int i = 0; i < 4; i++)
        if (sec[0x1BE + 16 * i + 4] == 0xEE) is_gpt = 1;

    if (!is_gpt) {
        kout("  layout: MBR\n");
        for (int i = 0; i < 4 && ncand < BOOT_MAX_PARTS; i++) {
            const uint8_t *e = sec + 0x1BE + 16 * i;
            uint8_t type = e[4];
            if (type != 0x0B && type != 0x0C && type != 0xEF) continue;    
            uint32_t lba = rd_le32(e + 8), n = rd_le32(e + 12);
            if (lba == 0 || n == 0) continue;
            starts[ncand] = lba; sizes[ncand] = n; ncand++;
        }
    } else {
        kout("  layout: GPT\n");
        if (blkdev_read(bd, 1, 1, sec) == VFS_OK && sec[0] == 'E' && sec[1] == 'F' && sec[2] == 'I' &&
            sec[3] == ' ' && sec[4] == 'P' && sec[5] == 'A' && sec[6] == 'R' && sec[7] == 'T') {
            uint64_t ent_lba = rd_le64(sec + 72);
            uint32_t nent    = rd_le32(sec + 80);
            uint32_t esz     = rd_le32(sec + 84);
            if (nent > 128) nent = 128;
            if (esz >= 128 && esz <= 512 && ent_lba != 0) {
                 


                static const uint8_t g_esp[16]  = {0x28,0x73,0x2A,0xC1,0x1F,0xF8,0xD2,0x11,0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B};
                static const uint8_t g_data[16] = {0xA2,0xA0,0xD0,0xEB,0xE5,0xB9,0x33,0x44,0x87,0xC0,0x68,0xB6,0xB7,0x26,0x99,0xC7};
                uint64_t cur_sector = (uint64_t)-1;
                for (uint32_t i = 0; i < nent && ncand < BOOT_MAX_PARTS; i++) {
                    uint64_t byte_off = (uint64_t)i * esz;
                    uint64_t s_lba = ent_lba + byte_off / ss;
                    uint32_t in_s  = (uint32_t)(byte_off % ss);
                    if (in_s + esz > ss) continue;    
                    if (s_lba != cur_sector) {
                        if (blkdev_read(bd, s_lba, 1, sec) != VFS_OK) break;
                        cur_sector = s_lba;
                    }
                    const uint8_t *e = sec + in_s;
                    int is_esp = 1, is_data = 1;
                    for (int k = 0; k < 16; k++) {
                        if (e[k] != g_esp[k])  is_esp = 0;
                        if (e[k] != g_data[k]) is_data = 0;
                    }
                    if (!is_esp && !is_data) continue;
                    uint64_t first = rd_le64(e + 32), last = rd_le64(e + 40);
                    if (first == 0 || last < first) continue;
                    starts[ncand] = first; sizes[ncand] = last - first + 1; ncand++;
                }
            }
        } else {
            kout("  GPT header not found\n");
        }
    }
    kfree(sec);

    if (ncand == 0) { kout("  no FAT partitions found\n"); return 0; }

    for (int i = 0; i < ncand && !mounted; i++) {
        if (starts[i] + sizes[i] > bd->sector_count) continue;    
        if (g_part_used >= BOOT_MAX_PARTS * 2) break;

        blkdev_t   *pd = &g_part_dev[g_part_used];
        part_priv_t *pp = &g_part_priv[g_part_used];
        g_part_used++;

        pp->base = bd; pp->first_lba = starts[i];
        int nl = 0;
        for (const char *q = bd->name; *q && nl < 20; q++) pd->name[nl++] = *q;
        pd->name[nl++] = 'p'; pd->name[nl++] = (char)('1' + i); pd->name[nl] = '\0';
        pd->sector_size  = bd->sector_size;
        pd->sector_count = sizes[i];
        pd->priv         = pp;
        pd->ops          = &g_part_ops;
        pd->readonly     = bd->readonly;

        if (blkdev_register(pd) < 0) { kout("  blkdev_register failed for partition\n"); break; }
        kout("  trying partition "); kout(pd->name); kout("\n");
        mounted = try_mount_root(pd);
    }
    return mounted;
}

 













 
typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp0;        
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];      
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} tss64_t;

 










static uint64_t  g_gdt[8] __attribute__((aligned(16)));
static tss64_t   g_tss    __attribute__((aligned(16)));

 





























void tss_set_rsp0(uint64_t rsp0) {
    g_tss.rsp0 = rsp0;
}

 
#define SYSCALL_KSTACK_SIZE (16 * 1024)
static uint8_t g_syscall_kstack[SYSCALL_KSTACK_SIZE] __attribute__((aligned(16)));

static void gdt_set_entry(int idx, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t gran) {
    g_gdt[idx]  = (uint64_t)(limit & 0xFFFF);
    g_gdt[idx] |= (uint64_t)(base  & 0xFFFFFF) << 16;
    g_gdt[idx] |= (uint64_t)access             << 40;
    g_gdt[idx] |= (uint64_t)((limit >> 16) & 0x0F) << 48;
    g_gdt[idx] |= (uint64_t)(gran & 0xF0)      << 48;
    g_gdt[idx] |= (uint64_t)((base >> 24) & 0xFF) << 56;
}

static void gdt_set_tss(int idx, uint64_t base, uint32_t limit) {
     
    g_gdt[idx]     = (uint64_t)(limit & 0xFFFF);
    g_gdt[idx]    |= (base & 0xFFFFFF) << 16;
    g_gdt[idx]    |= (uint64_t)0x89 << 40;          
    g_gdt[idx]    |= (uint64_t)((limit >> 16) & 0x0F) << 48;
    g_gdt[idx]    |= (uint64_t)((base >> 24) & 0xFF) << 56;
    g_gdt[idx + 1] = (base >> 32);                  
}

 
#define MSR_EFER    0xC0000080
#define MSR_STAR    0xC0000081
#define MSR_LSTAR   0xC0000082
#define MSR_SFMASK  0xC0000084

static inline void wrmsr(uint32_t msr, uint64_t val) {
    __asm__ volatile ("wrmsr"
        :: "c"(msr), "a"((uint32_t)val), "d"((uint32_t)(val >> 32)));
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

 













__attribute__((naked))
static void syscall_entry_stub(void) {
     













    __asm__ volatile (
        "mov %%rsp, fexox_os_percpu(%%rip)\n\t"         
        "mov fexox_os_percpu+8(%%rip), %%rsp\n\t"       

        "push %%r11\n\t"                    
        "push %%rcx\n\t"                    
        "push fexox_os_percpu(%%rip)\n\t"      

        "push %%rdi\n\t"                    
        "mov %%rax, %%rdi\n\t"              
        "mov %%r8,  %%r9\n\t"               
        "mov %%r10, %%r8\n\t"               
        "mov %%rdx, %%rcx\n\t"              
        "mov %%rsi, %%rdx\n\t"              
        "pop %%rsi\n\t"                     

        "call syscall_dispatch\n\t"

        "pop %%r10\n\t"                     
        "pop %%rcx\n\t"                     
        "pop %%r11\n\t"                     
        "mov %%r10, %%rsp\n\t"
        "sysretq\n\t"
        ::: "memory"
    );
}

 






typedef struct {
    uint64_t user_rsp_scratch;    
    uint64_t kernel_rsp;          
} percpu_t;

 
percpu_t fexox_os_percpu __attribute__((aligned(16)));
#define g_percpu fexox_os_percpu

 



















void syscall_set_kstack(uint64_t rsp0) {
    g_percpu.kernel_rsp = rsp0;
}

#define MSR_GS_BASE      0xC0000101
#define MSR_KERNEL_GS    0xC0000102
#define CR4_FSGSBASE     (1U << 16)

static void usermode_init(void) {
    DBG_MSG("KR", "usermode_init: setup GDT+TSS+SYSCALL");

     
    uint64_t kstack_top = (uint64_t)(uintptr_t)
        (g_syscall_kstack + SYSCALL_KSTACK_SIZE);
    kstack_top &= ~0xFULL;   

    for (int i = 0; i < (int)sizeof(tss64_t); i++)
        ((uint8_t *)&g_tss)[i] = 0;
    g_tss.rsp0        = kstack_top;
    g_tss.iopb_offset = sizeof(tss64_t);

     







    g_gdt[0] = 0;                                      
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xA0);         
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xC0);         
    g_gdt[3] = 0;                                      
    gdt_set_entry(4, 0, 0xFFFFF, 0xF2, 0xC0);         
    gdt_set_entry(5, 0, 0xFFFFF, 0xFA, 0xA0);         
    gdt_set_tss  (6, (uint64_t)(uintptr_t)&g_tss,
                     sizeof(tss64_t) - 1);             

     
    struct __attribute__((packed)) { uint16_t limit; uint64_t base; } gdtr;
    gdtr.limit = sizeof(g_gdt) - 1;
    gdtr.base  = (uint64_t)(uintptr_t)g_gdt;
    __asm__ volatile ("lgdt %0" :: "m"(gdtr) : "memory");

     




    __asm__ volatile (
        "pushq $0x08\n\t"             
        "lea 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        ::: "rax", "memory"
    );

     
    __asm__ volatile (
        "mov $0x10, %%ax\n\t"    
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%ss\n\t"
        "xor %%ax, %%ax\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        ::: "ax", "memory"
    );

     
    __asm__ volatile ("ltr %0" :: "r"((uint16_t)0x30));

     








    {
        uint32_t eax, ebx, ecx, edx;
        __asm__ volatile ("cpuid"
            : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
            : "a"(7), "c"(0));
        bool has_fsgsbase = (ebx & 1) != 0;

        uint64_t cr4;
        __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
        if (has_fsgsbase) {
            __asm__ volatile ("mov %0, %%cr4" :: "r"(cr4 | CR4_FSGSBASE) : "memory");
        } else {
            DBG_MSG("KR", "usermode_init: FSGSBASE not supported, skipping CR4 bit");
        }
    }

     
    g_percpu.user_rsp_scratch = 0;
    g_percpu.kernel_rsp       = kstack_top;

     





     
    wrmsr(MSR_GS_BASE,   0ULL);
    wrmsr(MSR_KERNEL_GS, 0ULL);

     
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | 1ULL);   

     






    uint64_t star = ((uint64_t)0x0008 << 32) |
                    ((uint64_t)0x0018 << 48);
    wrmsr(MSR_STAR,   star);
    wrmsr(MSR_LSTAR,  (uint64_t)(uintptr_t)syscall_entry_stub);
    wrmsr(MSR_SFMASK, (1ULL << 9));   

     
    syscall_init();

    DBG_MSG("KR", "usermode_init: GDT/TSS/SYSCALL ok (no swapgs, RIP-relative percpu)");
}

 


__attribute__((unused)) static void worker_a(void *arg) {
    (void)arg;
    uint32_t n = 0;
    while (1) {
        if ((n & 0xFFFFF) == 0)
            DBG_MSG("SC", "worker_a tick");
        n++;
        if (n == 0x300000) sched_exit();
    }
}

__attribute__((unused)) static void worker_b(void *arg) {
    (void)arg;
    uint32_t n = 0;
    while (1) {
        if ((n & 0xFFFFF) == 0)
            DBG_MSG("SC", "worker_b tick");
        n++;
        if (n == 0x300000) sched_exit();
    }
}

 





static void try_spawn_init(void) {
     
    static const char *init_paths[] = { "/init", "/bin/init", "/sbin/init", NULL };

    for (int i = 0; init_paths[i] != NULL; i++) {
        DBG_MSG("ELF", "trying to spawn ELF...");

         
        vfs_stat_t st;
        int sr = vfs_stat(init_paths[i], &st);
        if (sr != VFS_OK) continue;
        if (st.type != VFS_TYPE_FILE) continue;

        kprint("[ELF] spawning ");
        kprint(init_paths[i]);
        kprint("\n");

        int tid = elf64_spawn(init_paths[i], "init", 0);
        if (tid >= 0) {
            kprint("[OK] ELF64 process spawned (tid=");
            dbg_dec64((uint64_t)(uint32_t)tid);
            kprint(")\n");
            return;
        }

         
        kprint("[WARN] elf64_spawn failed, code=");
        if (tid < 0) { dbg_putc('-'); dbg_dec64((uint64_t)(uint32_t)(-tid)); }
        kprint("\n");
        return;   
    }

    kprint("[INFO] no /init found - skipping ELF spawn\n");
    DBG_MSG("ELF", "no init binary on fs");
}

 


void kernel_entry(BOOT_INFO *info) {
    __asm__ volatile ("cli" ::: "memory");

     

    fexox_os_klog_head = 0;
    fexox_os_klog_hook = fexox_os_klog_putc;

    DBG_MSG("KR", "10 kernel_entry");
    vga_row = 0; vga_col = 0;
    vga = VGA_PHYS;

    if (!info)                          DBG_PANIC("KR", "info NULL");
    if (info->magic != BOOT_INFO_MAGIC) DBG_PANIC("KR", "bad magic");

    kprint("=== Fexox OS Kernel ===\n");
    DBG_MSG("KR", "11 BOOT_INFO ok");

     








    uint64_t   saved_rsdp_phys = info->rsdp_phys;
    fb_info_t  saved_fb        = info->fb;

     
    DBG_MSG("KR", "12 mem_control_init...");
    int ret = mem_control_init(info->mem_map, info->mem_map_size, info->desc_size,
                               info->fb.phys_addr, info->fb.width, info->fb.height,
                               info->fb.pitch, info->fb.mode);
    if (ret != 0) {
        kprint_err("[FATAL] mem_control_init", ret);
        DBG_PANIC("KR", "mem_control_init");
    }
    vga = VGA_VIRT;   
    kprint("[OK] memory\n");
    DBG_MSG("KR", "13 memory ok");

     



    DBG_MSG("KR", "13a acpi_init...");
    if (acpi_init(saved_rsdp_phys) == 0) {
        kprint("[OK] acpi\n");
    } else {
        kprint("[INFO] no ACPI (rsdp not found / parse failed)\n");
    }

     
    DBG_MSG("KR", "13b fb_init...");
    int fb_mode = fb_init(&saved_fb);
    if (fb_mode == FB_MODE_LINEAR) {
         





        fbcon_init(0, 0, fb_width(), fb_height(), FB_WHITE, FB_DARK_BLUE);
        fbcon_puts("=== Fexox OS Kernel ===\n");
        DBG_MSG("KR", "13b fb linear OK");
    } else {
         
        kprint("[INFO] no GOP, VGA text mode\n");
        DBG_MSG("KR", "13b fb VGA fallback");
    }

     















    DBG_MSG("KR", "16 usermode_init...");
    usermode_init();
    kout("[OK] usermode\n");
    DBG_MSG("KR", "17 usermode ok");

     
    DBG_MSG("KR", "14 interrupt_init...");
    ret = interrupt_init(0xFEC00000ULL, 100);
    if (ret != 0) {
        kprint_err("[FATAL] interrupt_init", ret);
        DBG_PANIC("KR", "interrupt_init");
    }
    kout("[OK] interrupts\n");
    DBG_MSG("KR", "15 interrupts ok");

     










    __asm__ volatile ("sti" ::: "memory");

    DBG_MSG("KR", "20 vfs_init...");
    ret = vfs_init();
    if (ret != 0) {
        kprint_err("[WARN] vfs_init", ret);
        goto skip_fs;
    }
    kout("[OK] vfs\n");

     
    fat32_register();

     


    DBG_MSG("KR", "21 virtio_blk_init...");
    ret = virtio_blk_init();
    if (ret == 0) {
        kout("[OK] virtio-blk (vda)\n");
    } else {
        kprint_err("[INFO] virtio_blk_init (no device)", ret);
    }

     











#if defined(FEXOX_OS_PROBE_INTERNAL_DISKS) && FEXOX_OS_PROBE_INTERNAL_DISKS
    DBG_MSG("KR", "22 nvme_init...");
    ret = nvme_init();
    if (ret == 0) {
        kout("[OK] NVMe\n");
    } else {
        kprint_err("[INFO] nvme_init (no NVMe drives)", ret);
    }

    DBG_MSG("KR", "22 ahci_init...");
    ret = ahci_init();
    if (ret == 0) {
        kout("[OK] AHCI\n");
    } else {
        kprint_err("[INFO] ahci_init (no AHCI HBA)", ret);
    }

    DBG_MSG("KR", "23 ide_init...");
    ret = ide_init();
    if (ret == 0) {
        kout("[OK] IDE\n");
    } else {
        kprint_err("[INFO] ide_init (no IDE drives)", ret);
    }
#else
    kout("[SAFE] internal NVMe/SATA/IDE disks are NOT touched\n");
#endif

     










     






    {
        uint32_t n_uhci = 0, n_ohci = 0, n_ehci = 0, n_xhci = 0, n_other = 0;
        for (uint32_t bus = 0; bus < 256; bus++) {
            for (uint32_t dev = 0; dev < 32; dev++) {
                bool multi = false;
                for (uint32_t fn = 0; fn < 8; fn++) {
                    uint32_t id = pci_read32((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0x00);
                    if ((id & 0xFFFFu) == 0xFFFFu) { if (fn == 0) break; else continue; }
                    uint32_t cls = pci_read32((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0x08);
                    uint8_t class_code = (uint8_t)(cls >> 24);
                    uint8_t subclass   = (uint8_t)(cls >> 16);
                    uint8_t prog_if    = (uint8_t)(cls >> 8);
                    uint8_t hdr = (uint8_t)(pci_read32((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0x0C) >> 16);
                    if (fn == 0) multi = (hdr & 0x80) != 0;
                    if (class_code == 0x0C && subclass == 0x03) {
                        switch (prog_if) {
                            case 0x00: n_uhci++;  break;
                            case 0x10: n_ohci++;  break;
                            case 0x20: n_ehci++;  break;
                            case 0x30: n_xhci++;  break;
                            default:   n_other++; break;
                        }
                    }
                    if (fn == 0 && !multi) break;
                }
            }
        }
        char nb[16];
        #define KOUT_NUM(label, val) do { \
            uint32_t _v = (val); int _i = 0; \
            if (_v == 0) nb[_i++] = '0'; \
            else { char _t[12]; int _tn = 0; \
                   while (_v > 0) { _t[_tn++] = (char)('0' + (_v % 10)); _v /= 10; } \
                   while (_tn > 0) nb[_i++] = _t[--_tn]; } \
            nb[_i] = '\0'; \
            kout(label); kout(nb); kout("\n"); \
        } while (0)
        kout("[DIAG] USB controllers on PCI bus:\n");
        KOUT_NUM("  UHCI (0x00): ", n_uhci);
        KOUT_NUM("  OHCI (0x10): ", n_ohci);
        KOUT_NUM("  EHCI (0x20): ", n_ehci);
        KOUT_NUM("  xHCI (0x30, supported): ", n_xhci);
        KOUT_NUM("  other prog_if: ", n_other);
        #undef KOUT_NUM
    }

    DBG_MSG("KR", "23b usb_init...");
    usb_init();

     





















    {
        int mounted = 0;

         

        static const char *usb_devs[] = { "uda", "udb", "udc", "udd", NULL };
        for (int di = 0; usb_devs[di] != NULL && !mounted; di++) {
            blkdev_t *bd = blkdev_find(usb_devs[di]);
            if (!bd) continue;
            kout("[BOOT] USB disk "); kout(usb_devs[di]); kout("\n");
            DBG_MSG("KR", "24 mount boot volume from USB...");
            mounted = mount_boot_volume(bd);
        }

         
        if (!mounted && blkdev_find("vda")) {
            DBG_MSG("KR", "24 vfs_mount / (vda)...");
             



            blkdev_t *vd = blkdev_find("vda");
            if (mount_boot_volume(vd)) {
                mounted = 1;
            } else {
                ret = vfs_mount("vda", "fat32", "/", 0);
                if (ret == 0) {
                    kout("[OK] / mounted (FAT32, dev=vda)\n");
                    mounted = 1;
                } else {
                    kprint_err("[WARN] vfs_mount failed for vda", ret);
                }
            }
        }

        if (mounted) {
             
            int dfd = vfs_open("/", O_DIRECTORY);
            if (dfd >= 0) {
                vfs_dirent_t de;
                kout("[LS /]\n");
                for (uint32_t i = 0; vfs_readdir(dfd, i, &de) == VFS_OK; i++) {
                    kout("  "); kout(de.name);
                    kout(de.type == VFS_TYPE_DIR ? "/\n" : "\n");
                }
                vfs_close(dfd);
            }

             
            DBG_MSG("KR", "DIAG: before try_spawn_init");
            try_spawn_init();
            DBG_MSG("KR", "DIAG: after try_spawn_init");

        } else {
            kout("[WARN] no root filesystem mounted\n");
            DBG_MSG("KR", "no root fs - continuing without storage");

             


            const usb_stats_t *us = usb_get_stats();
            kout("--- USB diagnostics ---\n");
            kout_dec("  host controllers up : ", us->hcds);
            kout_dec("  devices enumerated  : ", us->devices);
            kout_dec("  hubs                : ", us->hubs);
            kout_dec("  HID (kbd/mouse)     : ", us->hid);
            kout_dec("  mass storage OK     : ", us->msc);
            kout_dec("  LS/FS behind HS hub (skipped): ", us->split_skipped);
            kout_dec("  enumeration failures: ", us->enum_fail);
            kout_dec("  first fail step (2=desc8 3=addr 4=desc18): ", us->fail_step);
            kout_dec("  first fail rc (2=err 3=timeout): ", (uint64_t)(us->fail_rc < 0 ? -us->fail_rc : us->fail_rc));
            for (int i = 0; i < us->n_list; i++) {
                kout("  dev "); kout_hex16(us->list[i].vid); kout(":"); kout_hex16(us->list[i].pid);
                kout(us->list[i].dev_class == 9 ? " hub" : "");
                kout(us->list[i].depth ? " (behind hub)\n" : "\n");
            }
            if (us->hcds == 0)
                kout("  -> no xHCI/EHCI/UHCI controller came up\n");
            else if (us->msc == 0)
                kout("  -> flash drive not seen as mass storage; try another USB 2.0 port\n");
            ehci_diag_dump(kout);
            kout("Continuing in 60 s...\n");
            usb_delay_ms(60000);
        }
    }

skip_fs:;

     

    if (fb_get_mode() == FB_MODE_LINEAR) {
        if (fb_enable_double_buffer() == 0)
            DBG_MSG("KR", "double-buffer enabled OK");
        else
            DBG_MSG("KR", "double-buffer failed, writing to front directly");
    }

     
    DBG_MSG("KR", "25 ui_draw_desktop...");
    ui_draw_desktop();
    DBG_MSG("KR", "25 ui ok");
    g_ui_desktop_shown = true;   
    boot_mark("stage 1: desktop drawn");

    DBG_MSG("KR", "25b ui_extra_init (cursor.png + tick clock)...");
    ui_extra_init();
    DBG_MSG("KR", "25b ui_extra ok");
    boot_mark("stage 2: ui_extra ok");

    DBG_MSG("KR", "25c ps2_mouse_init...");
    boot_mark("stage 3: ps2 init...");
    if (!ps2_controller_present()) {
        DBG_MSG("KR", "25c no 8042 (port 0x64 = 0xFF) - ps2_mouse_init skipped");
        boot_mark("stage 4: no PS/2 controller, skipped");
    } else if (ps2_mouse_init() != 0) {
        DBG_MSG("KR", "25c ps2 mouse not found (cursor will stay still)");
        boot_mark("stage 4: ps2 mouse not found");
    } else {
        DBG_MSG("KR", "25c ps2 mouse ok");
        boot_mark("stage 4: ps2 mouse ok");
    }

     
    DBG_MSG("KR", "18 sched_init...");
    boot_mark("stage 5: sched_init...");
    ret = sched_init();
    if (ret != 0) {
        kprint_err("[FATAL] sched_init", ret);
        DBG_PANIC("KR", "sched_init");
    }
    kout("[OK] scheduler\n");
    DBG_MSG("KR", "19 sched ok");
    boot_mark("stage 6: sched ok");

     






    DBG_MSG("KR", "net_init...");
    if (net_init() != 0) kout("[WARN] net_init failed (no netd thread)\n");

#if defined(FEXOX_OS_TEST_KTHREADS) && FEXOX_OS_TEST_KTHREADS
     



    DBG_MSG("KR", "DIAG: before worker_a create");
    sched_create_kthread(worker_a, NULL, "worker_a",  0);
    DBG_MSG("KR", "DIAG: after worker_a create, before worker_b");
    sched_create_kthread(worker_b, NULL, "worker_b", -5);
    DBG_MSG("KR", "DIAG: after worker_b create");
    kout("[OK] kthreads\n");
    boot_mark("stage 7: kthreads created");
#else
    boot_mark("stage 7: test kthreads disabled");
#endif

     
    DBG_MSG("KR", "DIAG: before sti");
    boot_mark("stage 8: sti...");
    __asm__ volatile ("sti" ::: "memory");
    DBG_MSG("KR", "DIAG: after sti");
    kout(">>> Fexox OS running <<<\n");
    DBG_MSG("KR", "99 idle loop");
    boot_mark("stage 9: idle loop (running)");

     
    while (1)
        __asm__ volatile ("hlt");
}

#ifdef __cplusplus
}
#endif