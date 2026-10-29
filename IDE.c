/* Fexox OS Project, EULA GPLv2 */
 


#include "VFS.h"
#include "debug_out.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

 


extern void *kmalloc(uint64_t size);
extern void  kfree(void *ptr);

typedef uint64_t phys_addr_t;
extern phys_addr_t pmm_alloc_frames_above(uint64_t count, uint64_t align,
                                           phys_addr_t min_phys);
extern void        pmm_free_frames(phys_addr_t pa, uint64_t count);

#ifndef DIRECT_MAP_OFFSET
#define DIRECT_MAP_OFFSET 0xFFFF880000000000ULL
#endif
#define DMA_MIN_PHYS 0x100000ULL

static inline void *dma_alloc(uint64_t size) {
    uint64_t pages = (size + 4095) / 4096;
    phys_addr_t pa = pmm_alloc_frames_above(pages, 1, DMA_MIN_PHYS);
    if (pa == (phys_addr_t)-1) return NULL;
    void *va = (void *)(uintptr_t)(pa + DIRECT_MAP_OFFSET);
    uint8_t *p = (uint8_t *)va;
    for (uint64_t i = 0; i < pages * 4096; i++) p[i] = 0;
    return va;
}
static inline void dma_free(void *va, uint64_t size) {
    if (!va) return;
    uint64_t pages = (size + 4095) / 4096;
    phys_addr_t pa = (phys_addr_t)(uintptr_t)va - DIRECT_MAP_OFFSET;
    pmm_free_frames(pa, pages);
}
static inline uint64_t virt_to_phys(const void *v) {
    uint64_t a = (uint64_t)(uintptr_t)v;
    if (a >= DIRECT_MAP_OFFSET)      return a - DIRECT_MAP_OFFSET;
    if (a >= 0xFFFFFFFF80000000ULL)  return a - 0xFFFFFFFF80000000ULL + 0x200000ULL;
    return a;
}

static inline void *kzalloc(uint64_t size) {
    void *p = kmalloc(size);
    if (p) { uint8_t *b = (uint8_t *)p; for (uint64_t i = 0; i < size; i++) b[i] = 0; }
    return p;
}
static inline void kmem_cpy(void *d, const void *s, uint64_t n) {
    uint8_t *dd = (uint8_t *)d; const uint8_t *ss = (const uint8_t *)s;
    while (n--) *dd++ = *ss++;
}
static inline int kstr_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static inline void kstr_cpy(char *d, const char *s, int max) {
    int i = 0;
    while (i < max-1 && s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
}

 


static inline void     outb(uint16_t p, uint8_t  v) { __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline void     outw(uint16_t p, uint16_t v) { __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
static inline void     outl(uint16_t p, uint32_t v) { __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }
static inline uint8_t  inb (uint16_t p) { uint8_t  v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline uint16_t inw (uint16_t p) { uint16_t v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline uint32_t inl (uint16_t p) { uint32_t v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p)); return v; }

 
static inline void pio_read_buf(uint16_t port, void *buf, uint32_t words) {
    uint16_t *p = (uint16_t *)buf;
    for (uint32_t i = 0; i < words; i++)
        p[i] = inw(port);
}
static inline void pio_write_buf(uint16_t port, const void *buf, uint32_t words) {
    const uint16_t *p = (const uint16_t *)buf;
    for (uint32_t i = 0; i < words; i++)
        outw(port, p[i]);
}

 


static inline uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    uint32_t addr = 0x80000000U | ((uint32_t)bus<<16) | ((uint32_t)dev<<11)
                  | ((uint32_t)fn<<8) | (reg & 0xFC);
    __asm__ volatile("outl %0,%1"::"a"(addr),"Nd"((uint16_t)0xCF8));
    uint32_t v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"((uint16_t)0xCFC));
    return v;
}
static inline void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg, uint32_t v) {
    uint32_t addr = 0x80000000U | ((uint32_t)bus<<16) | ((uint32_t)dev<<11)
                  | ((uint32_t)fn<<8) | (reg & 0xFC);
    __asm__ volatile("outl %0,%1"::"a"(addr),"Nd"((uint16_t)0xCF8));
    __asm__ volatile("outl %0,%1"::"a"(v),   "Nd"((uint16_t)0xCFC));
}
static inline uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    return (uint16_t)(pci_read32(bus,dev,fn,reg&~2) >> ((reg&2)*8));
}

 


#define ATA_REG_DATA        0x00   
#define ATA_REG_ERROR       0x01   
#define ATA_REG_FEATURES    0x01   
#define ATA_REG_SECCOUNT0   0x02   
#define ATA_REG_LBA0        0x03   
#define ATA_REG_LBA1        0x04   
#define ATA_REG_LBA2        0x05   
#define ATA_REG_HDDEVSEL    0x06   
#define ATA_REG_COMMAND     0x07   
#define ATA_REG_STATUS      0x07   
#define ATA_REG_SECCOUNT1   0x08   
#define ATA_REG_LBA3        0x09   
#define ATA_REG_LBA4        0x0A   
#define ATA_REG_LBA5        0x0B   

 
#define ATA_REG_CONTROL     0x00   
#define ATA_REG_ALTSTATUS   0x00   
#define ATA_REG_DEVADDRESS  0x01   

 


#define ATA_SR_BSY  0x80   
#define ATA_SR_DRDY 0x40   
#define ATA_SR_DF   0x20   
#define ATA_SR_DSC  0x10   
#define ATA_SR_DRQ  0x08   
#define ATA_SR_CORR 0x04   
#define ATA_SR_IDX  0x02   
#define ATA_SR_ERR  0x01   

 


#define ATA_CMD_READ_PIO        0x20   
#define ATA_CMD_READ_PIO_EXT    0x24   
#define ATA_CMD_WRITE_PIO       0x30   
#define ATA_CMD_WRITE_PIO_EXT   0x34   
#define ATA_CMD_READ_DMA        0xC8   
#define ATA_CMD_READ_DMA_EXT    0x25   
#define ATA_CMD_WRITE_DMA       0xCA   
#define ATA_CMD_WRITE_DMA_EXT   0x35   
#define ATA_CMD_CACHE_FLUSH     0xE7
#define ATA_CMD_CACHE_FLUSH_EXT 0xEA
#define ATA_CMD_PACKET          0xA0   
#define ATA_CMD_IDENTIFY_PACKET 0xA1   
#define ATA_CMD_IDENTIFY        0xEC   

 


#define BM_REG_CMD    0x00   
#define BM_REG_STATUS 0x02   
#define BM_REG_PRDT   0x04   

 
#define BM_CMD_START  0x01
#define BM_CMD_WRITE  0x00   
#define BM_CMD_READ   0x08   

 
#define BM_SR_ACT     0x01   
#define BM_SR_ERR     0x02   
#define BM_SR_INT     0x04   
#define BM_SR_DRV0    0x20   
#define BM_SR_DRV1    0x40   
#define BM_SR_SIMPLEX 0x80   

 



typedef struct __attribute__((packed)) {
    uint32_t phys_addr;   
    uint16_t byte_count;  
    uint16_t flags;       
} prdt_entry_t;

#define PRDT_EOT  0x8000

 




#define IDE_DMA_MAX_SECTORS 127

 


#define IDE_CHANNEL_PRIMARY   0
#define IDE_CHANNEL_SECONDARY 1

#define IDE_DRIVE_MASTER 0
#define IDE_DRIVE_SLAVE  1

typedef struct {
    uint16_t cmd_base;    
    uint16_t ctrl_base;   
    uint16_t bm_base;     
    bool     bm_avail;    

     
    prdt_entry_t *prdt;   
    uint64_t      prdt_phys;
} ide_channel_t;

typedef struct {
    ide_channel_t *ch;
    uint8_t        drive;        
    bool           lba48;        
    bool           dma_ok;       
    uint64_t       sectors;      
    char           model[41];    
    blkdev_t       blkdev;       
} ide_drive_t;

 
static ide_channel_t g_channels[2];
static ide_drive_t   g_drives[4];    
static int           g_drive_count = 0;

 



 
static inline void ide_delay(ide_channel_t *ch) {
    inb((uint16_t)(ch->ctrl_base + ATA_REG_ALTSTATUS));
    inb((uint16_t)(ch->ctrl_base + ATA_REG_ALTSTATUS));
    inb((uint16_t)(ch->ctrl_base + ATA_REG_ALTSTATUS));
    inb((uint16_t)(ch->ctrl_base + ATA_REG_ALTSTATUS));
}

 



static uint8_t ide_wait_not_busy(ide_channel_t *ch, uint32_t timeout_loops) {
    uint8_t s;
    for (uint32_t i = 0; i < timeout_loops; i++) {
        s = inb((uint16_t)(ch->ctrl_base + ATA_REG_ALTSTATUS));
        if (!(s & ATA_SR_BSY)) return s;
        __asm__ volatile("pause":::"memory");
    }
    return 0xFF;   
}

 



static uint8_t ide_wait_drq(ide_channel_t *ch, uint32_t timeout_loops) {
    uint8_t s;
    for (uint32_t i = 0; i < timeout_loops; i++) {
        s = inb((uint16_t)(ch->ctrl_base + ATA_REG_ALTSTATUS));
        if (!(s & ATA_SR_BSY) && (s & ATA_SR_DRQ)) return s;
        if (s & ATA_SR_ERR) return s;
        __asm__ volatile("pause":::"memory");
    }
    return 0xFF;
}

 


static void ide_select_drive(ide_channel_t *ch, uint8_t drive) {
     
    outb((uint16_t)(ch->cmd_base + ATA_REG_HDDEVSEL),
         (uint8_t)(0xA0 | (drive << 4)));
    ide_delay(ch);
}

 


 



static bool ide_identify(ide_channel_t *ch, uint8_t drive,
                         uint16_t *buf_256words) {
     
    ide_select_drive(ch, drive);

     
    outb((uint16_t)(ch->cmd_base + ATA_REG_SECCOUNT0), 0);
    outb((uint16_t)(ch->cmd_base + ATA_REG_LBA0),      0);
    outb((uint16_t)(ch->cmd_base + ATA_REG_LBA1),      0);
    outb((uint16_t)(ch->cmd_base + ATA_REG_LBA2),      0);

     
    outb((uint16_t)(ch->cmd_base + ATA_REG_COMMAND), ATA_CMD_IDENTIFY);
    ide_delay(ch);

     
    uint8_t s = inb((uint16_t)(ch->ctrl_base + ATA_REG_ALTSTATUS));
    if (s == 0x00) return false;

     
    s = ide_wait_not_busy(ch, 100000);
    if (s == 0xFF) {
        DBG_MSG("IDE", "identify: timeout waiting BSY=0");
        return false;
    }

     
    uint8_t cl = inb((uint16_t)(ch->cmd_base + ATA_REG_LBA1));
    uint8_t ch2 = inb((uint16_t)(ch->cmd_base + ATA_REG_LBA2));
    if (cl == 0x14 && ch2 == 0xEB) {
        DBG_MSG("IDE", "identify: ATAPI device, skipping");
        return false;
    }
    if (cl != 0x00 || ch2 != 0x00) {
        DBG_VAL("IDE", "identify: unknown signature cl", cl);
        return false;
    }

     
    s = ide_wait_drq(ch, 100000);
    if (s == 0xFF || (s & ATA_SR_ERR)) {
        DBG_VAL("IDE", "identify: error waiting DRQ, status", s);
        return false;
    }

     
    pio_read_buf((uint16_t)(ch->cmd_base + ATA_REG_DATA), buf_256words, 256);

     
    inb((uint16_t)(ch->cmd_base + ATA_REG_STATUS));

    return true;
}

 
static void ide_copy_string(char *dst, const uint16_t *words,
                             int word_start, int char_len) {
    int j = 0;
    for (int i = 0; i < char_len / 2; i++) {
        uint16_t w = words[word_start + i];
        dst[j++] = (char)(w >> 8);
        dst[j++] = (char)(w & 0xFF);
    }
     
    int last = char_len - 1;
    while (last >= 0 && dst[last] == ' ') last--;
    dst[last + 1] = '\0';
}

 



 



static void ide_setup_lba(ide_channel_t *ch, uint8_t drive,
                           uint64_t lba, uint16_t count, bool lba48) {
    if (lba48) {
         
        outb((uint16_t)(ch->cmd_base + ATA_REG_HDDEVSEL),
             (uint8_t)(0x40 | (drive << 4)));   
        ide_delay(ch);

         
        outb((uint16_t)(ch->cmd_base + ATA_REG_SECCOUNT0), (uint8_t)(count >> 8));
        outb((uint16_t)(ch->cmd_base + ATA_REG_LBA0),      (uint8_t)(lba >> 24));
        outb((uint16_t)(ch->cmd_base + ATA_REG_LBA1),      (uint8_t)(lba >> 32));
        outb((uint16_t)(ch->cmd_base + ATA_REG_LBA2),      (uint8_t)(lba >> 40));

         
        outb((uint16_t)(ch->cmd_base + ATA_REG_SECCOUNT0), (uint8_t)(count & 0xFF));
        outb((uint16_t)(ch->cmd_base + ATA_REG_LBA0),      (uint8_t)(lba & 0xFF));
        outb((uint16_t)(ch->cmd_base + ATA_REG_LBA1),      (uint8_t)(lba >> 8));
        outb((uint16_t)(ch->cmd_base + ATA_REG_LBA2),      (uint8_t)(lba >> 16));
    } else {
         
        outb((uint16_t)(ch->cmd_base + ATA_REG_HDDEVSEL),
             (uint8_t)(0xE0 | (drive << 4) | ((lba >> 24) & 0x0F)));
        ide_delay(ch);

        outb((uint16_t)(ch->cmd_base + ATA_REG_SECCOUNT0), (uint8_t)(count & 0xFF));
        outb((uint16_t)(ch->cmd_base + ATA_REG_LBA0),      (uint8_t)(lba & 0xFF));
        outb((uint16_t)(ch->cmd_base + ATA_REG_LBA1),      (uint8_t)(lba >> 8));
        outb((uint16_t)(ch->cmd_base + ATA_REG_LBA2),      (uint8_t)(lba >> 16));
    }
}

static int ide_pio_read(ide_drive_t *drv, uint64_t lba,
                        uint32_t count, void *buf) {
    ide_channel_t *ch  = drv->ch;
    uint8_t       *dst = (uint8_t *)buf;
    bool lba48 = drv->lba48 && (lba >= 0x10000000ULL || count > 255);

     
    uint8_t s = ide_wait_not_busy(ch, 10000000);
    if (s == 0xFF || (s & ATA_SR_ERR)) {
        DBG_VAL("IDE", "pio_read: drive not ready, status", s);
        return VFS_ERR_IO;
    }

     
    ide_setup_lba(ch, drv->drive, lba,
                  (uint16_t)(lba48 ? count : (count & 0xFF)), lba48);

     
    uint8_t cmd = lba48 ? ATA_CMD_READ_PIO_EXT : ATA_CMD_READ_PIO;
    outb((uint16_t)(ch->cmd_base + ATA_REG_COMMAND), cmd);

     
    for (uint32_t i = 0; i < count; i++) {
        s = ide_wait_drq(ch, 10000000);
        if (s == 0xFF || (s & ATA_SR_ERR)) {
            DBG_VAL("IDE", "pio_read: DRQ timeout/error at sector", i);
            DBG_VAL("IDE", "  status", s);
            DBG_VAL("IDE", "  error",  inb((uint16_t)(ch->cmd_base + ATA_REG_ERROR)));
            return VFS_ERR_IO;
        }
        pio_read_buf((uint16_t)(ch->cmd_base + ATA_REG_DATA),
                     dst + (uint64_t)i * 512, 256);   
         
        ide_delay(ch);
    }

    return VFS_OK;
}

static int ide_pio_write(ide_drive_t *drv, uint64_t lba,
                         uint32_t count, const void *buf) {
    ide_channel_t  *ch  = drv->ch;
    const uint8_t  *src = (const uint8_t *)buf;
    bool lba48 = drv->lba48 && (lba >= 0x10000000ULL || count > 255);

    uint8_t s = ide_wait_not_busy(ch, 10000000);
    if (s == 0xFF || (s & ATA_SR_ERR)) {
        DBG_VAL("IDE", "pio_write: drive not ready", s);
        return VFS_ERR_IO;
    }

    ide_setup_lba(ch, drv->drive, lba,
                  (uint16_t)(lba48 ? count : (count & 0xFF)), lba48);

    uint8_t cmd = lba48 ? ATA_CMD_WRITE_PIO_EXT : ATA_CMD_WRITE_PIO;
    outb((uint16_t)(ch->cmd_base + ATA_REG_COMMAND), cmd);

    for (uint32_t i = 0; i < count; i++) {
        s = ide_wait_drq(ch, 10000000);
        if (s == 0xFF || (s & ATA_SR_ERR)) {
            DBG_VAL("IDE", "pio_write: DRQ error at sector", i);
            return VFS_ERR_IO;
        }
        pio_write_buf((uint16_t)(ch->cmd_base + ATA_REG_DATA),
                      src + (uint64_t)i * 512, 256);
        ide_delay(ch);
    }

     
    outb((uint16_t)(ch->cmd_base + ATA_REG_COMMAND),
         lba48 ? ATA_CMD_CACHE_FLUSH_EXT : ATA_CMD_CACHE_FLUSH);
    ide_wait_not_busy(ch, 10000000);

    return VFS_OK;
}

 














static int ide_dma_rw(ide_drive_t *drv, uint64_t lba,
                      uint32_t count, void *buf, bool write) {
    ide_channel_t *ch = drv->ch;

    if (!ch->bm_avail || !drv->dma_ok) {
         
        return write ? ide_pio_write(drv, lba, count, buf)
                     : ide_pio_read (drv, lba, count, buf);
    }

     
    if (count > IDE_DMA_MAX_SECTORS) count = IDE_DMA_MAX_SECTORS;

    uint32_t byte_count = count * 512;

     
    uint8_t *dma_buf = (uint8_t *)dma_alloc(byte_count + 4096);
    if (!dma_buf) {
        DBG_MSG("IDE", "dma_rw: dma_alloc failed, fallback PIO");
        return write ? ide_pio_write(drv, lba, count, buf)
                     : ide_pio_read (drv, lba, count, buf);
    }

     
    uint64_t raw_phys  = virt_to_phys(dma_buf);
    uint64_t aligned   = (raw_phys + 65535) & ~(uint64_t)65535;
    uint8_t *aligned_v = (uint8_t *)(uintptr_t)(aligned + DIRECT_MAP_OFFSET);

     
    if ((aligned & 0xFFFF) + byte_count > 0x10000) {
         
        dma_free(dma_buf, byte_count + 4096);
        return write ? ide_pio_write(drv, lba, count, buf)
                     : ide_pio_read (drv, lba, count, buf);
    }

     
    if (write) {
        uint8_t *src = (uint8_t *)buf;
        for (uint32_t i = 0; i < byte_count; i++) aligned_v[i] = src[i];
         
        __asm__ volatile("mfence":::"memory");
    }

     
    ch->prdt[0].phys_addr  = (uint32_t)aligned;
    ch->prdt[0].byte_count = (uint16_t)(byte_count == 0x10000 ? 0 : byte_count);
    ch->prdt[0].flags      = PRDT_EOT;
    __asm__ volatile("mfence":::"memory");

     
    outl((uint16_t)(ch->bm_base + BM_REG_PRDT), (uint32_t)ch->prdt_phys);

     
    uint8_t bm_cmd = write ? BM_CMD_READ : BM_CMD_WRITE;
    outb((uint16_t)(ch->bm_base + BM_REG_CMD), bm_cmd);

     
    outb((uint16_t)(ch->bm_base + BM_REG_STATUS),
         (uint8_t)(BM_SR_INT | BM_SR_ERR));

     
    uint8_t s = ide_wait_not_busy(ch, 10000000);
    if (s == 0xFF || (s & ATA_SR_ERR)) {
        DBG_VAL("IDE", "dma_rw: drive not ready", s);
        dma_free(dma_buf, byte_count + 4096);
        return VFS_ERR_IO;
    }

     
    bool lba48 = drv->lba48 && (lba >= 0x10000000ULL || count > 255);
    ide_setup_lba(ch, drv->drive, lba,
                  (uint16_t)(lba48 ? count : (count & 0xFF)), lba48);

     
    uint8_t cmd;
    if (write) cmd = lba48 ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_WRITE_DMA;
    else       cmd = lba48 ? ATA_CMD_READ_DMA_EXT  : ATA_CMD_READ_DMA;
    outb((uint16_t)(ch->cmd_base + ATA_REG_COMMAND), cmd);

     
    outb((uint16_t)(ch->bm_base + BM_REG_CMD),
         (uint8_t)(bm_cmd | BM_CMD_START));

     
    uint8_t bm_sr;
    uint32_t spin = 0;
    while (1) {
        bm_sr = inb((uint16_t)(ch->bm_base + BM_REG_STATUS));
        if (bm_sr & (BM_SR_INT | BM_SR_ERR)) break;
        __asm__ volatile("pause":::"memory");
        spin++;
        if (spin > 50000000) {
            DBG_MSG("IDE", "dma_rw: TIMEOUT");
            outb((uint16_t)(ch->bm_base + BM_REG_CMD), bm_cmd);   
            dma_free(dma_buf, byte_count + 4096);
            return VFS_ERR_IO;
        }
    }

     
    outb((uint16_t)(ch->bm_base + BM_REG_CMD), bm_cmd);   

     
    if (bm_sr & BM_SR_ERR) {
        DBG_VAL("IDE", "dma_rw: Bus Master error, bm_status", bm_sr);
        outb((uint16_t)(ch->bm_base + BM_REG_STATUS),
             (uint8_t)(BM_SR_INT | BM_SR_ERR));
        dma_free(dma_buf, byte_count + 4096);
        return VFS_ERR_IO;
    }

    s = inb((uint16_t)(ch->cmd_base + ATA_REG_STATUS));
    if (s & (ATA_SR_ERR | ATA_SR_DF)) {
        DBG_VAL("IDE", "dma_rw: ATA error, status", s);
        outb((uint16_t)(ch->bm_base + BM_REG_STATUS),
             (uint8_t)(BM_SR_INT | BM_SR_ERR));
        dma_free(dma_buf, byte_count + 4096);
        return VFS_ERR_IO;
    }

     
    outb((uint16_t)(ch->bm_base + BM_REG_STATUS),
         (uint8_t)(BM_SR_INT | BM_SR_ERR));

     
    if (!write) {
        __asm__ volatile("mfence":::"memory");
        uint8_t *dst = (uint8_t *)buf;
        for (uint32_t i = 0; i < byte_count; i++) dst[i] = aligned_v[i];
    }

    dma_free(dma_buf, byte_count + 4096);
    return VFS_OK;
}

 


static int ide_blkdev_read(blkdev_t *dev, uint64_t lba,
                            uint32_t count, void *buf) {
    ide_drive_t *drv = (ide_drive_t *)dev->priv;
    if (lba + count > drv->sectors) return VFS_ERR_OVERFLOW;

    uint8_t *dst = (uint8_t *)buf;
    while (count > 0) {
        uint32_t n = count;
        if (n > IDE_DMA_MAX_SECTORS) n = IDE_DMA_MAX_SECTORS;
        int r = ide_dma_rw(drv, lba, n, dst, false);
        if (r != VFS_OK) return r;
        lba   += n;
        dst   += (uint64_t)n * 512;
        count -= n;
    }
    return VFS_OK;
}

static int ide_blkdev_write(blkdev_t *dev, uint64_t lba,
                             uint32_t count, const void *buf) {
    ide_drive_t *drv = (ide_drive_t *)dev->priv;
    if (dev->readonly)           return VFS_ERR_ROFS;
    if (lba + count > drv->sectors) return VFS_ERR_OVERFLOW;

    const uint8_t *src = (const uint8_t *)buf;
    while (count > 0) {
        uint32_t n = count;
        if (n > IDE_DMA_MAX_SECTORS) n = IDE_DMA_MAX_SECTORS;
        int r = ide_dma_rw(drv, lba, n, (void *)src, true);
        if (r != VFS_OK) return r;
        lba   += n;
        src   += (uint64_t)n * 512;
        count -= n;
    }
    return VFS_OK;
}

static void ide_blkdev_destroy(blkdev_t *dev) {
    (void)dev;
     
}

static blkdev_ops_t g_ide_ops = {
    .read_sectors  = ide_blkdev_read,
    .write_sectors = ide_blkdev_write,
    .destroy       = ide_blkdev_destroy,
};

 


static void ide_probe_drive(ide_channel_t *ch, uint8_t drive_num,
                             int channel_idx, int drive_idx) {
     
    static const char *drive_names[4] = {"hda","hdb","hdc","hdd"};
    int idx = channel_idx * 2 + drive_idx;
    if (idx >= 4) return;

    ide_drive_t *drv = &g_drives[g_drive_count];

     
    uint16_t id_buf[256];

    DBG_VAL("IDE", "probing channel", channel_idx);
    DBG_VAL("IDE", "probing drive",   drive_num);

    if (!ide_identify(ch, drive_num, id_buf)) {
        DBG_MSG("IDE", "  no device / ATAPI, skip");
        return;
    }

    drv->ch    = ch;
    drv->drive = drive_num;

     
    drv->lba48 = !!(id_buf[83] & (1 << 10));

     
    if (drv->lba48) {
        drv->sectors  = (uint64_t)id_buf[100];
        drv->sectors |= (uint64_t)id_buf[101] << 16;
        drv->sectors |= (uint64_t)id_buf[102] << 32;
        drv->sectors |= (uint64_t)id_buf[103] << 48;
    } else {
        drv->sectors  = (uint32_t)id_buf[60];
        drv->sectors |= (uint32_t)id_buf[61] << 16;
    }

     
    bool dma_supported = !!(id_buf[49] & (1 << 8));
    drv->dma_ok = dma_supported && ch->bm_avail;

     
    ide_copy_string(drv->model, id_buf, 27, 40);

     
    kstr_cpy(drv->blkdev.name, drive_names[idx], 32);
    drv->blkdev.sector_size  = 512;
    drv->blkdev.sector_count = drv->sectors;
    drv->blkdev.priv         = drv;
    drv->blkdev.ops          = &g_ide_ops;
    drv->blkdev.readonly     = false;

    dbg_puts("[IDE] found: "); dbg_puts(drive_names[idx]);
    dbg_puts(" model=\""); dbg_puts(drv->model); dbg_puts("\"");
    DBG_VAL("IDE", "  sectors", drv->sectors);
    DBG_VAL("IDE", "  lba48",   drv->lba48);
    DBG_VAL("IDE", "  dma_ok",  drv->dma_ok);

    int r = blkdev_register(&drv->blkdev);
    if (r < 0) {
        DBG_VAL("IDE", "  blkdev_register failed", (uint64_t)(int64_t)r);
        return;
    }

    g_drive_count++;
    DBG_MSG("IDE", "  registered ok");
}

 



 



static uint16_t ide_find_pci_bm_base(void) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            uint32_t id = pci_read32((uint8_t)bus, dev, 0, 0x00);
            if (id == 0xFFFFFFFF) continue;

            uint32_t cls = pci_read32((uint8_t)bus, dev, 0, 0x08);
            uint8_t class_code = (uint8_t)(cls >> 24);
            uint8_t subclass   = (uint8_t)(cls >> 16);
            uint8_t prog_if    = (uint8_t)(cls >> 8);

            if (class_code != 0x01 || subclass != 0x01) continue;

            DBG_VAL("IDE", "found PCI IDE controller bus", bus);
            DBG_VAL("IDE", "  device",  dev);
            DBG_VAL("IDE", "  prog_if", prog_if);

             
            uint32_t cmd = pci_read32((uint8_t)bus, dev, 0, 0x04);
            pci_write32((uint8_t)bus, dev, 0, 0x04, cmd | 0x05);

             
            uint32_t bar4 = pci_read32((uint8_t)bus, dev, 0, 0x20);
            if (bar4 & 1) {
                uint16_t bm_base = (uint16_t)(bar4 & ~3);
                DBG_VAL("IDE", "  bm_base (BAR4)", bm_base);
                return bm_base;
            }

            DBG_MSG("IDE", "  BAR4 not IO space, DMA disabled");
            return 0;
        }
    }
    DBG_MSG("IDE", "no PCI IDE controller found, DMA disabled");
    return 0;
}

 


int ide_init(void) {
    DBG_MSG("IDE", "=== ide_init ===");

     
    uint16_t bm_base = ide_find_pci_bm_base();

     
    g_channels[IDE_CHANNEL_PRIMARY].cmd_base  = 0x1F0;
    g_channels[IDE_CHANNEL_PRIMARY].ctrl_base = 0x3F6;
    g_channels[IDE_CHANNEL_PRIMARY].bm_base   = (uint16_t)(bm_base + 0x00);
    g_channels[IDE_CHANNEL_PRIMARY].bm_avail  = (bm_base != 0);

    g_channels[IDE_CHANNEL_SECONDARY].cmd_base  = 0x170;
    g_channels[IDE_CHANNEL_SECONDARY].ctrl_base = 0x376;
    g_channels[IDE_CHANNEL_SECONDARY].bm_base   = (uint16_t)(bm_base + 0x08);
    g_channels[IDE_CHANNEL_SECONDARY].bm_avail  = (bm_base != 0);

     
    for (int c = 0; c < 2; c++) {
        ide_channel_t *ch = &g_channels[c];
        ch->prdt = (prdt_entry_t *)dma_alloc(4096);
        if (!ch->prdt) {
            DBG_VAL("IDE", "dma_alloc PRDT failed for channel", c);
            ch->bm_avail = false;
            continue;
        }
        ch->prdt_phys = virt_to_phys(ch->prdt);
        if (ch->prdt_phys > 0xFFFFFFFFULL) {
            DBG_MSG("IDE", "PRDT phys > 4GB, DMA disabled for channel");
            dma_free(ch->prdt, 4096);
            ch->prdt = NULL;
            ch->bm_avail = false;
        }
    }

     



    for (int c = 0; c < 2; c++) {
        ide_channel_t *ch = &g_channels[c];
         
        outb((uint16_t)(ch->ctrl_base + ATA_REG_CONTROL), 0x06);
         
        for (volatile uint32_t d = 0; d < 500000; d++) __asm__ volatile("pause");
         
        outb((uint16_t)(ch->ctrl_base + ATA_REG_CONTROL), 0x02);
        for (volatile uint32_t d = 0; d < 500000; d++) __asm__ volatile("pause");
    }

     
    for (int c = 0; c < 2; c++) {
        ide_probe_drive(&g_channels[c], IDE_DRIVE_MASTER, c, 0);
        ide_probe_drive(&g_channels[c], IDE_DRIVE_SLAVE,  c, 1);
    }

    DBG_VAL("IDE", "total drives found", g_drive_count);

    if (g_drive_count == 0) {
        DBG_MSG("IDE", "no ATA drives detected");
        return VFS_ERR_NXDEV;
    }

    DBG_MSG("IDE", "=== ide_init done ===");
    return VFS_OK;
}

 




blkdev_t *ide_find_drive(const char *name) {
    for (int i = 0; i < g_drive_count; i++) {
        if (kstr_cmp(g_drives[i].blkdev.name, name) == 0)
            return &g_drives[i].blkdev;
    }
    return NULL;
}
