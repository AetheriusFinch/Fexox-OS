/* Fexox OS Project, EULA GPLv2 */

#include "USBMassStorage.h"
#include "USB.h"
#include "VFS.h"
#include "debug_out.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

extern void  *memset(void *dst, int c, size_t n);
extern void  *memcpy(void *dst, const void *src, size_t n);
extern size_t strlcpy(char *dst, const char *src, size_t n);    

 


#define USB_MSC_CBW_SIG 0x43425355u    
#define USB_MSC_CSW_SIG 0x53425355u    

#pragma pack(push, 1)
typedef struct {
    uint32_t dCBWSignature;
    uint32_t dCBWTag;
    uint32_t dCBWDataTransferLength;
    uint8_t  bmCBWFlags;    
    uint8_t  bCBWLUN;       
    uint8_t  bCBWCBLength;  
    uint8_t  CBWCB[16];
} usb_msc_cbw_t;    

typedef struct {
    uint32_t dCSWSignature;
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t  bCSWStatus;    
} usb_msc_csw_t;    
#pragma pack(pop)

 


#define USB_MSC_MAX_DEVICES 8

typedef struct {
    bool       used;
    usb_hcd_t *hcd;
    uint8_t    addr, speed;

    uint8_t    bulk_in_ep,  bulk_out_ep;
    uint16_t   bulk_in_maxp, bulk_out_maxp;
    uint8_t    toggle_in, toggle_out;

    uint32_t   tag;
    uint64_t   sector_count;
    uint32_t   sector_size;

    blkdev_t   blkdev;
} usb_msc_dev_t;

static usb_msc_dev_t g_msc[USB_MSC_MAX_DEVICES];
static int            g_msc_count = 0;

 


static int msc_command(usb_msc_dev_t *d, const uint8_t *cb, uint8_t cblen,
                       void *data, uint32_t data_len, bool dir_in) {
    usb_msc_cbw_t cbw;
    memset(&cbw, 0, sizeof(cbw));
    cbw.dCBWSignature          = USB_MSC_CBW_SIG;
    cbw.dCBWTag                = ++d->tag;
    cbw.dCBWDataTransferLength = data_len;
    cbw.bmCBWFlags             = dir_in ? 0x80u : 0x00u;
    cbw.bCBWLUN                = 0;
    cbw.bCBWCBLength           = cblen;
    memcpy(cbw.CBWCB, cb, cblen);

    int r = d->hcd->ops->bulk_transfer(d->hcd, d->addr, d->speed, d->bulk_out_ep,
                                       d->bulk_out_maxp, &cbw, sizeof(cbw), &d->toggle_out);
    if (r != 0) return r;

    if (data_len > 0) {
        uint8_t   ep     = dir_in ? d->bulk_in_ep     : d->bulk_out_ep;
        uint16_t  maxp   = dir_in ? d->bulk_in_maxp   : d->bulk_out_maxp;
        uint8_t  *toggle = dir_in ? &d->toggle_in     : &d->toggle_out;
        r = d->hcd->ops->bulk_transfer(d->hcd, d->addr, d->speed, ep, maxp, data, data_len, toggle);
        if (r != 0) return r;
    }

    usb_msc_csw_t csw;
    memset(&csw, 0, sizeof(csw));
    r = d->hcd->ops->bulk_transfer(d->hcd, d->addr, d->speed, d->bulk_in_ep,
                                   d->bulk_in_maxp, &csw, sizeof(csw), &d->toggle_in);
    if (r != 0) return r;

     



    if (csw.dCSWSignature != USB_MSC_CSW_SIG || csw.dCSWTag != cbw.dCBWTag) return -22;
    if (csw.bCSWStatus == 1) return -20;    
    if (csw.bCSWStatus != 0) return -21;    
    return 0;
}

 


 



static struct { uint32_t errors, recovered; int last_rc; uint64_t last_lba; } g_msc_stat;

void usb_msc_get_stats(uint32_t *errors, uint32_t *recovered, int *last_rc, uint64_t *last_lba) {
    if (errors)    *errors    = g_msc_stat.errors;
    if (recovered) *recovered = g_msc_stat.recovered;
    if (last_rc)   *last_rc   = g_msc_stat.last_rc;
    if (last_lba)  *last_lba  = g_msc_stat.last_lba;
}

static void msc_request_sense(usb_msc_dev_t *d);
static void msc_reset_recovery(usb_msc_dev_t *d);

 






static int msc_command_retry(usb_msc_dev_t *d, const uint8_t *cb, uint8_t cblen,
                             void *data, uint32_t len, bool dir_in, uint64_t lba) {
    int r = 0;
    for (int attempt = 0; attempt < 4; attempt++) {
        r = msc_command(d, cb, cblen, data, len, dir_in);
        if (r == 0) {
            if (attempt) g_msc_stat.recovered++;
            return 0;
        }
        g_msc_stat.errors++;
        g_msc_stat.last_rc  = r;
        g_msc_stat.last_lba = lba;
        if (r == -20) msc_request_sense(d);
        else          msc_reset_recovery(d);
        usb_delay_ms(20u * (uint32_t)(attempt + 1));
    }
    return r;
}

static int usb_msc_blkdev_read(blkdev_t *dev, uint64_t lba, uint32_t count, void *buf) {
    usb_msc_dev_t *d = (usb_msc_dev_t *)dev->priv;
    if (lba + count > d->sector_count) return VFS_ERR_OVERFLOW;

    uint32_t sec_sz = d->sector_size ? d->sector_size : 512u;
    uint32_t max_sectors = 65536u / sec_sz;    
    if (max_sectors == 0) max_sectors = 1;

    uint8_t *dst = (uint8_t *)buf;
    while (count > 0) {
        uint32_t n = count > max_sectors ? max_sectors : count;

        uint8_t cb[10];
        memset(cb, 0, sizeof(cb));
        cb[0] = 0x28;    
        cb[2] = (uint8_t)(lba >> 24); cb[3] = (uint8_t)(lba >> 16);
        cb[4] = (uint8_t)(lba >> 8);  cb[5] = (uint8_t)lba;
        cb[7] = (uint8_t)(n >> 8);    cb[8] = (uint8_t)n;

        if (msc_command_retry(d, cb, 10, dst, n * sec_sz, true, lba) != 0) return VFS_ERR_IO;

        lba   += n;
        dst   += (uint64_t)n * sec_sz;
        count -= n;
    }
    return VFS_OK;
}

static int usb_msc_blkdev_write(blkdev_t *dev, uint64_t lba, uint32_t count, const void *buf) {
    usb_msc_dev_t *d = (usb_msc_dev_t *)dev->priv;
    if (dev->readonly)             return VFS_ERR_ROFS;
    if (lba + count > d->sector_count) return VFS_ERR_OVERFLOW;

    uint32_t sec_sz = d->sector_size ? d->sector_size : 512u;
    uint32_t max_sectors = 65536u / sec_sz;
    if (max_sectors == 0) max_sectors = 1;

    const uint8_t *src = (const uint8_t *)buf;
    while (count > 0) {
        uint32_t n = count > max_sectors ? max_sectors : count;

        uint8_t cb[10];
        memset(cb, 0, sizeof(cb));
        cb[0] = 0x2A;    
        cb[2] = (uint8_t)(lba >> 24); cb[3] = (uint8_t)(lba >> 16);
        cb[4] = (uint8_t)(lba >> 8);  cb[5] = (uint8_t)lba;
        cb[7] = (uint8_t)(n >> 8);    cb[8] = (uint8_t)n;

        if (msc_command_retry(d, cb, 10, (void *)src, n * sec_sz, false, lba) != 0) return VFS_ERR_IO;

        lba   += n;
        src   += (uint64_t)n * sec_sz;
        count -= n;
    }
    return VFS_OK;
}

static void usb_msc_blkdev_destroy(blkdev_t *dev) { (void)dev; }

static blkdev_ops_t g_usb_msc_ops = {
    .read_sectors  = usb_msc_blkdev_read,
    .write_sectors = usb_msc_blkdev_write,
    .destroy       = usb_msc_blkdev_destroy,
};

 



 


static void msc_request_sense(usb_msc_dev_t *d) {
    uint8_t cb[6] = { 0x03, 0, 0, 0, 18, 0 };
    uint8_t buf[18];
    memset(buf, 0, sizeof(buf));
    msc_command(d, cb, 6, buf, sizeof(buf), true);
}

 



static void msc_reset_recovery(usb_msc_dev_t *d) {
    if (d->speed != USB_SPEED_HIGH) return;
    usb_setup_pkt_t sp;

    sp.bmRequestType = USB_REQ_DIR_H2D | USB_REQ_TYPE_CLASS | USB_REQ_RECIP_INTERFACE;
    sp.bRequest = 0xFF; sp.wValue = 0; sp.wIndex = 0; sp.wLength = 0;    
    d->hcd->ops->control_transfer(d->hcd, d->addr, d->speed, 64, &sp, NULL, 0);

    uint8_t eps[2] = { d->bulk_in_ep, d->bulk_out_ep };
    for (int i = 0; i < 2; i++) {
        sp.bmRequestType = USB_REQ_DIR_H2D | USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_ENDPOINT;
        sp.bRequest = USB_REQ_CLEAR_FEATURE; sp.wValue = 0; sp.wIndex = eps[i]; sp.wLength = 0;
        d->hcd->ops->control_transfer(d->hcd, d->addr, d->speed, 64, &sp, NULL, 0);
    }
    d->toggle_in = 0;
    d->toggle_out = 0;
}

 


int usb_msc_attach(usb_hcd_t *hcd, uint8_t addr, uint8_t speed,
                   uint8_t bulk_in_ep, uint16_t bulk_in_maxp,
                   uint8_t bulk_out_ep, uint16_t bulk_out_maxp) {
    if (!hcd->ops->bulk_transfer) {
        DBG_MSG("USBMSC", "HCD не поддерживает bulk_transfer — пропускаем");
        return -1;
    }
    if (g_msc_count >= USB_MSC_MAX_DEVICES) {
        DBG_MSG("USBMSC", "лимит устройств исчерпан (USB_MSC_MAX_DEVICES)");
        return -1;
    }

    usb_msc_dev_t *d = &g_msc[g_msc_count];
    memset(d, 0, sizeof(*d));
    d->used          = true;
    d->hcd           = hcd;
    d->addr          = addr;
    d->speed         = speed;
    d->bulk_in_ep    = bulk_in_ep;
    d->bulk_in_maxp  = bulk_in_maxp;
    d->bulk_out_ep   = bulk_out_ep;
    d->bulk_out_maxp = bulk_out_maxp;

     




    bool ready = false;
    int  xport_errs = 0;    
    for (int i = 0; i < 25 && !ready; i++) {
        uint8_t tur[6];
        memset(tur, 0, sizeof(tur));
        int r = msc_command(d, tur, 6, NULL, 0, true);
        if (r == 0) { ready = true; break; }
        if (r == -20)      msc_request_sense(d);       
        else if (r > -10) {                            
            msc_reset_recovery(d);
            if (++xport_errs >= 3) break;
        }
        usb_delay_ms(100);
    }
    DBG_VAL("USBMSC", "unit ready (1=yes)", ready ? 1 : 0);

    uint8_t cap[8];
    uint8_t rc10[10];
    int cap_rc = -1;
    for (int i = 0; i < 5 && cap_rc != 0; i++) {
        memset(cap, 0, sizeof(cap));
        memset(rc10, 0, sizeof(rc10));
        rc10[0] = 0x25;    
        cap_rc = msc_command(d, rc10, 10, cap, sizeof(cap), true);
        if (cap_rc == -20)      msc_request_sense(d);
        else if (cap_rc != 0 && cap_rc > -10) {
            msc_reset_recovery(d);
            if (++xport_errs >= 5) break;
        }
        if (cap_rc != 0) usb_delay_ms(100);
    }
    if (cap_rc != 0) {
        DBG_VAL("USBMSC", "READ CAPACITY(10) не удалась, addr", addr);
        return -1;
    }

    uint32_t last_lba  = ((uint32_t)cap[0] << 24) | ((uint32_t)cap[1] << 16) |
                         ((uint32_t)cap[2] << 8)  |  (uint32_t)cap[3];
    uint32_t block_len = ((uint32_t)cap[4] << 24) | ((uint32_t)cap[5] << 16) |
                         ((uint32_t)cap[6] << 8)  |  (uint32_t)cap[7];
    if (block_len == 0) block_len = 512;

    d->sector_count = (uint64_t)last_lba + 1u;
    d->sector_size  = block_len;

    char name[8];
    name[0] = 'u'; name[1] = 'd'; name[2] = (char)('a' + g_msc_count); name[3] = '\0';

    strlcpy(d->blkdev.name, name, sizeof(d->blkdev.name));
    d->blkdev.sector_size  = d->sector_size;
    d->blkdev.sector_count = d->sector_count;
    d->blkdev.priv         = d;
    d->blkdev.ops          = &g_usb_msc_ops;
    d->blkdev.readonly     = false;

    dbg_puts("[USBMSC] "); dbg_puts(name); dbg_puts("\n");
    DBG_VAL("USBMSC", "  sector_size",  d->sector_size);
    DBG_VAL("USBMSC", "  sector_count", d->sector_count);

    int r = blkdev_register(&d->blkdev);
    if (r < 0) {
        DBG_VAL("USBMSC", "blkdev_register failed", (uint64_t)(int64_t)r);
        return -1;
    }

    g_msc_count++;
    return 0;
}
