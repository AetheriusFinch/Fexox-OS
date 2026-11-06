/* Fexox OS Project, EULA GPLv2 */

#include "VFS.h"
#include "debug_out.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define AHCI_USE_NCQ         1
#define AHCI_USE_MSI         1
#define AHCI_USE_INTX        0
#define AHCI_ENABLE_WCACHE   1

#define AHCI_QD_MAX          8
#define AHCI_BOUNCE_SIZE     65536u
#define AHCI_CMD_TIMEOUT_MS  10000u
#define AHCI_FLUSH_TIMEOUT_MS 30000u
#define AHCI_MAX_RETRIES     3
#define AHCI_SCAN_PERIOD_MS  1000u

#define AHCI_VEC_BASE        0x50
#define AHCI_VEC_HI          0x5F
#define AHCI_VEC_LO          0x58

extern void *kmalloc(uint64_t size);
extern void  kfree(void *ptr);

typedef uint64_t phys_addr_t;
extern phys_addr_t pmm_alloc_frames_above(uint64_t count, uint64_t align,
                                           phys_addr_t min_phys);
extern void        pmm_free_frames(phys_addr_t pa, uint64_t count);

extern uint64_t interrupt_get_ticks(void);
extern uint32_t interrupt_get_timer_hz(void);
extern uint32_t interrupt_get_apic_id(void);

typedef void (*ahci_isr_fn_t)(uint64_t num, uint64_t err, void *frame);
extern int  interrupt_register_handler(uint16_t vector, ahci_isr_fn_t h);
extern int  interrupt_unregister_handler(uint16_t vector);
extern void ioapic_route_irq(uint8_t irq, uint8_t vector, uint32_t dest_lapic_id,
                             bool level, bool low_pol);

#ifndef DIRECT_MAP_OFFSET
#define DIRECT_MAP_OFFSET 0xFFFF880000000000ULL
#endif
#define DMA_MIN_PHYS 0x100000ULL

static inline void ahci_memzero(void *p, uint64_t n) {
    __asm__ volatile("rep stosb" : "+D"(p), "+c"(n) : "a"(0) : "memory");
}
static inline void ahci_memcpy(void *d, const void *s, uint64_t n) {
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) :: "memory");
}
static inline int ahci_streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a == (unsigned char)*b;
}
static inline void ahci_strcpy(char *d, const char *s, int max) {
    int i = 0; while (i < max - 1 && s[i]) { d[i] = s[i]; i++; } d[i] = 0;
}
static inline uint32_t ahci_ctz(uint32_t v) { return (uint32_t)__builtin_ctz(v); }

static void *ahci_kzalloc(uint64_t size) {
    void *p = kmalloc(size);
    if (p) ahci_memzero(p, size);
    return p;
}

static inline uint64_t virt_to_phys(const void *v) {
    uint64_t a = (uint64_t)(uintptr_t)v;
    if (a >= DIRECT_MAP_OFFSET)     return a - DIRECT_MAP_OFFSET;
    if (a >= 0xFFFFFFFF80000000ULL) return a - 0xFFFFFFFF80000000ULL + 0x200000ULL;
    return a;
}

static void *dma_alloc(uint64_t size) {
    uint64_t pages = (size + 4095) / 4096;
    phys_addr_t pa = pmm_alloc_frames_above(pages, 1, DMA_MIN_PHYS);
    if (pa == (phys_addr_t)-1) return NULL;
    void *va = (void *)(uintptr_t)(pa + DIRECT_MAP_OFFSET);
    ahci_memzero(va, pages * 4096);
    return va;
}
static void dma_free(void *va, uint64_t size) {
    if (!va) return;
    phys_addr_t pa = (phys_addr_t)(uintptr_t)va - DIRECT_MAP_OFFSET;
    pmm_free_frames(pa, (size + 4095) / 4096);
}

typedef struct { volatile uint32_t v; } ahci_lock_t;

static inline bool cpu_if_enabled(void) {
    uint64_t f;
    __asm__ volatile("pushfq; pop %0" : "=r"(f) :: "memory");
    return (f & 0x200) != 0;
}
static inline uint64_t lock_irqsave(ahci_lock_t *l) {
    uint64_t f;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(f) :: "memory");
    while (__atomic_exchange_n(&l->v, 1, __ATOMIC_ACQUIRE))
        __asm__ volatile("pause" ::: "memory");
    return f;
}
static inline void unlock_irqrestore(ahci_lock_t *l, uint64_t f) {
    __atomic_store_n(&l->v, 0, __ATOMIC_RELEASE);
    if (f & 0x200) __asm__ volatile("sti" ::: "memory");
}

static inline uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    uint32_t a = 0x80000000U | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                 ((uint32_t)fn << 8) | (reg & 0xFC);
    __asm__ volatile("outl %0,%1" :: "a"(a), "Nd"((uint16_t)0xCF8));
    uint32_t v; __asm__ volatile("inl %1,%0" : "=a"(v) : "Nd"((uint16_t)0xCFC));
    return v;
}
static inline void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg, uint32_t v) {
    uint32_t a = 0x80000000U | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                 ((uint32_t)fn << 8) | (reg & 0xFC);
    __asm__ volatile("outl %0,%1" :: "a"(a), "Nd"((uint16_t)0xCF8));
    __asm__ volatile("outl %0,%1" :: "a"(v), "Nd"((uint16_t)0xCFC));
}

static inline uint32_t mmio_r32(uint64_t addr) {
    __asm__ volatile("mfence" ::: "memory");
    return *(volatile uint32_t *)(uintptr_t)(addr + DIRECT_MAP_OFFSET);
}
static inline void mmio_w32(uint64_t addr, uint32_t v) {
    *(volatile uint32_t *)(uintptr_t)(addr + DIRECT_MAP_OFFSET) = v;
    __asm__ volatile("mfence" ::: "memory");
}

#define AHCI_CAP        0x000
#define AHCI_GHC        0x004
#define AHCI_IS         0x008
#define AHCI_PI         0x00C
#define AHCI_VS         0x010
#define AHCI_CAP2       0x024
#define AHCI_BOHC       0x028

#define GHC_AE          (1u << 31)
#define GHC_IE          (1u << 1)
#define GHC_HR          (1u << 0)

#define CAP_S64A        (1u << 31)
#define CAP_SNCQ        (1u << 30)
#define CAP_SCLO        (1u << 24)
#define CAP_NCS(c)      ((((c) >> 8) & 0x1F) + 1)
#define CAP_NP(c)       (((c) & 0x1F) + 1)
#define CAP2_BOH        (1u << 0)

#define BOHC_BOS        (1u << 0)
#define BOHC_OOS        (1u << 1)
#define BOHC_BB         (1u << 4)

#define PORT_BASE(p)    (0x100u + (uint32_t)(p) * 0x80u)
#define POFF_CLB        0x00
#define POFF_CLBU       0x04
#define POFF_FB         0x08
#define POFF_FBU        0x0C
#define POFF_IS         0x10
#define POFF_IE         0x14
#define POFF_CMD        0x18
#define POFF_TFD        0x20
#define POFF_SIG        0x24
#define POFF_SSTS       0x28
#define POFF_SCTL       0x2C
#define POFF_SERR       0x30
#define POFF_SACT       0x34
#define POFF_CI         0x38

#define PCMD_ST         (1u <<  0)
#define PCMD_SUD        (1u <<  1)
#define PCMD_POD        (1u <<  2)
#define PCMD_CLO        (1u <<  3)
#define PCMD_FRE        (1u <<  4)
#define PCMD_FR         (1u << 14)
#define PCMD_CR         (1u << 15)
#define PCMD_ICC_MASK   (0xFu << 28)
#define PCMD_ICC_ACTIVE (1u << 28)

#define PIS_DHRS        (1u <<  0)
#define PIS_PSS         (1u <<  1)
#define PIS_SDBS        (1u <<  3)
#define PIS_PCS         (1u <<  6)
#define PIS_PRCS        (1u << 22)
#define PIS_OFS         (1u << 24)
#define PIS_INFS        (1u << 26)
#define PIS_IFS         (1u << 27)
#define PIS_HBDS        (1u << 28)
#define PIS_HBFS        (1u << 29)
#define PIS_TFES        (1u << 30)
#define PIS_CPDS        (1u << 31)
#define PIS_FATAL       (PIS_TFES | PIS_HBFS | PIS_HBDS | PIS_IFS | PIS_OFS)
#define PIS_HOTPLUG     (PIS_PCS | PIS_PRCS | PIS_CPDS)
#define PORT_IE_MASK    (PIS_DHRS | PIS_PSS | PIS_SDBS | PIS_FATAL | PIS_INFS | PIS_HOTPLUG)
#define PORT_IE_EMPTY   (PIS_HOTPLUG)

#define TFD_BSY         0x80
#define TFD_DF          0x20
#define TFD_DRQ         0x08
#define TFD_ERR         0x01

#define SSTS_DET(s)     ((s) & 0xF)
#define SSTS_IPM(s)     (((s) >> 8) & 0xF)
#define SCTL_IPM_NOPM   (3u << 8)

#define SIG_ATA         0x00000101u

#define FIS_TYPE_REG_H2D 0x27

#define ATA_CMD_READ_DMA_EXT     0x25
#define ATA_CMD_WRITE_DMA_EXT    0x35
#define ATA_CMD_READ_DMA         0xC8
#define ATA_CMD_WRITE_DMA        0xCA
#define ATA_CMD_READ_FPDMA       0x60
#define ATA_CMD_WRITE_FPDMA      0x61
#define ATA_CMD_IDENTIFY         0xEC
#define ATA_CMD_FLUSH            0xE7
#define ATA_CMD_FLUSH_EXT        0xEA
#define ATA_CMD_STANDBY_IMM      0xE0
#define ATA_CMD_SET_FEATURES     0xEF
#define ATA_CMD_DSM              0x06
#define ATA_FEAT_WCACHE_ON       0x02
#define ATA_FEAT_LOOKAHEAD_ON    0xAA

typedef struct __attribute__((packed)) {
    uint8_t fis_type;
    uint8_t pmport_c;
    uint8_t command;
    uint8_t featurel;
    uint8_t lba0, lba1, lba2, device;
    uint8_t lba3, lba4, lba5, featureh;
    uint8_t countl, counth, icc, control;
    uint8_t rsv[4];
} fis_reg_h2d_t;

typedef struct __attribute__((packed)) {
    uint32_t dba;
    uint32_t dbau;
    uint32_t rsv;
    uint32_t dbc_i;
} ahci_prdt_entry_t;

typedef struct __attribute__((packed)) {
    uint8_t           cfis[64];
    uint8_t           acmd[16];
    uint8_t           rsv[48];
    ahci_prdt_entry_t prdt[1];
} ahci_cmd_tbl_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t prdtl;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t rsv[4];
} ahci_cmd_hdr_t;

#define CMDHDR_W    (1u << 6)

#define PORT_CTRL_CLB_OFF   0x000
#define PORT_CTRL_FB_OFF    0x400
#define PORT_CTRL_CTBL_OFF  0x500
#define PORT_CTBL_STRIDE    256

#define AHCI_MAX_CONTROLLERS 4
#define AHCI_MAX_DRIVES      (AHCI_MAX_CONTROLLERS * 32)

#define AHCI_WAIT_OK       0
#define AHCI_WAIT_ERROR    1
#define AHCI_WAIT_TIMEOUT  2

typedef struct ahci_port ahci_port_t;

typedef struct ahci_hba {
    uint64_t abar;
    uint32_t cap, cap2;
    uint32_t ports_impl;
    uint32_t num_slots;
    bool     s64a, ncq, sclo;

    uint8_t  bus, dev, fn;
    int      irq_mode;
    uint8_t  vector;
    volatile uint32_t hotplug_pending;
    uint64_t next_scan_ms;

    ahci_port_t *port[32];
} ahci_hba_t;

typedef struct {
    uint8_t  *bounce;
    uint64_t  bounce_phys;
} ahci_slot_t;

typedef struct {
    uint64_t lba;
    uint32_t nsec;
    uint8_t *user;
} ahci_job_t;

typedef struct {
    uint8_t  cmd, feat_lo, feat_hi, device;
    uint16_t count;
    uint64_t lba;
    uint32_t bytes;
    bool     write;
} ahci_ata_cmd_t;

struct ahci_port {
    ahci_hba_t *hba;
    uint32_t    port_num;

    uint8_t        *ctrl;
    uint64_t        ctrl_phys;
    ahci_cmd_hdr_t *clb;
    uint8_t        *ctbl_base;
    ahci_slot_t     slot[AHCI_QD_MAX];
    uint32_t        slots_alloc;

    volatile bool online;
    bool     registered;
    bool     ncq, lba48, trim, flush_ext, flush_std;
    bool     wcache_sup, lookahead_sup, ssd;
    uint32_t qd;
    uint32_t sector_size;
    uint64_t sectors;
    char     model[41], serial[21], fw[9];

    ahci_lock_t lock;
    volatile uint32_t issued, issued_q, done_mask, err_flags, err_tfd, err_serr;
    volatile uint32_t io_busy;
    bool     in_recover;
    uint64_t next_revive_ms;

    uint64_t st_reads, st_writes, st_sectors_r, st_sectors_w;
    uint32_t st_errors, st_timeouts, st_resets, st_retries;

    blkdev_t blkdev;
};

static ahci_hba_t   g_hbas[AHCI_MAX_CONTROLLERS];
static int          g_hba_count = 0;
static ahci_port_t *g_drives[AHCI_MAX_DRIVES];
static int          g_drive_count = 0;
static ahci_hba_t  *g_vec_hba[16];
static uint8_t      g_next_vec = AHCI_VEC_HI;
static bool         g_inited = false;

static inline uint32_t port_r(ahci_hba_t *h, uint32_t p, uint32_t off) {
    return mmio_r32(h->abar + PORT_BASE(p) + off);
}
static inline void port_w(ahci_hba_t *h, uint32_t p, uint32_t off, uint32_t v) {
    mmio_w32(h->abar + PORT_BASE(p) + off, v);
}
static inline uint32_t hba_r(ahci_hba_t *h, uint32_t off) { return mmio_r32(h->abar + off); }
static inline void     hba_w(ahci_hba_t *h, uint32_t off, uint32_t v) { mmio_w32(h->abar + off, v); }

typedef struct {
    bool     use_ticks;
    uint64_t end_ticks;
    uint64_t iters;
} deadline_t;

static void deadline_start(deadline_t *d, uint32_t ms) {
    uint32_t hz = interrupt_get_timer_hz();
    d->use_ticks = (hz != 0) && cpu_if_enabled();
    if (d->use_ticks) {
        d->end_ticks = interrupt_get_ticks() + ((uint64_t)ms * hz + 999) / 1000 + 1;
        d->iters = 0;
    } else {
        d->end_ticks = 0;
        d->iters = (uint64_t)ms * 1000;
    }
}
static bool deadline_expired(deadline_t *d) {
    if (d->use_ticks) return interrupt_get_ticks() >= d->end_ticks;
    if (d->iters == 0) return true;
    d->iters--;
    return false;
}

static uint64_t ahci_now_ms(void) {
    uint32_t hz = interrupt_get_timer_hz();
    if (!hz) return 0;
    return interrupt_get_ticks() * 1000ULL / hz;
}

static void ahci_delay_ms(uint32_t ms) {
    deadline_t d; deadline_start(&d, ms);
    if (d.use_ticks) {
        while (!deadline_expired(&d)) __asm__ volatile("hlt" ::: "memory");
    } else {
        for (uint32_t i = 0; i < ms; i++)
            for (int j = 0; j < 1000; j++)
                __asm__ volatile("outb %%al, $0x80" :: "a"((uint8_t)0));
    }
}

static bool port_wait(ahci_hba_t *h, uint32_t p, uint32_t off,
                      uint32_t mask, uint32_t val, uint32_t ms) {
    deadline_t d; deadline_start(&d, ms);
    for (;;) {
        if ((port_r(h, p, off) & mask) == val) return true;
        if (deadline_expired(&d)) return (port_r(h, p, off) & mask) == val;
        __asm__ volatile("pause" ::: "memory");
    }
}
static bool hba_wait(ahci_hba_t *h, uint32_t off, uint32_t mask, uint32_t val, uint32_t ms) {
    deadline_t d; deadline_start(&d, ms);
    for (;;) {
        if ((hba_r(h, off) & mask) == val) return true;
        if (deadline_expired(&d)) return (hba_r(h, off) & mask) == val;
        __asm__ volatile("pause" ::: "memory");
    }
}

static bool ahci_port_cmd_stop(ahci_hba_t *h, uint32_t p) {
    uint32_t cmd = port_r(h, p, POFF_CMD);
    bool ok = true;

    if (cmd & (PCMD_ST | PCMD_CR)) {
        port_w(h, p, POFF_CMD, cmd & ~PCMD_ST);
        if (!port_wait(h, p, POFF_CMD, PCMD_CR, 0, 500)) {
            DBG_VAL("AHCI", "stop: CR не сбросился, порт", p);
            ok = false;
        }
    }
    cmd = port_r(h, p, POFF_CMD);
    if (cmd & (PCMD_FRE | PCMD_FR)) {
        port_w(h, p, POFF_CMD, cmd & ~PCMD_FRE);
        if (!port_wait(h, p, POFF_CMD, PCMD_FR, 0, 500)) {
            DBG_VAL("AHCI", "stop: FR не сбросился, порт", p);
            ok = false;
        }
    }
    return ok;
}

static void ahci_port_cmd_start(ahci_hba_t *h, uint32_t p) {
    port_wait(h, p, POFF_CMD, PCMD_CR, 0, 500);
    uint32_t cmd = port_r(h, p, POFF_CMD);
    port_w(h, p, POFF_CMD, (cmd & ~PCMD_ICC_MASK) | PCMD_FRE);
    cmd = port_r(h, p, POFF_CMD);
    port_w(h, p, POFF_CMD, (cmd & ~PCMD_ICC_MASK) | PCMD_FRE | PCMD_ST);
}

static bool ahci_port_link_ok(ahci_hba_t *h, uint32_t p) {
    uint32_t s = port_r(h, p, POFF_SSTS);
    return SSTS_DET(s) == 3 && SSTS_IPM(s) == 1;
}

static bool ahci_port_comreset(ahci_hba_t *h, uint32_t p) {
    port_w(h, p, POFF_SCTL, SCTL_IPM_NOPM | 1);
    ahci_delay_ms(2);
    port_w(h, p, POFF_SCTL, SCTL_IPM_NOPM);
    port_wait(h, p, POFF_SSTS, 0xF, 3, 1000);
    port_w(h, p, POFF_SERR, 0xFFFFFFFFu);

    uint32_t s = port_r(h, p, POFF_SSTS);
    if (SSTS_DET(s) == 3 && SSTS_IPM(s) != 1) {
        uint32_t cmd = port_r(h, p, POFF_CMD);
        port_w(h, p, POFF_CMD, (cmd & ~PCMD_ICC_MASK) | PCMD_ICC_ACTIVE);
        port_wait(h, p, POFF_CMD, PCMD_ICC_MASK, 0, 50);
        port_wait(h, p, POFF_SSTS, 0xF00, 0x100, 100);
    }
    return ahci_port_link_ok(h, p);
}

static bool ahci_port_wait_ready(ahci_hba_t *h, uint32_t p, uint32_t ms) {
    return port_wait(h, p, POFF_TFD, TFD_BSY | TFD_DRQ, 0, ms);
}

static inline ahci_cmd_tbl_t *slot_tbl(ahci_port_t *ap, uint32_t slot) {
    return (ahci_cmd_tbl_t *)(ap->ctbl_base + (uint64_t)slot * PORT_CTBL_STRIDE);
}

static void ahci_build_cmd(ahci_port_t *ap, uint32_t slot, const ahci_ata_cmd_t *c) {
    ahci_cmd_tbl_t *t = slot_tbl(ap, slot);
    fis_reg_h2d_t *f = (fis_reg_h2d_t *)t->cfis;
    ahci_memzero(t->cfis, 64);

    f->fis_type = FIS_TYPE_REG_H2D;
    f->pmport_c = 0x80;
    f->command  = c->cmd;
    f->featurel = c->feat_lo;
    f->featureh = c->feat_hi;
    f->device   = c->device;
    f->lba0 = (uint8_t)(c->lba);
    f->lba1 = (uint8_t)(c->lba >> 8);
    f->lba2 = (uint8_t)(c->lba >> 16);
    f->lba3 = (uint8_t)(c->lba >> 24);
    f->lba4 = (uint8_t)(c->lba >> 32);
    f->lba5 = (uint8_t)(c->lba >> 40);
    f->countl = (uint8_t)(c->count);
    f->counth = (uint8_t)(c->count >> 8);

    if (c->bytes) {
        uint64_t pa = ap->slot[slot].bounce_phys;
        t->prdt[0].dba   = (uint32_t)pa;
        t->prdt[0].dbau  = (uint32_t)(pa >> 32);
        t->prdt[0].rsv   = 0;
        t->prdt[0].dbc_i = c->bytes - 1;
    }
    ap->clb[slot].flags = (uint16_t)((sizeof(fis_reg_h2d_t) / 4) | (c->write ? CMDHDR_W : 0));
    ap->clb[slot].prdtl = c->bytes ? 1 : 0;
    ap->clb[slot].prdbc = 0;
    __asm__ volatile("mfence" ::: "memory");
}

static void ahci_issue(ahci_port_t *ap, uint32_t slot, bool ncq_cmd) {
    ahci_hba_t *h = ap->hba;
    uint32_t bit = 1u << slot;
    uint64_t f = lock_irqsave(&ap->lock);
    ap->issued |= bit;
    if (ncq_cmd) {
        ap->issued_q |= bit;
        port_w(h, ap->port_num, POFF_SACT, bit);
    }
    port_w(h, ap->port_num, POFF_CI, bit);
    unlock_irqrestore(&ap->lock, f);
}

static void ahci_port_service(ahci_port_t *ap) {
    ahci_hba_t *h = ap->hba;
    uint32_t p = ap->port_num;
    uint64_t f = lock_irqsave(&ap->lock);

    uint32_t is = port_r(h, p, POFF_IS);
    if (is) {
        port_w(h, p, POFF_IS, is);
        hba_w(h, AHCI_IS, 1u << p);
    }
    if (is & PIS_FATAL) {
        ap->err_flags |= (is & PIS_FATAL);
        ap->err_tfd    = port_r(h, p, POFF_TFD);
        ap->err_serr   = port_r(h, p, POFF_SERR);
    }
    if (is & PIS_HOTPLUG) h->hotplug_pending = 1;

    if (ap->issued) {
        uint32_t done = 0;
        uint32_t q = ap->issued_q;
        if (q)             done |= q & ~port_r(h, p, POFF_SACT);
        uint32_t nq = ap->issued & ~q;
        if (nq)            done |= nq & ~port_r(h, p, POFF_CI);
        done &= ap->issued;
        if (done) {
            ap->issued   &= ~done;
            ap->issued_q &= ~done;
            ap->done_mask |= done;
        }
    }
    unlock_irqrestore(&ap->lock, f);
}

static void ahci_hba_irq(ahci_hba_t *h) {
    uint32_t is = hba_r(h, AHCI_IS) & h->ports_impl;
    while (is) {
        uint32_t p = ahci_ctz(is);
        is &= is - 1;
        ahci_port_t *ap = h->port[p];
        if (ap) {
            ahci_port_service(ap);
        } else {
            port_w(h, p, POFF_IS, 0xFFFFFFFFu);
            hba_w(h, AHCI_IS, 1u << p);
            h->hotplug_pending = 1;
        }
    }
}

static void ahci_msi_isr(uint64_t num, uint64_t err, void *frame) {
    (void)err; (void)frame;
    uint64_t idx = num - AHCI_VEC_BASE;
    if (idx < 16 && g_vec_hba[idx]) ahci_hba_irq(g_vec_hba[idx]);
}

static uint32_t ahci_wait_any(ahci_port_t *ap, uint32_t mask, uint32_t timeout_ms, int *status) {
    bool irqs = cpu_if_enabled();
    deadline_t dl; deadline_start(&dl, timeout_ms);
    uint32_t done = 0;
    int st = AHCI_WAIT_TIMEOUT;

    for (;;) {
        if (irqs) __asm__ volatile("cli" ::: "memory");
        ahci_port_service(ap);

        uint64_t f = lock_irqsave(&ap->lock);
        done = ap->done_mask & mask;
        ap->done_mask &= ~done;
        uint32_t err = ap->err_flags;
        unlock_irqrestore(&ap->lock, f);

        if (err)               { st = AHCI_WAIT_ERROR;   break; }
        if (done)              { st = AHCI_WAIT_OK;      break; }
        if (deadline_expired(&dl)) { st = AHCI_WAIT_TIMEOUT; break; }

        if (irqs) __asm__ volatile("sti; hlt" ::: "memory");
        else      __asm__ volatile("pause" ::: "memory");
    }
    if (irqs) __asm__ volatile("sti" ::: "memory");
    *status = st;
    return done;
}

static void ahci_io_acquire(ahci_port_t *ap) {
    while (__atomic_exchange_n(&ap->io_busy, 1, __ATOMIC_ACQUIRE)) {
        if (cpu_if_enabled()) __asm__ volatile("hlt" ::: "memory");
        else                  __asm__ volatile("pause" ::: "memory");
    }
}
static bool ahci_io_try_acquire(ahci_port_t *ap) {
    return !__atomic_exchange_n(&ap->io_busy, 1, __ATOMIC_ACQUIRE);
}
static void ahci_io_release(ahci_port_t *ap) {
    __atomic_store_n(&ap->io_busy, 0, __ATOMIC_RELEASE);
}

static int ahci_exec_raw(ahci_port_t *ap, const ahci_ata_cmd_t *c, uint32_t timeout_ms) {
    ahci_build_cmd(ap, 0, c);
    ahci_issue(ap, 0, false);

    int st;
    uint32_t done = ahci_wait_any(ap, 1, timeout_ms, &st);
    if (st == AHCI_WAIT_OK && (done & 1)) {
        uint32_t tfd = port_r(ap->hba, ap->port_num, POFF_TFD);
        if (tfd & (TFD_ERR | TFD_DF)) {
            DBG_VAL("AHCI", "команда завершилась с ошибкой, TFD", tfd);
            return VFS_ERR_IO;
        }
        return VFS_OK;
    }
    if (st == AHCI_WAIT_TIMEOUT) ap->st_timeouts++;
    return VFS_ERR_IO;
}

static void ahci_apply_features(ahci_port_t *ap) {
#if AHCI_ENABLE_WCACHE
    ahci_ata_cmd_t c;
    if (ap->wcache_sup) {
        ahci_memzero(&c, sizeof(c));
        c.cmd = ATA_CMD_SET_FEATURES; c.feat_lo = ATA_FEAT_WCACHE_ON;
        if (ahci_exec_raw(ap, &c, 5000) != VFS_OK) return;
    }
    if (ap->lookahead_sup) {
        ahci_memzero(&c, sizeof(c));
        c.cmd = ATA_CMD_SET_FEATURES; c.feat_lo = ATA_FEAT_LOOKAHEAD_ON;
        ahci_exec_raw(ap, &c, 5000);
    }
#else
    (void)ap;
#endif
}

static bool ahci_port_recover(ahci_port_t *ap) {
    ahci_hba_t *h = ap->hba;
    uint32_t p = ap->port_num;

    if (ap->in_recover) return false;
    ap->in_recover = true;
    ap->st_resets++;
    ap->st_errors++;

    DBG_VAL("AHCI", "recover: порт", p);
    DBG_VAL("AHCI", "  err_flags", ap->err_flags);
    DBG_VAL("AHCI", "  err_tfd",   ap->err_tfd);
    DBG_VAL("AHCI", "  err_serr",  ap->err_serr);

    ahci_port_cmd_stop(h, p);

    uint64_t f = lock_irqsave(&ap->lock);
    ap->issued = ap->issued_q = ap->done_mask = ap->err_flags = 0;
    unlock_irqrestore(&ap->lock, f);

    port_w(h, p, POFF_SERR, 0xFFFFFFFFu);
    port_w(h, p, POFF_IS,   0xFFFFFFFFu);
    hba_w(h, AHCI_IS, 1u << p);

    bool need_reset = ap->ncq;
    uint32_t tfd = port_r(h, p, POFF_TFD);
    if (!need_reset && (tfd & (TFD_BSY | TFD_DRQ))) {
        if (h->sclo) {
            port_w(h, p, POFF_CMD, port_r(h, p, POFF_CMD) | PCMD_CLO);
            port_wait(h, p, POFF_CMD, PCMD_CLO, 0, 500);
            tfd = port_r(h, p, POFF_TFD);
        }
        if (tfd & (TFD_BSY | TFD_DRQ)) need_reset = true;
    }
    if (!ahci_port_link_ok(h, p)) need_reset = true;

    bool alive = true;
    if (need_reset) alive = ahci_port_comreset(h, p);

    if (alive && !ahci_port_wait_ready(h, p, 5000)) {
        alive = ahci_port_comreset(h, p) && ahci_port_wait_ready(h, p, 5000);
    }

    if (!alive) {
        DBG_VAL("AHCI", "recover: порт не ожил", p);
        ap->online = false;
        ap->in_recover = false;
        return false;
    }

    port_w(h, p, POFF_SERR, 0xFFFFFFFFu);
    port_w(h, p, POFF_IS,   0xFFFFFFFFu);
    hba_w(h, AHCI_IS, 1u << p);
    ahci_port_cmd_start(h, p);
    ahci_apply_features(ap);
    h->hotplug_pending = 0;
    ap->in_recover = false;
    return true;
}

static int ahci_exec(ahci_port_t *ap, const ahci_ata_cmd_t *c, uint32_t timeout_ms) {
    for (int attempt = 0; attempt < 2; attempt++) {
        if (ahci_exec_raw(ap, c, timeout_ms) == VFS_OK) return VFS_OK;
        if (!ahci_port_recover(ap)) return VFS_ERR_IO;
    }
    return VFS_ERR_IO;
}

static int ahci_flush_locked(ahci_port_t *ap) {
    if (!ap->flush_ext && !ap->flush_std) return VFS_OK;
    ahci_ata_cmd_t c;
    ahci_memzero(&c, sizeof(c));
    c.cmd = ap->flush_ext ? ATA_CMD_FLUSH_EXT : ATA_CMD_FLUSH;
    return ahci_exec(ap, &c, AHCI_FLUSH_TIMEOUT_MS);
}

static void ahci_submit_rw(ahci_port_t *ap, uint32_t slot, uint64_t lba,
                           uint32_t n, bool write) {
    ahci_ata_cmd_t c;
    ahci_memzero(&c, sizeof(c));
    c.bytes = n * ap->sector_size;
    c.write = write;

    if (ap->ncq) {
        c.cmd     = write ? ATA_CMD_WRITE_FPDMA : ATA_CMD_READ_FPDMA;
        c.lba     = lba;
        c.feat_lo = (uint8_t)n;
        c.feat_hi = (uint8_t)(n >> 8);
        c.count   = (uint16_t)(slot << 3);
        c.device  = 0x40;
    } else if (ap->lba48) {
        c.cmd    = write ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT;
        c.lba    = lba;
        c.count  = (uint16_t)n;
        c.device = 0x40;
    } else {
        c.cmd    = write ? ATA_CMD_WRITE_DMA : ATA_CMD_READ_DMA;
        c.lba    = lba & 0xFFFFFFu;
        c.count  = (uint16_t)n;
        c.device = (uint8_t)(0x40 | ((lba >> 24) & 0x0F));
    }
    ahci_build_cmd(ap, slot, &c);
    ahci_issue(ap, slot, ap->ncq);
}

static int ahci_port_ensure_online(ahci_port_t *ap);

static int ahci_rw_request(ahci_port_t *ap, uint64_t lba, uint32_t count,
                           uint8_t *buf, bool write) {
    const uint32_t ss = ap->sector_size;
    uint32_t max_chunk = AHCI_BOUNCE_SIZE / ss;
    if (!ap->lba48 && max_chunk > 128) max_chunk = 128;

    uint32_t depth = ap->qd ? ap->qd : 1;
    if (depth > ap->slots_alloc) depth = ap->slots_alloc;
    if (!ap->ncq) depth = 1;

    ahci_job_t job[AHCI_QD_MAX];
    uint64_t next = lba;
    uint32_t remain = count;
    uint8_t *cur = buf;
    uint32_t inflight = 0;
    int retries = 0;
    int result = VFS_OK;

    while (remain || inflight) {

        while (remain && (uint32_t)__builtin_popcount(inflight) < depth) {
            uint32_t freebits = ~inflight & ((1u << depth) - 1u);
            uint32_t slot = ahci_ctz(freebits);
            uint32_t n = remain > max_chunk ? max_chunk : remain;

            job[slot].lba = next; job[slot].nsec = n; job[slot].user = cur;
            if (write) ahci_memcpy(ap->slot[slot].bounce, cur, (uint64_t)n * ss);

            ahci_submit_rw(ap, slot, next, n, write);
            inflight |= 1u << slot;
            next += n; cur += (uint64_t)n * ss; remain -= n;
        }

        int st;
        uint32_t done = ahci_wait_any(ap, inflight, AHCI_CMD_TIMEOUT_MS, &st);
        while (done) {
            uint32_t slot = ahci_ctz(done);
            done &= done - 1;
            if (!write) {
                __asm__ volatile("mfence" ::: "memory");
                ahci_memcpy(job[slot].user, ap->slot[slot].bounce,
                            (uint64_t)job[slot].nsec * ss);
            }
            inflight &= ~(1u << slot);
        }

        if (st != AHCI_WAIT_OK) {
            if (st == AHCI_WAIT_TIMEOUT) ap->st_timeouts++;
            retries++;
            ap->st_retries++;

            bool alive = ahci_port_recover(ap);
            if (!alive || retries > AHCI_MAX_RETRIES) {
                result = alive ? VFS_ERR_IO : VFS_ERR_NXDEV;
                break;
            }
            if (inflight) {
                uint64_t minlba = ~0ULL;
                for (uint32_t s = 0; s < depth; s++)
                    if ((inflight & (1u << s)) && job[s].lba < minlba) minlba = job[s].lba;
                next   = minlba;
                cur    = buf + (minlba - lba) * ss;
                remain = (uint32_t)(count - (minlba - lba));
                inflight = 0;
            }
            depth = 1;
            if (ap->ncq) {

            }
        }
    }

    if (result == VFS_OK) {
        if (write) { ap->st_writes++; ap->st_sectors_w += count; }
        else       { ap->st_reads++;  ap->st_sectors_r += count; }
        if (write) result = ahci_flush_locked(ap);
    }
    return result;
}

static int ahci_port_io(ahci_port_t *ap, uint64_t lba, uint32_t count,
                        void *buf, bool write) {
    if (count == 0) return VFS_OK;
    if (!buf) return VFS_ERR_INVAL;
    if (lba >= ap->sectors || count > ap->sectors - lba) return VFS_ERR_OVERFLOW;

    ahci_io_acquire(ap);
    int r = ahci_port_ensure_online(ap);
    if (r == VFS_OK) r = ahci_rw_request(ap, lba, count, (uint8_t *)buf, write);
    ahci_io_release(ap);
    return r;
}

static int ahci_blkdev_read(blkdev_t *dev, uint64_t lba, uint32_t count, void *buf) {
    return ahci_port_io((ahci_port_t *)dev->priv, lba, count, buf, false);
}
static int ahci_blkdev_write(blkdev_t *dev, uint64_t lba, uint32_t count, const void *buf) {
    if (dev->readonly) return VFS_ERR_ROFS;
    return ahci_port_io((ahci_port_t *)dev->priv, lba, count, (void *)buf, true);
}

int ahci_flush(blkdev_t *dev);

static void ahci_blkdev_destroy(blkdev_t *dev) { if (dev) ahci_flush(dev); }

static blkdev_ops_t g_ahci_ops = {
    .read_sectors  = ahci_blkdev_read,
    .write_sectors = ahci_blkdev_write,
    .destroy       = ahci_blkdev_destroy,
};

int ahci_flush(blkdev_t *dev) {
    if (!dev || dev->ops != &g_ahci_ops) return VFS_ERR_INVAL;
    ahci_port_t *ap = (ahci_port_t *)dev->priv;
    ahci_io_acquire(ap);
    int r = ahci_port_ensure_online(ap);
    if (r == VFS_OK) r = ahci_flush_locked(ap);
    ahci_io_release(ap);
    return r;
}

int ahci_trim(blkdev_t *dev, uint64_t lba, uint64_t count) {
    if (!dev || dev->ops != &g_ahci_ops) return VFS_ERR_INVAL;
    ahci_port_t *ap = (ahci_port_t *)dev->priv;
    if (!ap->trim || !ap->lba48) return VFS_ERR_UNSUP;
    if (dev->readonly) return VFS_ERR_ROFS;
    if (lba >= ap->sectors || count > ap->sectors - lba) return VFS_ERR_OVERFLOW;

    ahci_io_acquire(ap);
    int r = ahci_port_ensure_online(ap);
    while (r == VFS_OK && count) {
        uint64_t *pl = (uint64_t *)ap->slot[0].bounce;
        ahci_memzero(pl, 512);
        int nr = 0;
        while (count && nr < 64) {
            uint64_t len = count > 0xFFFF ? 0xFFFF : count;
            pl[nr++] = (lba & 0xFFFFFFFFFFFFULL) | (len << 48);
            lba += len; count -= len;
        }
        ahci_ata_cmd_t c;
        ahci_memzero(&c, sizeof(c));
        c.cmd = ATA_CMD_DSM; c.feat_lo = 1; c.count = 1;
        c.device = 0x40; c.bytes = 512; c.write = true;
        r = ahci_exec(ap, &c, AHCI_CMD_TIMEOUT_MS);
    }
    ahci_io_release(ap);
    return r;
}

static void ahci_copy_string(char *dst, const uint16_t *w, int start, int nwords) {
    int j = 0;
    for (int i = 0; i < nwords; i++) {
        char a = (char)(w[start + i] >> 8), b = (char)(w[start + i] & 0xFF);
        dst[j++] = (a >= 32 && a < 127) ? a : ' ';
        dst[j++] = (b >= 32 && b < 127) ? b : ' ';
    }
    int last = j - 1;
    while (last >= 0 && dst[last] == ' ') last--;
    dst[last + 1] = '\0';
}

static bool ahci_parse_identify(ahci_port_t *ap, const uint16_t *id) {
    if (id[0] == 0 || id[0] == 0xFFFF) return false;
    if (id[0] & 0x8000) return false;

    ap->lba48 = (id[83] & (1u << 10)) != 0;
    uint64_t s28 = (uint64_t)id[60] | ((uint64_t)id[61] << 16);
    uint64_t s48 = (uint64_t)id[100] | ((uint64_t)id[101] << 16) |
                   ((uint64_t)id[102] << 32) | ((uint64_t)id[103] << 48);
    ap->sectors = (ap->lba48 && s48) ? s48 : s28;
    if (ap->sectors == 0) return false;

    uint32_t ss = 512;
    if ((id[106] & 0xC000) == 0x4000 && (id[106] & (1u << 12))) {
        uint32_t words = (uint32_t)id[117] | ((uint32_t)id[118] << 16);
        if (words) ss = words * 2;
    }
    if (ss != 512 && ss != 4096) {
        DBG_VAL("AHCI", "неподдерживаемый размер сектора", ss);
        return false;
    }
    if (ss == 4096) DBG_MSG("AHCI", "диск с логическим сектором 4096 (VFS/FAT32 ожидают 512?)");
    ap->sector_size = ss;

    ap->flush_ext     = (id[83] & (1u << 13)) != 0;
    ap->flush_std     = (id[83] & (1u << 12)) != 0;
    ap->wcache_sup    = (id[82] & (1u << 5)) != 0;
    ap->lookahead_sup = (id[82] & (1u << 6)) != 0;
    ap->trim          = (id[169] & 1u) != 0;
    ap->ssd           = (id[217] == 1);

    ap->ncq = false;
    ap->qd  = 1;
#if AHCI_USE_NCQ
    if (ap->hba->ncq && id[76] != 0 && id[76] != 0xFFFF && (id[76] & (1u << 8))) {
        uint32_t qd = (uint32_t)(id[75] & 0x1F) + 1;
        if (qd > ap->hba->num_slots) qd = ap->hba->num_slots;
        if (qd > AHCI_QD_MAX)        qd = AHCI_QD_MAX;
        if (qd > ap->slots_alloc)    qd = ap->slots_alloc;
        if (qd >= 1) { ap->ncq = true; ap->qd = qd; }
    }
#endif
    ahci_copy_string(ap->model,  id, 27, 20);
    ahci_copy_string(ap->serial, id, 10, 10);
    ahci_copy_string(ap->fw,     id, 23, 4);
    return true;
}

static bool ahci_identify(ahci_port_t *ap, uint16_t *out256) {
    ahci_ata_cmd_t c;
    ahci_memzero(&c, sizeof(c));
    c.cmd = ATA_CMD_IDENTIFY; c.bytes = 512;
    ahci_memzero(ap->slot[0].bounce, 512);
    if (ahci_exec_raw(ap, &c, 5000) != VFS_OK) return false;
    __asm__ volatile("mfence" ::: "memory");
    const uint16_t *src = (const uint16_t *)ap->slot[0].bounce;
    for (int i = 0; i < 256; i++) out256[i] = src[i];
    return true;
}

static void ahci_make_name(char *out, int idx) {
    out[0] = 's'; out[1] = 'd';
    if (idx < 26) { out[2] = (char)('a' + idx); out[3] = 0; }
    else { out[2] = (char)('a' + idx / 26 - 1); out[3] = (char)('a' + idx % 26); out[4] = 0; }
}

static void ahci_port_free(ahci_port_t *ap) {
    if (!ap) return;
    for (uint32_t i = 0; i < AHCI_QD_MAX; i++)
        if (ap->slot[i].bounce) dma_free(ap->slot[i].bounce, AHCI_BOUNCE_SIZE);
    if (ap->ctrl) dma_free(ap->ctrl, 4096);
    kfree(ap);
}

static bool ahci_port_alloc(ahci_hba_t *h, ahci_port_t *ap) {
    ap->ctrl = (uint8_t *)dma_alloc(4096);
    if (!ap->ctrl) return false;
    ap->ctrl_phys = virt_to_phys(ap->ctrl);
    if (!h->s64a && ap->ctrl_phys > 0xFFFFFFFFULL) return false;

    ap->clb       = (ahci_cmd_hdr_t *)(ap->ctrl + PORT_CTRL_CLB_OFF);
    ap->ctbl_base = ap->ctrl + PORT_CTRL_CTBL_OFF;

    uint32_t want = AHCI_QD_MAX;
    if (want > h->num_slots) want = h->num_slots;
    ap->slots_alloc = 0;
    for (uint32_t i = 0; i < want; i++) {
        uint8_t *b = (uint8_t *)dma_alloc(AHCI_BOUNCE_SIZE);
        if (!b) break;
        uint64_t pa = virt_to_phys(b);
        if (!h->s64a && pa + AHCI_BOUNCE_SIZE > 0x100000000ULL) {
            dma_free(b, AHCI_BOUNCE_SIZE);
            break;
        }
        ap->slot[i].bounce = b;
        ap->slot[i].bounce_phys = pa;
        ap->slots_alloc++;
    }
    if (ap->slots_alloc == 0) return false;

    for (uint32_t i = 0; i < ap->slots_alloc; i++) {
        uint64_t tp = ap->ctrl_phys + PORT_CTRL_CTBL_OFF + (uint64_t)i * PORT_CTBL_STRIDE;
        ap->clb[i].ctba  = (uint32_t)tp;
        ap->clb[i].ctbau = (uint32_t)(tp >> 32);
    }
    return true;
}

static void ahci_port_program(ahci_hba_t *h, ahci_port_t *ap) {
    uint32_t p = ap->port_num;
    uint64_t clb = ap->ctrl_phys + PORT_CTRL_CLB_OFF;
    uint64_t fb  = ap->ctrl_phys + PORT_CTRL_FB_OFF;
    port_w(h, p, POFF_CLB,  (uint32_t)clb);
    port_w(h, p, POFF_CLBU, (uint32_t)(clb >> 32));
    port_w(h, p, POFF_FB,   (uint32_t)fb);
    port_w(h, p, POFF_FBU,  (uint32_t)(fb >> 32));
}

static bool ahci_port_detect(ahci_hba_t *h, uint32_t p) {
    uint32_t cmd = port_r(h, p, POFF_CMD);
    port_w(h, p, POFF_CMD, cmd | PCMD_POD | PCMD_SUD);
    port_w(h, p, POFF_SCTL, SCTL_IPM_NOPM);

    uint32_t s = port_r(h, p, POFF_SSTS);
    uint32_t det = SSTS_DET(s);
    if (det == 0) return false;

    if (det == 1) {
        if (!ahci_port_comreset(h, p)) return false;
    } else if (det == 3 && SSTS_IPM(s) != 1) {
        cmd = port_r(h, p, POFF_CMD);
        port_w(h, p, POFF_CMD, (cmd & ~PCMD_ICC_MASK) | PCMD_ICC_ACTIVE);
        port_wait(h, p, POFF_CMD, PCMD_ICC_MASK, 0, 50);
        port_wait(h, p, POFF_SSTS, 0xF00, 0x100, 200);
    } else if (det != 3) {
        return false;
    }
    return ahci_port_link_ok(h, p);
}

static void ahci_probe_port(ahci_hba_t *h, uint32_t p) {
    ahci_port_t *ap = h->port[p];
    if (ap && ap->online) return;

    ahci_port_cmd_stop(h, p);
    port_w(h, p, POFF_IE, 0);
    port_w(h, p, POFF_SERR, 0xFFFFFFFFu);
    port_w(h, p, POFF_IS,   0xFFFFFFFFu);
    hba_w(h, AHCI_IS, 1u << p);

    if (!ahci_port_detect(h, p)) {
        if (ap) ap->online = false;
        return;
    }

    if (!ahci_port_wait_ready(h, p, 3000)) {
        ahci_port_comreset(h, p);
        if (!ahci_port_wait_ready(h, p, 3000)) {
            DBG_VAL("AHCI", "порт не готов (BSY/DRQ), порт", p);
            return;
        }
    }

    uint32_t sig = port_r(h, p, POFF_SIG);
    if (sig != SIG_ATA) {
        DBG_VAL("AHCI", "не ATA-устройство (ATAPI/PM/SEMB не поддерживаются), sig", sig);
        return;
    }

    bool fresh = (ap == NULL);
    if (fresh) {
        if (g_drive_count >= AHCI_MAX_DRIVES) { DBG_MSG("AHCI", "слишком много дисков"); return; }
        ap = (ahci_port_t *)ahci_kzalloc(sizeof(ahci_port_t));
        if (!ap) { DBG_MSG("AHCI", "нет памяти под порт"); return; }
        ap->hba = h; ap->port_num = p;
        if (!ahci_port_alloc(h, ap)) {
            DBG_MSG("AHCI", "DMA-память под порт недоступна");
            ahci_port_free(ap);
            return;
        }
    }

    ahci_port_program(h, ap);
    ap->issued = ap->issued_q = ap->done_mask = ap->err_flags = 0;
    ahci_port_cmd_start(h, p);

    uint16_t id[256];
    bool ok = false;
    for (int attempt = 0; attempt < 3 && !ok; attempt++) {
        ok = ahci_identify(ap, id);
        if (!ok && !ahci_port_recover(ap)) break;
    }
    if (!ok) {
        DBG_VAL("AHCI", "IDENTIFY не удался, порт", p);
        ahci_port_cmd_stop(h, p);
        if (fresh) ahci_port_free(ap);
        return;
    }

    char old_serial[21], old_model[41];
    uint64_t old_sectors = ap->sectors;
    ahci_strcpy(old_serial, ap->serial, 21);
    ahci_strcpy(old_model,  ap->model, 41);

    if (!ahci_parse_identify(ap, id)) {
        DBG_VAL("AHCI", "IDENTIFY: диск не подходит, порт", p);
        ahci_port_cmd_stop(h, p);
        if (fresh) ahci_port_free(ap);
        return;
    }
    ahci_apply_features(ap);

    ap->online = true;
    h->port[p] = ap;

    if (fresh) {
        char name[8];
        ahci_make_name(name, g_drive_count);
        ahci_strcpy(ap->blkdev.name, name, 32);
        ap->blkdev.sector_size  = ap->sector_size;
        ap->blkdev.sector_count = ap->sectors;
        ap->blkdev.priv         = ap;
        ap->blkdev.ops          = &g_ahci_ops;
        ap->blkdev.readonly     = false;

        int r = blkdev_register(&ap->blkdev);
        if (r < 0) {
            DBG_VAL("AHCI", "blkdev_register failed", (uint64_t)(int64_t)r);
            h->port[p] = NULL;
            ap->online = false;
            ahci_port_cmd_stop(h, p);
            ahci_port_free(ap);
            return;
        }
        ap->registered = true;
        g_drives[g_drive_count++] = ap;
    } else {
        if (!ahci_streq(old_serial, ap->serial) || old_sectors != ap->sectors)
            DBG_MSG("AHCI", "на порту ДРУГОЙ диск (старое имя blkdev сохранено)");
        ap->blkdev.sector_size  = ap->sector_size;
        ap->blkdev.sector_count = ap->sectors;
        (void)old_model;
    }

    dbg_puts("[AHCI] порт "); dbg_dec64(p);
    dbg_puts(" -> "); dbg_puts(ap->blkdev.name);
    dbg_puts(" \""); dbg_puts(ap->model); dbg_puts("\" fw=");
    dbg_puts(ap->fw); dbg_puts(" sn="); dbg_puts(ap->serial); dbg_puts("\n");
    DBG_VAL("AHCI", "  sectors", ap->sectors);
    DBG_VAL("AHCI", "  sector_size", ap->sector_size);
    DBG_VAL("AHCI", "  lba48", ap->lba48);
    DBG_VAL("AHCI", "  ncq depth", ap->ncq ? ap->qd : 0);
    DBG_VAL("AHCI", "  trim", ap->trim);

    if (h->irq_mode) {
        port_w(h, p, POFF_IS, 0xFFFFFFFFu);
        port_w(h, p, POFF_IE, PORT_IE_MASK);
    }
}

static int ahci_port_ensure_online(ahci_port_t *ap) {
    if (ap->online) return VFS_OK;
    uint64_t now = ahci_now_ms();
    if (now < ap->next_revive_ms) return VFS_ERR_NXDEV;
    ap->next_revive_ms = now + 1000;
    ahci_probe_port(ap->hba, ap->port_num);
    return ap->online ? VFS_OK : VFS_ERR_NXDEV;
}

static uint8_t ahci_pci_find_cap(ahci_hba_t *h, uint8_t id) {
    uint32_t st = pci_read32(h->bus, h->dev, h->fn, 0x04) >> 16;
    if (!(st & 0x10)) return 0;
    uint8_t ptr = (uint8_t)(pci_read32(h->bus, h->dev, h->fn, 0x34) & 0xFC);
    for (int i = 0; i < 48 && ptr >= 0x40; i++) {
        uint32_t dw = pci_read32(h->bus, h->dev, h->fn, ptr);
        if ((dw & 0xFF) == id) return ptr;
        ptr = (uint8_t)((dw >> 8) & 0xFC);
    }
    return 0;
}

static void ahci_pci_intx_disable(ahci_hba_t *h, bool disable) {
    uint32_t c = pci_read32(h->bus, h->dev, h->fn, 0x04) & 0xFFFF;
    if (disable) c |= 0x0400; else c &= ~0x0400u;
    pci_write32(h->bus, h->dev, h->fn, 0x04, c);
}

static bool ahci_setup_msi(ahci_hba_t *h) {
#if AHCI_USE_MSI
    uint8_t cap = ahci_pci_find_cap(h, 0x05);
    if (!cap || g_next_vec < AHCI_VEC_LO) return false;

    uint32_t dw0 = pci_read32(h->bus, h->dev, h->fn, cap);
    uint16_t ctrl = (uint16_t)(dw0 >> 16);
    bool is64 = (ctrl & 0x0080) != 0;
    bool pvm  = (ctrl & 0x0100) != 0;

    uint8_t vec = g_next_vec--;
    uint32_t addr = 0xFEE00000u | ((interrupt_get_apic_id() & 0xFF) << 12);

    g_vec_hba[vec - AHCI_VEC_BASE] = h;
    if (interrupt_register_handler(vec, ahci_msi_isr) != 0) {
        g_vec_hba[vec - AHCI_VEC_BASE] = NULL;
        return false;
    }

    pci_write32(h->bus, h->dev, h->fn, cap + 4, addr);
    if (is64) {
        pci_write32(h->bus, h->dev, h->fn, cap + 8,  0);
        pci_write32(h->bus, h->dev, h->fn, cap + 12, vec);
        if (pvm) pci_write32(h->bus, h->dev, h->fn, cap + 16, 0);
    } else {
        pci_write32(h->bus, h->dev, h->fn, cap + 8,  vec);
        if (pvm) pci_write32(h->bus, h->dev, h->fn, cap + 12, 0);
    }
    ctrl = (uint16_t)((ctrl & ~0x0070) | 0x0001);
    pci_write32(h->bus, h->dev, h->fn, cap, (dw0 & 0xFFFF) | ((uint32_t)ctrl << 16));
    ahci_pci_intx_disable(h, true);

    h->vector = vec;
    h->irq_mode = 1;
    DBG_VAL("AHCI", "MSI включён, вектор", vec);
    return true;
#else
    (void)h; return false;
#endif
}

static bool ahci_setup_intx(ahci_hba_t *h) {
#if AHCI_USE_INTX
    uint32_t line = pci_read32(h->bus, h->dev, h->fn, 0x3C) & 0xFF;
    if (line == 0 || line >= 24 || g_next_vec < AHCI_VEC_LO) return false;
    uint8_t vec = g_next_vec--;
    g_vec_hba[vec - AHCI_VEC_BASE] = h;
    if (interrupt_register_handler(vec, ahci_msi_isr) != 0) return false;
    ioapic_route_irq((uint8_t)line, vec, interrupt_get_apic_id(), true, true);
    h->vector = vec;
    h->irq_mode = 2;
    DBG_VAL("AHCI", "INTx через IOAPIC, линия", line);
    return true;
#else
    (void)h; return false;
#endif
}

static void ahci_bios_handoff(ahci_hba_t *h) {
    if (!(h->cap2 & CAP2_BOH)) return;
    uint32_t b = hba_r(h, AHCI_BOHC);
    if (!(b & BOHC_BOS)) return;
    DBG_MSG("AHCI", "BIOS handoff");
    hba_w(h, AHCI_BOHC, b | BOHC_OOS);
    hba_wait(h, AHCI_BOHC, BOHC_BOS, 0, 25);
    if (hba_r(h, AHCI_BOHC) & BOHC_BB)
        hba_wait(h, AHCI_BOHC, BOHC_BB, 0, 2000);
}

static bool ahci_hba_reset(ahci_hba_t *h) {
    uint32_t ghc = hba_r(h, AHCI_GHC);
    if (!(ghc & GHC_AE)) { hba_w(h, AHCI_GHC, ghc | GHC_AE); ghc = hba_r(h, AHCI_GHC); }
    hba_w(h, AHCI_GHC, ghc | GHC_HR);
    if (!hba_wait(h, AHCI_GHC, GHC_HR, 0, 1000)) {
        DBG_MSG("AHCI", "HBA reset timeout");
        return false;
    }
    hba_w(h, AHCI_GHC, hba_r(h, AHCI_GHC) | GHC_AE);
    return true;
}

static void ahci_wait_phy(ahci_hba_t *h) {
    deadline_t total; deadline_start(&total, 800);
    uint64_t first_up = 0;
    for (;;) {
        uint32_t up = 0, impl = 0;
        for (uint32_t p = 0; p < 32; p++) {
            if (!(h->ports_impl & (1u << p))) continue;
            impl++;
            if (SSTS_DET(port_r(h, p, POFF_SSTS)) == 3) up++;
        }
        if (impl == up) return;
        if (up) {
            if (!first_up) first_up = ahci_now_ms() + 1;
            else if (ahci_now_ms() >= first_up + 150) return;
        }
        if (deadline_expired(&total)) return;
        ahci_delay_ms(10);
    }
}

static void ahci_hba_enable_irq(ahci_hba_t *h) {
    if (!h->irq_mode) return;
    hba_w(h, AHCI_IS, 0xFFFFFFFFu);
    for (uint32_t p = 0; p < 32; p++) {
        if (!(h->ports_impl & (1u << p))) continue;
        port_w(h, p, POFF_IS, 0xFFFFFFFFu);
        port_w(h, p, POFF_IE, h->port[p] && h->port[p]->online ? PORT_IE_MASK : PORT_IE_EMPTY);
    }
    hba_w(h, AHCI_IS, 0xFFFFFFFFu);
    hba_w(h, AHCI_GHC, hba_r(h, AHCI_GHC) | GHC_IE);
}

static void ahci_init_hba(uint8_t bus, uint8_t dev, uint8_t fn, uint64_t abar_phys) {
    if (g_hba_count >= AHCI_MAX_CONTROLLERS) return;
    ahci_hba_t *h = &g_hbas[g_hba_count++];
    ahci_memzero(h, sizeof(*h));
    h->abar = abar_phys; h->bus = bus; h->dev = dev; h->fn = fn;

    h->cap  = hba_r(h, AHCI_CAP);
    h->cap2 = hba_r(h, AHCI_CAP2);
    ahci_bios_handoff(h);
    if (!ahci_hba_reset(h)) return;

    hba_w(h, AHCI_GHC, hba_r(h, AHCI_GHC) & ~GHC_IE);
    hba_w(h, AHCI_IS, 0xFFFFFFFFu);

    h->cap         = hba_r(h, AHCI_CAP);
    h->cap2        = hba_r(h, AHCI_CAP2);
    h->s64a        = (h->cap & CAP_S64A) != 0;
    h->ncq         = (h->cap & CAP_SNCQ) != 0;
    h->sclo        = (h->cap & CAP_SCLO) != 0;
    h->num_slots   = CAP_NCS(h->cap);
    h->ports_impl  = hba_r(h, AHCI_PI);
    if (h->ports_impl == 0) {
        uint32_t np = CAP_NP(h->cap);
        h->ports_impl = np >= 32 ? 0xFFFFFFFFu : ((1u << np) - 1u);
        DBG_MSG("AHCI", "PI=0, берём порты из CAP.NP");
    }

    DBG_VAL("AHCI", "HBA version",  hba_r(h, AHCI_VS));
    DBG_VAL("AHCI", "CAP",          h->cap);
    DBG_VAL("AHCI", "ports_impl",   h->ports_impl);
    DBG_VAL("AHCI", "num_slots",    h->num_slots);
    DBG_VAL("AHCI", "s64a",         h->s64a);
    DBG_VAL("AHCI", "ncq",          h->ncq);

    if (!ahci_setup_msi(h) && !ahci_setup_intx(h))
        DBG_MSG("AHCI", "режим без IRQ: ожидание по тикам таймера + опрос");

    ahci_wait_phy(h);
    for (uint32_t p = 0; p < 32; p++)
        if (h->ports_impl & (1u << p)) ahci_probe_port(h, p);

    ahci_hba_enable_irq(h);
}

int ahci_init(void) {
    DBG_MSG("AHCI", "=== ahci_init ===");
    if (g_inited) return g_drive_count ? VFS_OK : VFS_ERR_NXDEV;
    g_inited = true;

    int found = 0;
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            for (uint8_t fn = 0; fn < 8; fn++) {
                uint32_t id = pci_read32((uint8_t)bus, dev, fn, 0x00);
                if (id == 0xFFFFFFFFu) { if (fn == 0) break; continue; }

                uint32_t cls = pci_read32((uint8_t)bus, dev, fn, 0x08);
                uint8_t cc = (uint8_t)(cls >> 24), sc = (uint8_t)(cls >> 16), pi = (uint8_t)(cls >> 8);

                if (cc == 0x01 && sc == 0x06 && pi == 0x01) {
                    DBG_VAL("AHCI", "найден AHCI HBA, bus", bus);
                    DBG_VAL("AHCI", "  device", dev);
                    DBG_VAL("AHCI", "  vendor:device", id);

                    uint32_t pcmd = pci_read32((uint8_t)bus, dev, fn, 0x04) & 0xFFFF;
                    pci_write32((uint8_t)bus, dev, fn, 0x04, pcmd | 0x06);

                    uint32_t bar5 = pci_read32((uint8_t)bus, dev, fn, 0x24);
                    if (bar5 & 1) {
                        DBG_MSG("AHCI", "BAR5 — IO-пространство, пропуск");
                    } else {
                        uint64_t abar = bar5 & ~0xFULL;
                        if (((bar5 >> 1) & 3) == 2)
                            abar |= (uint64_t)pci_read32((uint8_t)bus, dev, fn, 0x28) << 32;
                        DBG_VAL("AHCI", "  ABAR phys", abar);
                        if (abar) { ahci_init_hba((uint8_t)bus, dev, fn, abar); found++; }
                    }
                }
                if (fn == 0) {
                    uint8_t hdr = (uint8_t)(pci_read32((uint8_t)bus, dev, 0, 0x0C) >> 16);
                    if (!(hdr & 0x80)) break;
                }
            }
        }
    }

    if (!found)           { DBG_MSG("AHCI", "AHCI-контроллер не найден"); return VFS_ERR_NXDEV; }
    DBG_VAL("AHCI", "всего AHCI-дисков", g_drive_count);
    if (!g_drive_count)   { DBG_MSG("AHCI", "на портах нет ATA-дисков"); return VFS_ERR_NXDEV; }
    DBG_MSG("AHCI", "=== ahci_init done ===");
    return VFS_OK;
}

static void ahci_scan_hba(ahci_hba_t *h) {
    for (uint32_t p = 0; p < 32; p++) {
        if (!(h->ports_impl & (1u << p))) continue;
        ahci_port_t *ap = h->port[p];
        uint32_t s = port_r(h, p, POFF_SSTS);

        if (ap && ap->online) {
            if (SSTS_DET(s) != 3) {
                DBG_VAL("AHCI", "диск отключён, порт", p);
                ap->online = false;
            }
            continue;
        }
        if (SSTS_DET(s) == 0) continue;

        if (ap) {
            if (!ahci_io_try_acquire(ap)) continue;
            ap->next_revive_ms = 0;
            ahci_probe_port(h, p);
            ahci_io_release(ap);
        } else {
            ahci_probe_port(h, p);
        }
        if (h->port[p] && h->port[p]->online && h->irq_mode)
            port_w(h, p, POFF_IE, PORT_IE_MASK);
    }
}

void ahci_poll(void) {
    uint64_t now = ahci_now_ms();
    for (int i = 0; i < g_hba_count; i++) {
        ahci_hba_t *h = &g_hbas[i];
        if (h->hotplug_pending || now >= h->next_scan_ms) {
            h->hotplug_pending = 0;
            h->next_scan_ms = now + AHCI_SCAN_PERIOD_MS;
            ahci_scan_hba(h);
        }
    }
}

int ahci_rescan(void) {
    for (int i = 0; i < g_hba_count; i++) ahci_scan_hba(&g_hbas[i]);
    return g_drive_count;
}

void ahci_shutdown(bool power_off) {
    for (int i = 0; i < g_drive_count; i++) {
        ahci_port_t *ap = g_drives[i];
        if (!ap || !ap->online) continue;
        ahci_io_acquire(ap);
        if (ap->online) {
            ahci_flush_locked(ap);
            if (power_off) {
                ahci_ata_cmd_t c;
                ahci_memzero(&c, sizeof(c));
                c.cmd = ATA_CMD_STANDBY_IMM;
                ahci_exec_raw(ap, &c, 10000);
            }
        }

    }
    for (int i = 0; i < g_hba_count; i++) {
        ahci_hba_t *h = &g_hbas[i];
        hba_w(h, AHCI_GHC, hba_r(h, AHCI_GHC) & ~GHC_IE);
        for (uint32_t p = 0; p < 32; p++) {
            if (!(h->ports_impl & (1u << p))) continue;
            port_w(h, p, POFF_IE, 0);
            ahci_port_cmd_stop(h, p);
        }
    }
}

blkdev_t *ahci_find_drive(const char *name) {
    for (int i = 0; i < g_drive_count; i++)
        if (ahci_streq(g_drives[i]->blkdev.name, name)) return &g_drives[i]->blkdev;
    return NULL;
}

int ahci_drive_count(void) { return g_drive_count; }

blkdev_t *ahci_drive_at(int idx) {
    if (idx < 0 || idx >= g_drive_count) return NULL;
    return &g_drives[idx]->blkdev;
}

void ahci_dump_info(void) {
    for (int i = 0; i < g_drive_count; i++) {
        ahci_port_t *ap = g_drives[i];
        dbg_puts("[AHCI] "); dbg_puts(ap->blkdev.name); dbg_puts(" \""); dbg_puts(ap->model);
        dbg_puts(ap->online ? "\" online" : "\" OFFLINE");
        dbg_puts(ap->ncq ? " NCQ" : " noNCQ");
        dbg_puts(ap->ssd ? " SSD\n" : "\n");
        DBG_VAL("AHCI", "  reads",    ap->st_reads);
        DBG_VAL("AHCI", "  writes",   ap->st_writes);
        DBG_VAL("AHCI", "  errors",   ap->st_errors);
        DBG_VAL("AHCI", "  timeouts", ap->st_timeouts);
        DBG_VAL("AHCI", "  resets",   ap->st_resets);
        DBG_VAL("AHCI", "  retries",  ap->st_retries);
    }
}
