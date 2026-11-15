/* Fexox OS Project, EULA GPLv2 */
 


#include "VFS.h"
#include "debug_out.h"
#include "UsbDiag.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

 


extern void *kmalloc(uint64_t size);
extern void  kfree(void *ptr);

 
#ifndef DIRECT_MAP_OFFSET
#define DIRECT_MAP_OFFSET  0xFFFF880000000000ULL
#endif

 







typedef uint64_t phys_addr_t;
extern phys_addr_t pmm_alloc_frames(uint64_t count, uint64_t align);
extern void        pmm_free_frames(phys_addr_t pa, uint64_t count);
 



extern phys_addr_t pmm_alloc_frames_above(uint64_t count, uint64_t align,
                                           phys_addr_t min_phys);

 





#define DMA_MIN_PHYS  0x100000ULL

static void *dma_alloc(uint64_t size) {
    uint64_t pages = (size + 4095) / 4096;
    phys_addr_t pa = pmm_alloc_frames_above(pages, 1, DMA_MIN_PHYS);
    if (pa == (phys_addr_t)-1) return NULL;
     
    void *va = (void *)(uintptr_t)(pa + DIRECT_MAP_OFFSET);
     
    uint8_t *p = (uint8_t *)va;
    for (uint64_t i = 0; i < pages * 4096; i++) p[i] = 0;
    return va;
}

static void dma_free(void *va, uint64_t size) {
    if (!va) return;
    uint64_t pages = (size + 4095) / 4096;
    phys_addr_t pa = (phys_addr_t)(uintptr_t)va - DIRECT_MAP_OFFSET;
    pmm_free_frames(pa, pages);
}

static inline void *kzalloc(uint64_t size) {
    void *p = kmalloc(size);
    if (p) {
        uint8_t *b = (uint8_t *)p;
        for (uint64_t i = 0; i < size; i++) b[i] = 0;
    }
    return p;
}

static inline void kmem_set(void *dst, uint8_t v, uint64_t n) {
    uint8_t *p = (uint8_t *)dst;
    while (n--) *p++ = v;
}

static inline __attribute__((unused)) void kmem_cpy(void *dst, const void *src, uint64_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

static inline int kstr_len(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static inline int kstr_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static inline int kstr_ncmp(const char *a, const char *b, int n) {
    while (n-- && *a && *a == *b) { a++; b++; }
    if (n < 0) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

static inline void kstr_cpy(char *dst, const char *src, int max) {
    int i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

 
 









typedef volatile uint64_t spinlock_t;
static inline void spin_lock(spinlock_t *l) {
    uint64_t flags;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    *l = flags;
}
static inline void spin_unlock(spinlock_t *l) {
    uint64_t flags = *l;
    __asm__ volatile ("push %0; popfq" :: "r"(flags) : "memory", "cc");
}

 


static inline uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    uint32_t addr = 0x80000000U
                  | ((uint32_t)bus  << 16)
                  | ((uint32_t)dev  << 11)
                  | ((uint32_t)fn   <<  8)
                  | (reg & 0xFC);
    __asm__ volatile ("outl %0, %1" :: "a"(addr), "Nd"((uint16_t)0xCF8));
    uint32_t val;
    __asm__ volatile ("inl %1, %0"  : "=a"(val) : "Nd"((uint16_t)0xCFC));
    return val;
}

static inline void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn,
                                uint8_t reg, uint32_t val) {
    uint32_t addr = 0x80000000U
                  | ((uint32_t)bus  << 16)
                  | ((uint32_t)dev  << 11)
                  | ((uint32_t)fn   <<  8)
                  | (reg & 0xFC);
    __asm__ volatile ("outl %0, %1" :: "a"(addr), "Nd"((uint16_t)0xCF8));
    __asm__ volatile ("outl %0, %1" :: "a"(val),  "Nd"((uint16_t)0xCFC));
}

static inline uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    return (uint16_t)(pci_read32(bus, dev, fn, reg & ~2) >> ((reg & 2) * 8));
}

static inline __attribute__((unused)) uint8_t pci_read8(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    return (uint8_t)(pci_read32(bus, dev, fn, reg & ~3) >> ((reg & 3) * 8));
}

 


 

static inline __attribute__((unused)) volatile uint32_t *mmio32(uint64_t phys) {
    return (volatile uint32_t *)(uintptr_t)(phys + DIRECT_MAP_OFFSET);
}

static inline __attribute__((unused)) void mmio_write32(uint64_t phys, uint32_t val) {
    volatile uint32_t *p = mmio32(phys);
    *p = val;
    __asm__ volatile ("mfence" ::: "memory");
}

static inline __attribute__((unused)) uint32_t mmio_read32(uint64_t phys) {
    __asm__ volatile ("mfence" ::: "memory");
    return *mmio32(phys);
}

 
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0,%1" :: "a"(val),"Nd"(port));
}
static inline __attribute__((unused)) uint8_t inb(uint16_t port) {
    uint8_t v; __asm__ volatile ("inb %1,%0" : "=a"(v) : "Nd"(port)); return v;
}
static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0,%1" :: "a"(val),"Nd"(port));
}
static inline uint32_t inl(uint16_t port) {
    uint32_t v; __asm__ volatile ("inl %1,%0" : "=a"(v) : "Nd"(port)); return v;
}
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0,%1" :: "a"(val),"Nd"(port));
}
static inline uint16_t inw(uint16_t port) {
    uint16_t v; __asm__ volatile ("inw %1,%0" : "=a"(v) : "Nd"(port)); return v;
}

 
static inline uint64_t virt_to_phys(const void *v) {
    uint64_t a = (uint64_t)(uintptr_t)v;
     
    if (a >= DIRECT_MAP_OFFSET)
        return a - DIRECT_MAP_OFFSET;
     
    if (a >= 0xFFFFFFFF80000000ULL)
        return a - 0xFFFFFFFF80000000ULL + 0x200000ULL;
    return a;
}

 


static blkdev_t *g_blkdevs[VFS_MAX_BLKDEVS];
static int        g_blkdev_count = 0;
static spinlock_t g_blk_lock = 0;

int blkdev_register(blkdev_t *dev) {
    spin_lock(&g_blk_lock);
    if (g_blkdev_count >= VFS_MAX_BLKDEVS) {
        spin_unlock(&g_blk_lock);
        return VFS_ERR_BUSY;
    }
    int idx = g_blkdev_count++;
    g_blkdevs[idx] = dev;
    spin_unlock(&g_blk_lock);
    DBG_VAL("BLK", "registered device", (uint64_t)(uintptr_t)dev);
    dbg_puts("[BLK] device name: "); dbg_puts(dev->name); dbg_puts("\n");
    DBG_VAL("BLK", "sector_size", (uint64_t)dev->sector_size);
    DBG_VAL("BLK", "sector_count", dev->sector_count);
    return idx;
}

int blkdev_flush(blkdev_t *dev) {
    if (!dev || !dev->ops || !dev->ops->flush) return VFS_OK;
    return dev->ops->flush(dev);
}

int blkdev_count(void) { return g_blkdev_count; }
blkdev_t *blkdev_at(int idx) { return (idx >= 0 && idx < g_blkdev_count) ? g_blkdevs[idx] : NULL; }

blkdev_t *blkdev_find(const char *name) {
    for (int i = 0; i < g_blkdev_count; i++) {
        if (g_blkdevs[i] && kstr_cmp(g_blkdevs[i]->name, name) == 0)
            return g_blkdevs[i];
    }
    return NULL;
}

 













int blkdev_read(blkdev_t *dev, uint64_t lba, uint32_t count, void *buf) {
    if (!dev || !dev->ops || !dev->ops->read_sectors)
        return VFS_ERR_INVAL;
    spin_lock(&g_blk_lock);
    int rc = dev->ops->read_sectors(dev, lba, count, buf);
    spin_unlock(&g_blk_lock);
    return rc;
}

int blkdev_write(blkdev_t *dev, uint64_t lba, uint32_t count, const void *buf) {
    if (!dev || !dev->ops || !dev->ops->write_sectors)
        return VFS_ERR_INVAL;
    if (dev->readonly)
        return VFS_ERR_ROFS;
    spin_lock(&g_blk_lock);
    int rc = dev->ops->write_sectors(dev, lba, count, buf);
    spin_unlock(&g_blk_lock);
    return rc;
}

 









 


#define VIRTIO_VENDOR_ID        0x1AF4
#define VIRTIO_BLK_DEV_ID       0x1001    
#define VIRTIO_BLK_DEV_ID2      0x1042    

 
#define VREG_HOST_FEATURES      0x00
#define VREG_GUEST_FEATURES     0x04
#define VREG_QUEUE_PFN          0x08
#define VREG_QUEUE_SIZE         0x0C
#define VREG_QUEUE_SEL          0x0E
#define VREG_QUEUE_NOTIFY       0x10
#define VREG_STATUS             0x12
#define VREG_ISR                0x13
#define VREG_CONFIG             0x14      

 
#define VSTAT_ACK               0x01
#define VSTAT_DRIVER            0x02
#define VSTAT_DRIVER_OK         0x04
#define VSTAT_FEATURES_OK       0x08
#define VSTAT_FAILED            0x80

 
#define VBLK_F_RO               (1u << 5)
#define VBLK_F_BLK_SIZE         (1u << 6)

 
#define VBLK_T_IN               0
#define VBLK_T_OUT              1

 
#define VBLK_S_OK               0
#define VBLK_S_IOERR            1
#define VBLK_S_UNSUPP           2

 
#define VDESC_F_NEXT            0x01
#define VDESC_F_WRITE           0x02     

 







#define VQ_SIZE         256    
#define SECTOR_SIZE     512

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} vq_desc_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VQ_SIZE];
    uint16_t used_event;
} vq_avail_t;

typedef struct __attribute__((packed)) {
    uint32_t id;
    uint32_t len;
} vq_used_elem_t;

typedef struct __attribute__((packed)) {
    uint16_t       flags;
    uint16_t       idx;
    vq_used_elem_t ring[VQ_SIZE];
    uint16_t       avail_event;
} vq_used_t;

 












typedef struct {
    vq_desc_t  desc[VQ_SIZE];    
} __attribute__((packed, aligned(4096))) vq_page0_t;

typedef struct {
    vq_avail_t avail;
    uint8_t    _pad[4096 - sizeof(vq_avail_t)];
} __attribute__((packed, aligned(4096))) vq_page1_t;

typedef struct {
    vq_used_t  used;
} __attribute__((packed, aligned(4096))) vq_page2_t;

 
typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} vblk_req_hdr_t;

 


typedef struct {
    uint16_t     io_base;
    uint16_t     queue_size;

     
    uint8_t     *vq_base;    
    uint32_t     vq_pages;   
    vq_page0_t  *pg0;        

     
    vq_desc_t   *desc;
    vq_avail_t  *avail;
    vq_used_t   *used;

    uint16_t     last_used_idx;

     
    vblk_req_hdr_t *req_hdr;
    uint8_t        *req_status;
} vblk_priv_t;

 


static inline void vblk_status_write(uint16_t io_base, uint8_t val) {
    outb((uint16_t)(io_base + VREG_STATUS), val);
}

 


static bool vblk_pci_find(uint8_t *bus_out, uint8_t *dev_out, uint8_t *fn_out) {
    DBG_MSG("VBK", "pci_find: scanning bus 0..7");
    for (uint16_t bus = 0; bus < 8; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            uint32_t id = pci_read32((uint8_t)bus, dev, 0, 0);
            if ((id & 0xFFFF) == 0xFFFF) continue;    
            uint16_t vid = (uint16_t)(id & 0xFFFF);
            uint16_t did = (uint16_t)(id >> 16);
            if (vid == VIRTIO_VENDOR_ID &&
                (did == VIRTIO_BLK_DEV_ID || did == VIRTIO_BLK_DEV_ID2)) {
                DBG_VAL("VBK", "pci_find: bus",   bus);
                DBG_VAL("VBK", "pci_find: dev",   dev);
                DBG_VAL("VBK", "pci_find: did",   did);
                *bus_out = (uint8_t)bus;
                *dev_out = dev;
                *fn_out  = 0;
                return true;
            }
        }
    }
    DBG_MSG("VBK", "pci_find: not found");
    return false;
}

 


static inline void vblk_flush_range(const void *start, uint32_t len) {
    const uint8_t *p = (const uint8_t *)start;
    const uint8_t *end = p + len;
    while (p < end) {
        __asm__ volatile ("clflush (%0)" :: "r"(p) : "memory");
        p += 64;
    }
    __asm__ volatile ("mfence" ::: "memory");
}

static inline uint16_t vblk_read_used_idx(vq_used_t *used) {
     
    volatile uint16_t *ptr = (volatile uint16_t *)((uint8_t *)used + 2);
    __asm__ volatile ("clflush (%0)" :: "r"(ptr) : "memory");
    __asm__ volatile ("lfence" ::: "memory");
    return *ptr;
}

 


static int vblk_do_request(vblk_priv_t *p,
                            uint32_t type, uint64_t sector,
                            uint32_t count, void *data_buf) {
    uint32_t data_len = count * SECTOR_SIZE;

     
     

     


    uint32_t dma_size = (data_len < 4096) ? 4096 : data_len;
    uint8_t *dma_buf  = (uint8_t *)dma_alloc(dma_size);
    if (!dma_buf) {
        DBG_MSG("VBK", "  ERR: dma_alloc failed");
        return VFS_ERR_NOMEM;
    }
    uint64_t dma_phys = virt_to_phys(dma_buf);

     
    if (dma_phys > 0xFFFFFFFFULL) {
        DBG_VAL("VBK", "  ERR: dma_phys > 4GB!", dma_phys);
        dma_free(dma_buf, dma_size);
        return VFS_ERR_IO;
    }

     
    if (type == VBLK_T_OUT) {
        uint8_t *src = (uint8_t *)data_buf;
        for (uint32_t i = 0; i < data_len; i++) dma_buf[i] = src[i];
        vblk_flush_range(dma_buf, data_len);
        DBG_MSG("VBK", "  write: copied and flushed data to DMA buf");
    }

     


    p->req_hdr->type     = type;
    p->req_hdr->reserved = 0;
    p->req_hdr->sector   = sector;
    *p->req_status       = 0xFF;
    vblk_flush_range(p->req_hdr, sizeof(vblk_req_hdr_t) + 1);

    uint64_t hdr_phys    = virt_to_phys(p->req_hdr);
    uint64_t status_phys = virt_to_phys(p->req_status);

    if (hdr_phys > 0xFFFFFFFFULL || status_phys > 0xFFFFFFFFULL) {
        DBG_MSG("VBK", "  ERR: hdr/status phys > 4GB!");
        dma_free(dma_buf, dma_size);
        return VFS_ERR_IO;
    }

     




    uint16_t di_hdr    = 0;
    uint16_t di_data   = 1;
    uint16_t di_status = 2;

    p->desc[di_hdr].addr  = hdr_phys;
    p->desc[di_hdr].len   = sizeof(vblk_req_hdr_t);
    p->desc[di_hdr].flags = VDESC_F_NEXT;
    p->desc[di_hdr].next  = di_data;

    p->desc[di_data].addr  = dma_phys;
    p->desc[di_data].len   = data_len;
    p->desc[di_data].flags = (type == VBLK_T_IN ? VDESC_F_WRITE : 0) | VDESC_F_NEXT;
    p->desc[di_data].next  = di_status;

    p->desc[di_status].addr  = status_phys;
    p->desc[di_status].len   = 1;
    p->desc[di_status].flags = VDESC_F_WRITE;
    p->desc[di_status].next  = 0;

    vblk_flush_range(&p->desc[di_hdr],    sizeof(vq_desc_t));
    vblk_flush_range(&p->desc[di_data],   sizeof(vq_desc_t));
    vblk_flush_range(&p->desc[di_status], sizeof(vq_desc_t));

     



    p->avail->flags = 0;

    uint16_t avail_slot = p->avail->idx % (uint16_t)p->queue_size;
    p->avail->ring[avail_slot] = di_hdr;

    __asm__ volatile ("mfence" ::: "memory");
    p->avail->idx = (uint16_t)(p->avail->idx + 1);

     
    vblk_flush_range(p->avail, sizeof(vq_avail_t));

     
    uint16_t used_before = vblk_read_used_idx(p->used);
    uint16_t cur_used;

     
    outw((uint16_t)(p->io_base + VREG_QUEUE_NOTIFY), 0);

     




    inb((uint16_t)(p->io_base + VREG_ISR));

     




    uint32_t spin = 0;

    while (1) {
        cur_used = vblk_read_used_idx(p->used);
        if (cur_used != used_before) break;

        __asm__ volatile ("pause" ::: "memory");
        spin++;

        if (spin >= 50000000) {
             
            DBG_MSG("VBK", "TIMEOUT: virtio never responded — state dump:");
            DBG_VAL("VBK", "  io_base",       p->io_base);
            DBG_VAL("VBK", "  used->idx",      cur_used);
            DBG_VAL("VBK", "  used_before",    used_before);
            DBG_VAL("VBK", "  last_used_idx",  p->last_used_idx);
            DBG_VAL("VBK", "  avail->idx",     p->avail->idx);
            DBG_VAL("VBK", "  ISR",     inb((uint16_t)(p->io_base + VREG_ISR)));
            DBG_VAL("VBK", "  STATUS",  inb((uint16_t)(p->io_base + VREG_STATUS)));
            DBG_VAL("VBK", "  req_status byte", *p->req_status);
            outw((uint16_t)(p->io_base + VREG_QUEUE_SEL), 0);
            DBG_VAL("VBK", "  PFN readback",
                    inl((uint16_t)(p->io_base + VREG_QUEUE_PFN)));
            DBG_VAL("VBK", "  pg0 phys",  virt_to_phys(p->pg0));
            DBG_VAL("VBK", "  avail phys",virt_to_phys(p->avail));
            DBG_VAL("VBK", "  used phys", virt_to_phys(p->used));
            dma_free(dma_buf, dma_size);
            return VFS_ERR_IO;
        }
    }

request_done:
    p->last_used_idx = cur_used;

     
    uint16_t used_elem_idx = (uint16_t)((cur_used - 1) % p->queue_size);
    vblk_flush_range(&p->used->ring[used_elem_idx], sizeof(vq_used_elem_t));

     
    __asm__ volatile ("clflush (%0)" :: "r"(p->req_status) : "memory");
    __asm__ volatile ("lfence"       ::: "memory");
    uint8_t status_byte = *p->req_status;
    DBG_VAL("VBK", "  req_status byte", status_byte);

    if (status_byte != VBLK_S_OK) {
        DBG_VAL("VBK", "ERR: device status", status_byte);
        dma_free(dma_buf, dma_size);
        return VFS_ERR_IO;
    }

     
    if (type == VBLK_T_IN) {
        vblk_flush_range(dma_buf, data_len);
        uint8_t *dst = (uint8_t *)data_buf;
        for (uint32_t i = 0; i < data_len; i++) dst[i] = dma_buf[i];
    }

    dma_free(dma_buf, dma_size);
    return VFS_OK;
}

 


static int vblk_read_sectors(blkdev_t *dev, uint64_t lba,
                              uint32_t count, void *buf) {
    vblk_priv_t *p = (vblk_priv_t *)dev->priv;
    uint8_t *dst = (uint8_t *)buf;
    for (uint32_t i = 0; i < count; i++) {
        int r = vblk_do_request(p, VBLK_T_IN, lba + i, 1,
                                 dst + (uint64_t)i * SECTOR_SIZE);
        if (r != VFS_OK) {
            DBG_VAL("VBK", "read_sectors FAILED lba", lba + i);
            return r;
        }
    }
    return VFS_OK;
}

static int vblk_write_sectors(blkdev_t *dev, uint64_t lba,
                               uint32_t count, const void *buf) {
    vblk_priv_t *p = (vblk_priv_t *)dev->priv;
    const uint8_t *src = (const uint8_t *)buf;
    for (uint32_t i = 0; i < count; i++) {
        int r = vblk_do_request(p, VBLK_T_OUT, lba + i, 1,
                                 (void *)(src + (uint64_t)i * SECTOR_SIZE));
        if (r != VFS_OK) {
            DBG_VAL("VBK", "write_sectors FAILED lba", lba + i);
            return r;
        }
    }
    return VFS_OK;
}

static blkdev_ops_t g_virtio_blk_ops = {
    .read_sectors  = vblk_read_sectors,
    .write_sectors = vblk_write_sectors,
    .destroy       = NULL,
};

 


int virtio_blk_init(void) {
    DBG_MSG("VBK", "=== virtio_blk_init START ===");

     
    uint8_t bus = 0, dev_n = 0, fn = 0;
    if (!vblk_pci_find(&bus, &dev_n, &fn)) {
        DBG_MSG("VBK", "ERR: device not found");
        return VFS_ERR_NXDEV;
    }

    uint16_t pci_cmd = pci_read16(bus, dev_n, fn, 0x04);
    uint16_t pci_did = pci_read16(bus, dev_n, fn, 0x02);
    uint8_t  pci_rev = (uint8_t)(pci_read32(bus, dev_n, fn, 0x08) & 0xFF);
    DBG_VAL("VBK", "PCI did", pci_did);
    DBG_VAL("VBK", "PCI rev", pci_rev);

     
    pci_write32(bus, dev_n, fn, 0x04, (uint32_t)(pci_cmd | 0x05));

     
    uint32_t bar0 = pci_read32(bus, dev_n, fn, 0x10);
    DBG_VAL("VBK", "BAR0 raw", bar0);
    if (!(bar0 & 1)) {
        DBG_MSG("VBK", "ERR: BAR0 is MMIO, expected IO");
        return VFS_ERR_NXDEV;
    }
    uint16_t io_base = (uint16_t)(bar0 & 0xFFFC);
    DBG_VAL("VBK", "io_base", io_base);

     
    vblk_status_write(io_base, 0);
    for (volatile int d = 0; d < 100000; d++) __asm__ volatile ("pause");

     
    vblk_status_write(io_base, VSTAT_ACK);
    vblk_status_write(io_base, VSTAT_ACK | VSTAT_DRIVER);

     
    uint32_t host_feat = inl((uint16_t)(io_base + VREG_HOST_FEATURES));
    DBG_VAL("VBK", "host_features", host_feat);
    uint32_t guest_feat = host_feat & (VBLK_F_RO | VBLK_F_BLK_SIZE);
    outl((uint16_t)(io_base + VREG_GUEST_FEATURES), guest_feat);
    bool readonly = !!(host_feat & VBLK_F_RO);

     
    bool features_ok_set = false;
    if (pci_did == VIRTIO_BLK_DEV_ID2) {
        vblk_status_write(io_base, VSTAT_ACK | VSTAT_DRIVER | VSTAT_FEATURES_OK);
        uint8_t sc = inb((uint16_t)(io_base + VREG_STATUS));
        if (sc & VSTAT_FEATURES_OK) {
            features_ok_set = true;
        } else {
            DBG_MSG("VBK", "FEATURES_OK rejected");
            vblk_status_write(io_base, VSTAT_ACK | VSTAT_DRIVER);
        }
    }

     
    uint64_t cap_lo   = inl((uint16_t)(io_base + VREG_CONFIG));
    uint64_t cap_hi   = inl((uint16_t)(io_base + VREG_CONFIG + 4));
    uint64_t capacity = cap_lo | (cap_hi << 32);
    DBG_VAL("VBK", "capacity_sectors", capacity);

     
    DBG_MSG("VBK", "step8: virtqueue");
    outw((uint16_t)(io_base + VREG_QUEUE_SEL), 0);
    uint16_t qs = inw((uint16_t)(io_base + VREG_QUEUE_SIZE));
    DBG_VAL("VBK", "  queue_size", qs);
    if (qs == 0) {
        DBG_MSG("VBK", "ERR: queue_size == 0");
        return VFS_ERR_IO;
    }
    if (qs > VQ_SIZE) {
        DBG_VAL("VBK", "ERR: queue_size > VQ_SIZE; increase VQ_SIZE to", qs);
        return VFS_ERR_IO;
    }

     









    uint32_t avail_off  = 16u * qs;
    uint32_t avail_size = 4u + 2u * qs + 2u;
    uint32_t used_off   = (avail_off + avail_size + 4095u) & ~4095u;
    uint32_t used_size  = 4u + 8u * qs + 2u;
    uint32_t vq_total   = used_off + used_size;
    uint32_t vq_pages   = (vq_total + 4095u) / 4096u;

    DBG_VAL("VBK", "  qs",        qs);
    DBG_VAL("VBK", "  avail_off", avail_off);
    DBG_VAL("VBK", "  used_off",  used_off);
    DBG_VAL("VBK", "  vq_pages",  vq_pages);

    uint8_t *contig = (uint8_t *)dma_alloc((uint64_t)vq_pages * 4096);
    if (!contig) { DBG_MSG("VBK", "ERR: dma_alloc vq"); return VFS_ERR_NOMEM; }
    uint64_t pg0_phys = virt_to_phys(contig);
    DBG_VAL("VBK", "  contig base phys", pg0_phys);
    if (pg0_phys & 0xFFF) {
        DBG_VAL("VBK", "ERR: contig not page-aligned!", pg0_phys);
        dma_free(contig, (uint64_t)vq_pages * 4096);
        return VFS_ERR_IO;
    }

     
    uint8_t *req_buf = (uint8_t *)dma_alloc(4096);
    if (!req_buf) {
        DBG_MSG("VBK", "ERR: dma_alloc req_buf");
        dma_free(contig, (uint64_t)vq_pages * 4096);
        return VFS_ERR_NOMEM;
    }
    uint64_t req_buf_phys = virt_to_phys(req_buf);
    DBG_VAL("VBK", "  req_buf phys", req_buf_phys);

     
    vblk_priv_t *priv = (vblk_priv_t *)kzalloc(sizeof(vblk_priv_t));
    if (!priv) {
        DBG_MSG("VBK","ERR: kzalloc priv");
        dma_free(req_buf, 4096);
        dma_free(contig, (uint64_t)vq_pages * 4096);
        return VFS_ERR_NOMEM;
    }

    priv->io_base       = io_base;
    priv->queue_size    = qs;
    priv->vq_pages      = vq_pages;
    priv->vq_base       = contig;
     
    priv->desc          = (vq_desc_t *)contig;
    priv->avail         = (vq_avail_t *)(contig + avail_off);
    priv->used          = (vq_used_t  *)(contig + used_off);
     
    priv->pg0           = (vq_page0_t *)contig;
    priv->last_used_idx = 0;
    priv->req_hdr       = (vblk_req_hdr_t *)req_buf;
    priv->req_status    = req_buf + sizeof(vblk_req_hdr_t);

    DBG_VAL("VBK", "  desc  phys", pg0_phys);
    DBG_VAL("VBK", "  avail phys", pg0_phys + avail_off);
    DBG_VAL("VBK", "  used  phys", pg0_phys + used_off);

     
    priv->avail->flags = 0;
    priv->avail->idx   = 0;

     
    vblk_flush_range(contig, (uint64_t)vq_pages * 4096);

     



    uint32_t pfn = (uint32_t)(pg0_phys >> 12);
    DBG_VAL("VBK", "  writing PFN", pfn);
    __asm__ volatile ("mfence" ::: "memory");
    outl((uint16_t)(io_base + VREG_QUEUE_PFN), pfn);

     
    outw((uint16_t)(io_base + VREG_QUEUE_SEL), 0);
    uint32_t pfn_rb = inl((uint16_t)(io_base + VREG_QUEUE_PFN));
    DBG_VAL("VBK", "  PFN readback", pfn_rb);
    if (pfn_rb != pfn) {
        DBG_VAL("VBK", "ERR: PFN readback mismatch! expected", pfn);
        dma_free(contig, (uint64_t)vq_pages * 4096);
        dma_free(req_buf, 4096);
        return VFS_ERR_IO;
    }

     
    uint8_t final_status = VSTAT_ACK | VSTAT_DRIVER | VSTAT_DRIVER_OK;
    if (features_ok_set) final_status |= VSTAT_FEATURES_OK;
    vblk_status_write(io_base, final_status);

    uint8_t s_final = inb((uint16_t)(io_base + VREG_STATUS));
    if (s_final & VSTAT_FAILED) {
        DBG_MSG("VBK", "ERR: device FAILED after DRIVER_OK");
        return VFS_ERR_IO;
    }
    DBG_MSG("VBK", "=== virtio-blk READY ===");

     
    blkdev_t *blkdev = (blkdev_t *)kzalloc(sizeof(blkdev_t));
    if (!blkdev) return VFS_ERR_NOMEM;

    kstr_cpy(blkdev->name, "vda", 32);
    blkdev->sector_size  = SECTOR_SIZE;
    blkdev->sector_count = capacity;
    blkdev->priv         = priv;
    blkdev->ops          = &g_virtio_blk_ops;
    blkdev->readonly     = readonly;

    int idx = blkdev_register(blkdev);
    if (idx < 0) {
        DBG_VAL("VBK", "ERR: blkdev_register failed", (uint64_t)(int64_t)idx);
        return idx;
    }
    DBG_MSG("VBK", "vda registered OK");
    return VFS_OK;
}

 



 
static fs_ops_t  *g_fs_drivers[16];
static int        g_fs_count = 0;

 
static vfs_mount_t g_mounts[VFS_MAX_MOUNTS];
static int         g_mount_count = 0;
static spinlock_t  g_mount_lock = 0;

 
typedef struct {
    vfs_node_t *node;
    uint64_t    offset;
    int         flags;
    bool        used;
} vfs_fd_t;

static vfs_fd_t   g_fds[VFS_MAX_FD];
static spinlock_t g_fd_lock = 0;

int vfs_init(void) {
    kmem_set(g_fs_drivers, 0, sizeof(g_fs_drivers));
    kmem_set(g_mounts,     0, sizeof(g_mounts));
    kmem_set(g_fds,        0, sizeof(g_fds));
    g_fs_count    = 0;
    g_mount_count = 0;
    DBG_MSG("VFS", "vfs_init ok");
    return VFS_OK;
}

int vfs_register_fs(fs_ops_t *ops) {
    if (g_fs_count >= 16) return VFS_ERR_BUSY;
    g_fs_drivers[g_fs_count++] = ops;
    DBG_MSG("VFS", "fs registered");
    return VFS_OK;
}

 
static fs_ops_t *vfs_find_fs(const char *name) {
    for (int i = 0; i < g_fs_count; i++)
        if (g_fs_drivers[i] && kstr_cmp(g_fs_drivers[i]->name, name) == 0)
            return g_fs_drivers[i];
    return NULL;
}

 
static vfs_mount_t *vfs_find_mount(const char *path) {
    vfs_mount_t *best = NULL;
    int best_len = -1;
    for (int i = 0; i < g_mount_count; i++) {
        if (!g_mounts[i].root) continue;
        int mlen = kstr_len(g_mounts[i].path);
        if (kstr_ncmp(path, g_mounts[i].path, mlen) == 0) {
             
            char next = path[mlen];
            if (next == '\0' || next == '/' || g_mounts[i].path[mlen - 1] == '/') {
                if (mlen > best_len) { best_len = mlen; best = &g_mounts[i]; }
            }
        }
    }
    return best;
}

int vfs_mount(const char *dev_name, const char *fs_name,
              const char *path, uint32_t flags) {
    blkdev_t *dev = blkdev_find(dev_name);
    if (!dev) {
        DBG_MSG("VFS", "vfs_mount: device not found");
        return VFS_ERR_NXDEV;
    }
    fs_ops_t *fs = vfs_find_fs(fs_name);
    if (!fs) {
        DBG_MSG("VFS", "vfs_mount: fs not found");
        return VFS_ERR_UNSUP;
    }

    spin_lock(&g_mount_lock);
    if (g_mount_count >= VFS_MAX_MOUNTS) {
        spin_unlock(&g_mount_lock);
        return VFS_ERR_BUSY;
    }

    DBG_MSG("VFS", "vfs_mount: calling fs->mount...");
    dbg_puts("[VFS] fs name: "); dbg_puts(fs->name); dbg_puts("\n");
    dbg_puts("[VFS] dev name: "); dbg_puts(dev->name); dbg_puts("\n");
    DBG_VAL("VFS", "dev sector_size",  (uint64_t)dev->sector_size);
    DBG_VAL("VFS", "dev sector_count", dev->sector_count);
    vfs_node_t *root = fs->mount(dev, flags);
    if (!root) {
        spin_unlock(&g_mount_lock);
        DBG_MSG("VFS", "vfs_mount: fs->mount failed");
        return VFS_ERR_IO;
    }

    vfs_mount_t *mnt = &g_mounts[g_mount_count++];
    kstr_cpy(mnt->path, path, VFS_MAX_PATH);
    mnt->root  = root;
    mnt->dev   = dev;
    mnt->fs    = fs;
    mnt->flags = flags;
    root->mount = mnt;
    spin_unlock(&g_mount_lock);

    DBG_MSG("VFS", "mounted ok");
    return VFS_OK;
}

int vfs_umount(const char *path) {
    spin_lock(&g_mount_lock);
    for (int i = 0; i < g_mount_count; i++) {
        if (kstr_cmp(g_mounts[i].path, path) == 0) {
            if (g_mounts[i].fs && g_mounts[i].fs->umount)
                g_mounts[i].fs->umount(&g_mounts[i]);
            if (g_mounts[i].root && g_mounts[i].root->ops->release)
                g_mounts[i].root->ops->release(g_mounts[i].root);
            kmem_set(&g_mounts[i], 0, sizeof(vfs_mount_t));
             
            for (int j = i; j < g_mount_count - 1; j++)
                g_mounts[j] = g_mounts[j + 1];
            g_mount_count--;
            spin_unlock(&g_mount_lock);
            return VFS_OK;
        }
    }
    spin_unlock(&g_mount_lock);
    return VFS_ERR_NOENT;
}

 



int vfs_mount_count(void) {
    return g_mount_count;
}

 



bool vfs_mount_info(int idx, char *path_out, size_t path_sz,
                    char *dev_out, size_t dev_sz) {
    if (idx < 0 || idx >= g_mount_count) return false;
    vfs_mount_t *mnt = &g_mounts[idx];

    if (path_out && path_sz > 0) kstr_cpy(path_out, mnt->path, (int)path_sz);
    if (dev_out && dev_sz > 0) {
        if (mnt->dev) kstr_cpy(dev_out, mnt->dev->name, (int)dev_sz);
        else          dev_out[0] = '\0';
    }
    return true;
}

 


static vfs_node_t *vfs_resolve(const char *path, bool parent, char *last_component) {
    vfs_mount_t *mnt = vfs_find_mount(path);
    if (!mnt) return NULL;

     
    int mlen = kstr_len(mnt->path);
    const char *rel = path + mlen;
     
    while (*rel == '/') rel++;

    vfs_node_t *cur = mnt->root;
    cur->refcount++;

    if (*rel == '\0') {
         
        if (parent && last_component) {
            last_component[0] = '.';
            last_component[1] = '\0';
        }
        return cur;
    }

     
    char comp[VFS_MAX_NAME + 1];
    const char *p = rel;

    while (*p) {
         
        int ci = 0;
        while (*p && *p != '/') {
            if (ci < VFS_MAX_NAME) comp[ci++] = *p;
            p++;
        }
        comp[ci] = '\0';
        while (*p == '/') p++;

         
        if (parent && *p == '\0') {
            if (last_component) kstr_cpy(last_component, comp, VFS_MAX_NAME + 1);
            return cur;
        }

         
        if (!cur->ops || !cur->ops->lookup) {
            if (cur->ops && cur->ops->release) cur->ops->release(cur);
            return NULL;
        }
        vfs_node_t *next = cur->ops->lookup(cur, comp);
        if (cur->ops->release) cur->ops->release(cur);
        if (!next) return NULL;
        cur = next;
    }

    return cur;
}

 


static int fd_alloc(vfs_node_t *node, int flags) {
    spin_lock(&g_fd_lock);
    for (int i = 3; i < VFS_MAX_FD; i++) {   
        if (!g_fds[i].used) {
            g_fds[i].node   = node;
            g_fds[i].offset = 0;
            g_fds[i].flags  = flags;
            g_fds[i].used   = true;
            spin_unlock(&g_fd_lock);
            return i;
        }
    }
    spin_unlock(&g_fd_lock);
    return VFS_ERR_NFILE;
}

static vfs_fd_t *fd_get(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FD || !g_fds[fd].used)
        return NULL;
    return &g_fds[fd];
}

 


int vfs_open(const char *path, int flags) {
    bool create  = !!(flags & O_CREAT);
    bool is_dir  = !!(flags & O_DIRECTORY);

    vfs_node_t *node;

    if (create) {
        char last[VFS_MAX_NAME + 1];
        vfs_node_t *parent = vfs_resolve(path, true, last);
        if (!parent) return VFS_ERR_NOENT;

         
        node = parent->ops->lookup(parent, last);
        if (!node) {
             
            if (!parent->ops->create) {
                if (parent->ops->release) parent->ops->release(parent);
                return VFS_ERR_UNSUP;
            }
            node = parent->ops->create(parent, last,
                                        is_dir ? VFS_TYPE_DIR : VFS_TYPE_FILE);
            if (!node) {
                if (parent->ops->release) parent->ops->release(parent);
                return VFS_ERR_IO;
            }
        }
        if (parent->ops->release) parent->ops->release(parent);
    } else {
        node = vfs_resolve(path, false, NULL);
        if (!node) return VFS_ERR_NOENT;
    }

     
    if (is_dir && node->stat.type != VFS_TYPE_DIR) {
        if (node->ops->release) node->ops->release(node);
        return VFS_ERR_NOTDIR;
    }
    if (!is_dir && node->stat.type == VFS_TYPE_DIR &&
        !(flags & O_DIRECTORY)) {
        if (node->ops->release) node->ops->release(node);
        return VFS_ERR_ISDIR;
    }

     
    if ((flags & O_TRUNC) && node->stat.type == VFS_TYPE_FILE) {
        if (node->ops->truncate) node->ops->truncate(node, 0);
    }

    return fd_alloc(node, flags);
}

int vfs_close(int fd) {
    vfs_fd_t *f = fd_get(fd);
    if (!f) return VFS_ERR_BADF;
    spin_lock(&g_fd_lock);
    if (f->node && f->node->ops->release)
        f->node->ops->release(f->node);
    kmem_set(f, 0, sizeof(vfs_fd_t));
    spin_unlock(&g_fd_lock);
    return VFS_OK;
}

static void vfs_refresh_size(vfs_node_t *n);

int64_t vfs_read(int fd, void *buf, uint64_t size) {
    vfs_fd_t *f = fd_get(fd);
    if (!f) return VFS_ERR_BADF;
    if (!f->node->ops->read) return VFS_ERR_UNSUP;
    int64_t r = f->node->ops->read(f->node, f->offset, size, buf);
    if (r > 0) f->offset += (uint64_t)r;
    return r;
}

int64_t vfs_write(int fd, const void *buf, uint64_t size) {
    vfs_fd_t *f = fd_get(fd);
    if (!f) return VFS_ERR_BADF;
    if (f->flags & O_APPEND) { vfs_refresh_size(f->node); f->offset = f->node->stat.size; }
    if (!f->node->ops->write) return VFS_ERR_UNSUP;
    int64_t r = f->node->ops->write(f->node, f->offset, size, buf);
    if (r > 0) f->offset += (uint64_t)r;
    return r;
}

int64_t vfs_seek(int fd, int64_t offset, int whence) {
    vfs_fd_t *f = fd_get(fd);
    if (!f) return VFS_ERR_BADF;
    int64_t new_off;
    switch (whence) {
        case 0: new_off = offset; break;
        case 1: new_off = (int64_t)f->offset + offset; break;
        case 2: vfs_refresh_size(f->node); new_off = (int64_t)f->node->stat.size + offset; break;
        default: return VFS_ERR_INVAL;
    }
    if (new_off < 0) return VFS_ERR_INVAL;
    f->offset = (uint64_t)new_off;
    return new_off;
}

int vfs_stat(const char *path, vfs_stat_t *out) {
    vfs_node_t *node = vfs_resolve(path, false, NULL);
    if (!node) return VFS_ERR_NOENT;
    *out = node->stat;
    if (node->ops->release) node->ops->release(node);
    return VFS_OK;
}

int vfs_fstat(int fd, vfs_stat_t *out) {
    vfs_fd_t *f = fd_get(fd);
    if (!f) return VFS_ERR_BADF;
    vfs_refresh_size(f->node);
    *out = f->node->stat;
    return VFS_OK;
}

int vfs_readdir(int fd, uint32_t index, vfs_dirent_t *out) {
    vfs_fd_t *f = fd_get(fd);
    if (!f) return VFS_ERR_BADF;
    if (f->node->stat.type != VFS_TYPE_DIR) return VFS_ERR_NOTDIR;
    if (!f->node->ops->readdir) return VFS_ERR_UNSUP;
    return f->node->ops->readdir(f->node, index, out);
}

int vfs_mkdir(const char *path) {
    char last[VFS_MAX_NAME + 1];
    vfs_node_t *parent = vfs_resolve(path, true, last);
    if (!parent) return VFS_ERR_NOENT;
    if (!parent->ops->create) {
        if (parent->ops->release) parent->ops->release(parent);
        return VFS_ERR_UNSUP;
    }
    vfs_node_t *n = parent->ops->create(parent, last, VFS_TYPE_DIR);
    if (parent->ops->release) parent->ops->release(parent);
    if (!n) return VFS_ERR_IO;
    if (n->ops->release) n->ops->release(n);
    return VFS_OK;
}

int vfs_unlink(const char *path) {
    char last[VFS_MAX_NAME + 1];
    vfs_node_t *parent = vfs_resolve(path, true, last);
    if (!parent) return VFS_ERR_NOENT;
    if (!parent->ops->unlink) {
        if (parent->ops->release) parent->ops->release(parent);
        return VFS_ERR_UNSUP;
    }
    int r = parent->ops->unlink(parent, last);
    if (parent->ops->release) parent->ops->release(parent);
    return r;
}

int vfs_rmdir(const char *path) {
    return vfs_unlink(path);   
}

int vfs_truncate(const char *path, uint64_t size) {
    vfs_node_t *node = vfs_resolve(path, false, NULL);
    if (!node) return VFS_ERR_NOENT;
    int r = VFS_ERR_UNSUP;
    if (node->ops->truncate) r = node->ops->truncate(node, size);
    if (node->ops->release) node->ops->release(node);
    return r;
}

int vfs_ftruncate(int fd, uint64_t size) {
    vfs_fd_t *f = fd_get(fd);
    if (!f) return VFS_ERR_BADF;
    if (!f->node->ops->truncate) return VFS_ERR_UNSUP;
    return f->node->ops->truncate(f->node, size);
}

/* ================================================================== */
/* POSIX layer: credentials, symlink-aware path walk, attributes,      */
/* links, rename, xattr access.  Used by the Linux/musl syscalls; the  */
/* classic vfs_open()/vfs_stat()/... above keep working unchanged and  */
/* behave as root (kernel-internal callers).                           */
/* ================================================================== */

#define VFS_SYMLOOP 40
#define V_S_IFMT   0170000u
#define V_S_IFDIR  0040000u
#define V_S_IFREG  0100000u
#define V_S_IFLNK  0120000u

static const vfs_cred_t g_root_cred = { 0, 0, 0, 0, 022, 0, { 0 } };
static const vfs_cred_t *(*g_cred_provider)(void);

void vfs_set_cred_provider(const vfs_cred_t *(*fn)(void)) { g_cred_provider = fn; }
const vfs_cred_t *vfs_cred(void) {
    const vfs_cred_t *c = g_cred_provider ? g_cred_provider() : NULL;
    return c ? c : &g_root_cred;
}

static inline const vfs_node_xops_t *nx(vfs_node_t *n) { return (n && n->ops) ? n->ops->x : NULL; }
static void node_put(vfs_node_t *n) { if (n && n->ops && n->ops->release) n->ops->release(n); }
void vfs_node_put(vfs_node_t *n) { node_put(n); }

/* Walk `in` component by component.
     want_parent : stop before the last component, return the parent, name in `last`
     follow_last : resolve a symbolic link in the last component too
   Search (x) permission is required on every directory that is traversed.            */
static vfs_node_t *vfs_walk(const char *in, bool want_parent, bool follow_last, char *last,
                            const vfs_cred_t *cr, int *err) {
    char *buf = (char *)kmalloc(VFS_MAX_PATH), *tgt = (char *)kmalloc(VFS_MAX_PATH), *nb = (char *)kmalloc(VFS_MAX_PATH);
    vfs_node_t *result = NULL;
    *err = VFS_ERR_NOMEM;
    if (!buf || !tgt || !nb) goto out;
    if (kstr_len(in) >= VFS_MAX_PATH) { *err = VFS_ERR_NAMETOOLONG; goto out; }
    kstr_cpy(buf, in, VFS_MAX_PATH);
    int loops = 0;
    for (;;) {
        vfs_mount_t *mnt = vfs_find_mount(buf);
        if (!mnt) { *err = VFS_ERR_NOENT; goto out; }
        const char *rel = buf + kstr_len(mnt->path);
        while (*rel == '/') rel++;
        vfs_node_t *cur = mnt->root;
        cur->refcount++;
        if (!*rel) {
            if (want_parent && last) { last[0] = '.'; last[1] = '\0'; }
            result = cur; *err = VFS_OK; goto out;
        }
        char comp[VFS_MAX_NAME + 1];
        const char *p = rel;
        bool restart = false;
        while (*p) {
            const char *cs = p;
            int ci = 0;
            while (*p && *p != '/') { if (ci < VFS_MAX_NAME) comp[ci++] = *p; p++; }
            comp[ci] = '\0';
            const char *ce = p;
            while (*p == '/') p++;
            bool is_last = (*p == '\0');
            if (ci == VFS_MAX_NAME && *ce != '/' && ce - cs > VFS_MAX_NAME) { node_put(cur); *err = VFS_ERR_NAMETOOLONG; goto out; }
            if (want_parent && is_last) {
                if (last) kstr_cpy(last, comp, VFS_MAX_NAME + 1);
                result = cur; *err = VFS_OK; goto out;
            }
            if (cur->stat.type != VFS_TYPE_DIR) { node_put(cur); *err = VFS_ERR_NOTDIR; goto out; }
            const vfs_node_xops_t *x = nx(cur);
            if (cr && x && x->permission) {
                int r = x->permission(cur, cr, VFS_PERM_X);
                if (r) { node_put(cur); *err = r; goto out; }
            }
            if (!cur->ops->lookup) { node_put(cur); *err = VFS_ERR_UNSUP; goto out; }
            vfs_node_t *next = cur->ops->lookup(cur, comp);
            node_put(cur);
            if (!next) { *err = VFS_ERR_NOENT; goto out; }
            if (next->stat.type == VFS_TYPE_LINK && (!is_last || follow_last)) {
                const vfs_node_xops_t *lx = nx(next);
                if (!lx || !lx->readlink) { node_put(next); *err = VFS_ERR_INVAL; goto out; }
                if (++loops > VFS_SYMLOOP) { node_put(next); *err = VFS_ERR_LOOP; goto out; }
                int tl = lx->readlink(next, tgt, VFS_MAX_PATH - 1);
                node_put(next);
                if (tl <= 0) { *err = VFS_ERR_IO; goto out; }
                tgt[tl] = '\0';
                size_t pre = (size_t)(cs - buf), rest = (size_t)kstr_len(ce);
                size_t o = 0;
                if (tgt[0] != '/') { if (pre + (size_t)tl + rest + 1 > VFS_MAX_PATH) { *err = VFS_ERR_NAMETOOLONG; goto out; } for (; o < pre; o++) nb[o] = buf[o]; }
                else if ((size_t)tl + rest + 1 > VFS_MAX_PATH) { *err = VFS_ERR_NAMETOOLONG; goto out; }
                for (int k = 0; k < tl; k++) nb[o++] = tgt[k];
                for (size_t k = 0; k < rest; k++) nb[o++] = ce[k];
                nb[o] = '\0';
                kstr_cpy(buf, nb, VFS_MAX_PATH);
                restart = true;
                break;
            }
            cur = next;
        }
        if (!restart) { result = cur; *err = VFS_OK; goto out; }
    }
out:
    if (buf) kfree(buf);
    if (tgt) kfree(tgt);
    if (nb) kfree(nb);
    return result;
}

static int node_perm(vfs_node_t *n, const vfs_cred_t *cr, uint32_t want) {
    const vfs_node_xops_t *x = nx(n);
    if (x && x->permission) return x->permission(n, cr, want);
    if ((want & VFS_PERM_W) && (n->stat.attr & 1u)) return VFS_ERR_ACCES;      /* FAT read-only bit */
    return VFS_OK;
}

static void attr_synth(vfs_node_t *n, vfs_attr_t *a) {
    kmem_set(a, 0, sizeof(*a));
    bool dir = n->stat.type == VFS_TYPE_DIR;
    a->ino = n->stat.cluster ? n->stat.cluster : 1;
    a->mode = dir ? (V_S_IFDIR | 0755) : (V_S_IFREG | ((n->stat.attr & 1u) ? 0444 : 0644));
    a->nlink = dir ? 2 : 1;
    a->size = n->stat.size;
    a->blocks = (n->stat.alloc_size ? n->stat.alloc_size : n->stat.size + 511) / 512;
    a->blksize = 4096; a->dev = 1;
    a->atime_ns = (uint64_t)n->stat.atime * 1000000000ULL;
    a->mtime_ns = (uint64_t)n->stat.mtime * 1000000000ULL;
    a->ctime_ns = (uint64_t)n->stat.ctime * 1000000000ULL;
    a->btime_ns = a->ctime_ns;
}
static int node_getattr(vfs_node_t *n, vfs_attr_t *a) {
    const vfs_node_xops_t *x = nx(n);
    if (x && x->getattr) return x->getattr(n, a);
    attr_synth(n, a);
    return VFS_OK;
}
static void vfs_refresh_size(vfs_node_t *n) {
    const vfs_node_xops_t *x = nx(n);
    vfs_attr_t a;
    if (x && x->getattr && x->getattr(n, &a) == VFS_OK) { n->stat.size = a.size; n->stat.alloc_size = a.blocks * 512; }
}

int vfs_open2(const char *path, int flags, uint32_t mode) {
    const vfs_cred_t *cr = vfs_cred();
    int acc = flags & O_ACCMODE;
    bool want_r = (acc != O_WRONLY), want_w = (acc == O_WRONLY || acc == O_RDWR || (flags & O_TRUNC));
    bool created = false;
    int err;
    vfs_node_t *node = vfs_walk(path, false, !(flags & VFS_O_NOFOLLOW), NULL, cr, &err);
    if (!node) {
        if (err != VFS_ERR_NOENT || !(flags & O_CREAT)) return err;
        char last[VFS_MAX_NAME + 1];
        /* creating through a dangling symbolic link creates the link's target */
        char *cpath = (char *)kmalloc(VFS_MAX_PATH), *ltgt = (char *)kmalloc(VFS_MAX_PATH);
        if (!cpath || !ltgt) { if (cpath) kfree(cpath); if (ltgt) kfree(ltgt); return VFS_ERR_NOMEM; }
        kstr_cpy(cpath, path, VFS_MAX_PATH);
        if (!(flags & VFS_O_NOFOLLOW)) {
            for (int hop = 0;; hop++) {
                int e2; vfs_node_t *ln = vfs_walk(cpath, false, false, NULL, cr, &e2);
                if (!ln) break;
                const vfs_node_xops_t *lx = nx(ln);
                if (ln->stat.type != VFS_TYPE_LINK || !lx || !lx->readlink) { node_put(ln); break; }
                if (hop >= VFS_SYMLOOP) { node_put(ln); kfree(cpath); kfree(ltgt); return VFS_ERR_LOOP; }
                int tl = lx->readlink(ln, ltgt, VFS_MAX_PATH - 1);
                node_put(ln);
                if (tl <= 0) break;
                ltgt[tl] = '\0';
                if (ltgt[0] == '/') kstr_cpy(cpath, ltgt, VFS_MAX_PATH);
                else {
                    int cut = kstr_len(cpath); while (cut > 0 && cpath[cut - 1] != '/') cut--;
                    if (cut + tl + 1 > VFS_MAX_PATH) { kfree(cpath); kfree(ltgt); return VFS_ERR_NAMETOOLONG; }
                    for (int k = 0; k <= tl; k++) cpath[cut + k] = ltgt[k];
                }
            }
        }
        vfs_node_t *parent = vfs_walk(cpath, true, true, last, cr, &err);
        kfree(cpath); kfree(ltgt);
        if (!parent) return err;
        if (parent->stat.type != VFS_TYPE_DIR) { node_put(parent); return VFS_ERR_NOTDIR; }
        const vfs_node_xops_t *x = nx(parent);
        uint32_t eff = (mode & 07777u) & ~(cr->umask & 0777u);
        vfs_node_t *nn = NULL;
        if (x && x->create2) err = x->create2(parent, last, V_S_IFREG | eff, 0, cr, &nn);
        else if (parent->ops->create) { nn = parent->ops->create(parent, last, VFS_TYPE_FILE); err = nn ? VFS_OK : VFS_ERR_IO; }
        else err = VFS_ERR_UNSUP;
        node_put(parent);
        if (err) return err;
        node = nn; created = true;
    } else {
        if ((flags & O_CREAT) && (flags & VFS_O_EXCL)) { node_put(node); return VFS_ERR_EXIST; }
        if (node->stat.type == VFS_TYPE_LINK) { node_put(node); return VFS_ERR_LOOP; }       /* O_NOFOLLOW on a link */
    }
    if ((flags & VFS_O_LXDIR) && node->stat.type != VFS_TYPE_DIR) { node_put(node); return VFS_ERR_NOTDIR; }
    if (node->stat.type == VFS_TYPE_DIR && want_w) { node_put(node); return VFS_ERR_ISDIR; }
    if (!created) {
        uint32_t need = (want_r ? VFS_PERM_R : 0) | (want_w ? VFS_PERM_W : 0);
        int r = need ? node_perm(node, cr, need) : VFS_OK;
        if (r) { node_put(node); return r; }
    }
    if ((flags & O_TRUNC) && node->stat.type == VFS_TYPE_FILE && !created && node->ops->truncate) {
        int r = node->ops->truncate(node, 0);
        if (r) { node_put(node); return r; }
    }
    int fd = fd_alloc(node, flags & (O_ACCMODE | O_APPEND));
    if (fd < 0) node_put(node);
    return fd;
}

vfs_node_t *vfs_lookup_node(const char *path, int follow, int *err) {
    int e; vfs_node_t *n = vfs_walk(path, false, follow != 0, NULL, vfs_cred(), &e);
    if (err) *err = e;
    return n;
}
vfs_node_t *vfs_fd_node(int fd) { vfs_fd_t *f = fd_get(fd); return f ? f->node : NULL; }
vfs_mount_t *vfs_mount_of_path(const char *path) { return vfs_find_mount(path); }

int vfs_getattr(const char *path, int follow, vfs_attr_t *out) {
    int err; vfs_node_t *n = vfs_walk(path, false, follow != 0, NULL, vfs_cred(), &err);
    if (!n) return err;
    int r = node_getattr(n, out);
    node_put(n);
    return r;
}
int vfs_fgetattr(int fd, vfs_attr_t *out) {
    vfs_fd_t *f = fd_get(fd); if (!f) return VFS_ERR_BADF;
    return node_getattr(f->node, out);
}

static int node_setattr(vfs_node_t *n, const vfs_setattr_t *sa, const vfs_cred_t *cr) {
    const vfs_node_xops_t *x = nx(n);
    if (x && x->setattr) return x->setattr(n, sa, cr);
    if (sa->valid & (VFS_SET_MODE | VFS_SET_UID | VFS_SET_GID)) return VFS_ERR_PERM;   /* FAT has no owners/modes */
    if ((sa->valid & VFS_SET_SIZE) && n->ops->truncate) return n->ops->truncate(n, sa->size);
    return VFS_OK;                                                                    /* times: ignored */
}
int vfs_setattr(const char *path, int follow, const vfs_setattr_t *sa) {
    const vfs_cred_t *cr = vfs_cred(); int err;
    vfs_node_t *n = vfs_walk(path, false, follow != 0, NULL, cr, &err);
    if (!n) return err;
    int r = node_setattr(n, sa, cr);
    node_put(n);
    return r;
}
int vfs_fsetattr(int fd, const vfs_setattr_t *sa) {
    vfs_fd_t *f = fd_get(fd); if (!f) return VFS_ERR_BADF;
    return node_setattr(f->node, sa, vfs_cred());
}

int vfs_access2(const char *path, uint32_t mode, int use_real_ids, int follow) {
    vfs_cred_t c = *vfs_cred();
    if (use_real_ids) { c.euid = c.uid; c.egid = c.gid; }
    int err; vfs_node_t *n = vfs_walk(path, false, follow != 0, NULL, &c, &err);
    if (!n) return err;
    int r = (mode & 7u) ? node_perm(n, &c, mode & 7u) : VFS_OK;
    node_put(n);
    return r;
}

static int create_in(const char *path, uint32_t mode_with_type, uint64_t rdev, bool is_dir_legacy) {
    const vfs_cred_t *cr = vfs_cred(); int err; char last[VFS_MAX_NAME + 1];
    vfs_node_t *parent = vfs_walk(path, true, true, last, cr, &err);
    if (!parent) return err;
    if (parent->stat.type != VFS_TYPE_DIR) { node_put(parent); return VFS_ERR_NOTDIR; }
    if ((last[0] == '.' && last[1] == '\0') || (last[0] == '.' && last[1] == '.' && last[2] == '\0')) { node_put(parent); return VFS_ERR_EXIST; }
    vfs_node_t *ex = parent->ops->lookup ? parent->ops->lookup(parent, last) : NULL;
    if (ex) { node_put(ex); node_put(parent); return VFS_ERR_EXIST; }
    const vfs_node_xops_t *x = nx(parent);
    vfs_node_t *nn = NULL;
    uint32_t eff = (mode_with_type & V_S_IFMT) | ((mode_with_type & 07777u) & ~(cr->umask & 0777u));
    if (x && x->create2) err = x->create2(parent, last, eff, rdev, cr, &nn);
    else if (parent->ops->create) { nn = parent->ops->create(parent, last, is_dir_legacy ? VFS_TYPE_DIR : VFS_TYPE_FILE); err = nn ? VFS_OK : VFS_ERR_IO; }
    else err = VFS_ERR_UNSUP;
    node_put(parent);
    if (!err) node_put(nn);
    return err;
}
int vfs_mkdir2(const char *path, uint32_t mode) { return create_in(path, V_S_IFDIR | (mode & 07777u), 0, true); }
int vfs_mknod2(const char *path, uint32_t mode, uint64_t rdev) {
    if (!(mode & V_S_IFMT)) mode |= V_S_IFREG;
    return create_in(path, mode, rdev, false);
}

static int unlink_in(const char *path, int rmdir) {
    const vfs_cred_t *cr = vfs_cred(); int err; char last[VFS_MAX_NAME + 1];
    vfs_node_t *parent = vfs_walk(path, true, true, last, cr, &err);
    if (!parent) return err;
    int r;
    const vfs_node_xops_t *x = nx(parent);
    if (x && x->unlink2) r = x->unlink2(parent, last, cr, rmdir);
    else if (parent->ops->unlink) {
        vfs_node_t *t = parent->ops->lookup ? parent->ops->lookup(parent, last) : NULL;
        if (!t) r = VFS_ERR_NOENT;
        else {
            bool isdir = t->stat.type == VFS_TYPE_DIR; node_put(t);
            r = (rmdir && !isdir) ? VFS_ERR_NOTDIR : (!rmdir && isdir) ? VFS_ERR_ISDIR : parent->ops->unlink(parent, last);
        }
    } else r = VFS_ERR_UNSUP;
    node_put(parent);
    return r;
}
int vfs_unlink2(const char *path) { return unlink_in(path, 0); }
int vfs_rmdir2(const char *path) { return unlink_in(path, 1); }

int vfs_symlink2(const char *target, const char *linkpath) {
    const vfs_cred_t *cr = vfs_cred(); int err; char last[VFS_MAX_NAME + 1];
    vfs_node_t *parent = vfs_walk(linkpath, true, true, last, cr, &err);
    if (!parent) return err;
    const vfs_node_xops_t *x = nx(parent);
    vfs_node_t *nn = NULL;
    int r = (x && x->symlink) ? x->symlink(parent, last, target, cr, &nn) : VFS_ERR_PERM;
    node_put(parent);
    if (!r) node_put(nn);
    return r;
}
int vfs_readlink2(const char *path, char *buf, size_t sz) {
    int err; vfs_node_t *n = vfs_walk(path, false, false, NULL, vfs_cred(), &err);
    if (!n) return err;
    int r;
    const vfs_node_xops_t *x = nx(n);
    if (n->stat.type != VFS_TYPE_LINK || !x || !x->readlink) r = VFS_ERR_INVAL;
    else r = x->readlink(n, buf, sz);
    node_put(n);
    return r;
}
int vfs_link2(const char *oldp, const char *newp) {
    const vfs_cred_t *cr = vfs_cred(); int err; char last[VFS_MAX_NAME + 1];
    if (vfs_find_mount(oldp) != vfs_find_mount(newp)) return VFS_ERR_XDEV;
    vfs_node_t *t = vfs_walk(oldp, false, false, NULL, cr, &err);
    if (!t) return err;
    vfs_node_t *parent = vfs_walk(newp, true, true, last, cr, &err);
    if (!parent) { node_put(t); return err; }
    const vfs_node_xops_t *x = nx(parent);
    int r = (x && x->link) ? x->link(parent, last, t, cr) : VFS_ERR_PERM;
    node_put(parent); node_put(t);
    return r;
}
int vfs_rename2(const char *oldp, const char *newp, uint32_t flags) {
    if (flags & ~1u) return VFS_ERR_INVAL;
    const vfs_cred_t *cr = vfs_cred(); int err; char on[VFS_MAX_NAME + 1], nn[VFS_MAX_NAME + 1];
    if (vfs_find_mount(oldp) != vfs_find_mount(newp)) return VFS_ERR_XDEV;
    vfs_node_t *od = vfs_walk(oldp, true, true, on, cr, &err); if (!od) return err;
    vfs_node_t *nd = vfs_walk(newp, true, true, nn, cr, &err); if (!nd) { node_put(od); return err; }
    const vfs_node_xops_t *x = nx(od);
    int r;
    if (!x || !x->rename) r = VFS_ERR_UNSUP;
    else {
        r = VFS_OK;
        if (flags & 1u) { vfs_node_t *e = nd->ops->lookup ? nd->ops->lookup(nd, nn) : NULL; if (e) { node_put(e); r = VFS_ERR_EXIST; } }
        if (!r) r = x->rename(od, on, nd, nn, cr);
    }
    node_put(nd); node_put(od);
    return r;
}

int vfs_truncate2(const char *path, uint64_t size) {
    const vfs_cred_t *cr = vfs_cred(); int err;
    vfs_node_t *n = vfs_walk(path, false, true, NULL, cr, &err);
    if (!n) return err;
    int r;
    if (n->stat.type == VFS_TYPE_DIR) r = VFS_ERR_ISDIR;
    else if ((r = node_perm(n, cr, VFS_PERM_W)) == VFS_OK) r = n->ops->truncate ? n->ops->truncate(n, size) : VFS_ERR_UNSUP;
    node_put(n);
    return r;
}
int vfs_ftruncate2(int fd, uint64_t size) {
    vfs_fd_t *f = fd_get(fd); if (!f) return VFS_ERR_BADF;
    if (f->node->stat.type == VFS_TYPE_DIR) return VFS_ERR_ISDIR;
    return f->node->ops->truncate ? f->node->ops->truncate(f->node, size) : VFS_ERR_UNSUP;
}
int vfs_fsync2(int fd) {
    vfs_fd_t *f = fd_get(fd); if (!f) return VFS_ERR_BADF;
    return f->node->ops->sync ? f->node->ops->sync(f->node) : VFS_OK;
}

int vfs_attach(const char *path, vfs_node_t *root, fs_ops_t *fs, blkdev_t *dev, uint32_t flags) {
    spin_lock(&g_mount_lock);
    if (g_mount_count >= VFS_MAX_MOUNTS) { spin_unlock(&g_mount_lock); return VFS_ERR_BUSY; }
    for (int i = 0; i < g_mount_count; i++)
        if (kstr_cmp(g_mounts[i].path, path) == 0) { spin_unlock(&g_mount_lock); return VFS_ERR_BUSY; }
    vfs_mount_t *mnt = &g_mounts[g_mount_count++];
    kstr_cpy(mnt->path, path, VFS_MAX_PATH);
    mnt->root = root; mnt->dev = dev; mnt->fs = fs; mnt->flags = flags;
    root->mount = mnt;
    spin_unlock(&g_mount_lock);
    return VFS_OK;
}

void vfs_sync_all(void) {
    for (int i = 0; i < g_mount_count; i++) {
        vfs_node_t *r = g_mounts[i].root;
        if (r && r->ops && r->ops->sync) r->ops->sync(r);
    }
}
