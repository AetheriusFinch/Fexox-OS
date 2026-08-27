/*
 * USBMassStorage.c — класс-драйвер USB Mass Storage, Bulk-Only Transport
 * (USB Mass Storage Class – Bulk Only Transport, rev 1.0) поверх SCSI
 * transparent command set (bInterfaceSubClass==0x06 "SCSI transparent",
 * bInterfaceProtocol==0x50 "Bulk-Only").
 *
 * Протокол BOT для одной команды — три bulk-транзакции подряд:
 *   1. CBW (Command Block Wrapper, 31 байт) -> bulk OUT
 *   2. DATA (опционально, размер из самой команды) -> bulk IN или OUT
 *   3. CSW (Command Status Wrapper, 13 байт) <- bulk IN
 * Используется usb_hcd_ops_t.bulk_transfer (см. USB.h, реализован в
 * UHCI.c/EHCI.c) — Data Toggle у bulk-эндпоинтов персистентный между
 * вызовами, поэтому в usb_msc_dev_t хранится toggle_in/toggle_out.
 *
 * Регистрируется как обычный blkdev (см. VFS.h — тот же интерфейс, что у
 * AHCI.c/IDE.c рядом), поэтому FAT32/VFS работают с USB-флешкой ровно так
 * же, как с SATA/IDE диском — разницы для остального ядра нет.
 *
 * Сознательные границы этой версии:
 *   - Один LUN (bCBWLUN всегда 0) — составные картридеры с несколькими
 *     логическими юнитами не поддержаны (увидим только первый).
 *   - Нет обработки Stall/Reset Recovery по полной схеме BOT-спеки (Bulk-
 *     Only Mass Storage Reset + Clear Feature на обоих эндпоинтах) —
 *     если bulk_transfer вернул ошибку, команда просто проваливается;
 *     устройство может потребовать физического переподключения, чтобы
 *     восстановиться после реального STALL. Для нормально работающих
 *     флешек в первом приближении не критично.
 *   - Не 4Kn (4096-байтные секторы) — READ CAPACITY(10) возвращает 32-бит
 *     LBA, диски больше 2ТБ (где нужен READ CAPACITY(16)) не поддержаны.
 */
#include "USBMassStorage.h"
#include "USB.h"
#include "VFS.h"
#include "debug_out.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

extern void  *memset(void *dst, int c, size_t n);
extern void  *memcpy(void *dst, const void *src, size_t n);
extern size_t strlcpy(char *dst, const char *src, size_t n);   /* klibc.c */

/* =========================================================================
 * CBW / CSW (USB Mass Storage BOT spec §5.1/§5.2)
 * ========================================================================= */
#define USB_MSC_CBW_SIG 0x43425355u   /* "USBC" */
#define USB_MSC_CSW_SIG 0x53425355u   /* "USBS" */

#pragma pack(push, 1)
typedef struct {
    uint32_t dCBWSignature;
    uint32_t dCBWTag;
    uint32_t dCBWDataTransferLength;
    uint8_t  bmCBWFlags;   /* bit7: 1=IN(device->host), 0=OUT */
    uint8_t  bCBWLUN;      /* младшие 4 бита */
    uint8_t  bCBWCBLength; /* 1..16 */
    uint8_t  CBWCB[16];
} usb_msc_cbw_t;   /* 31 байт */

typedef struct {
    uint32_t dCSWSignature;
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t  bCSWStatus;   /* 0=passed, 1=failed, 2=phase error */
} usb_msc_csw_t;   /* 13 байт */
#pragma pack(pop)

/* =========================================================================
 * Контекст устройства
 * ========================================================================= */
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

/* =========================================================================
 * msc_command — одна SCSI-команда целиком (CBW+DATA+CSW)
 * ========================================================================= */
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

    if (csw.dCSWSignature != USB_MSC_CSW_SIG || csw.dCSWTag != cbw.dCBWTag) return -1;
    if (csw.bCSWStatus != 0) return -2;
    return 0;
}

/* =========================================================================
 * blkdev_ops
 * ========================================================================= */
static int usb_msc_blkdev_read(blkdev_t *dev, uint64_t lba, uint32_t count, void *buf) {
    usb_msc_dev_t *d = (usb_msc_dev_t *)dev->priv;
    if (lba + count > d->sector_count) return VFS_ERR_OVERFLOW;

    uint32_t sec_sz = d->sector_size ? d->sector_size : 512u;
    uint32_t max_sectors = 65536u / sec_sz;   /* см. UHCI/EHCI_BULK_BUF_SIZE */
    if (max_sectors == 0) max_sectors = 1;

    uint8_t *dst = (uint8_t *)buf;
    while (count > 0) {
        uint32_t n = count > max_sectors ? max_sectors : count;

        uint8_t cb[10];
        memset(cb, 0, sizeof(cb));
        cb[0] = 0x28;   /* READ (10) */
        cb[2] = (uint8_t)(lba >> 24); cb[3] = (uint8_t)(lba >> 16);
        cb[4] = (uint8_t)(lba >> 8);  cb[5] = (uint8_t)lba;
        cb[7] = (uint8_t)(n >> 8);    cb[8] = (uint8_t)n;

        if (msc_command(d, cb, 10, dst, n * sec_sz, true) != 0) return VFS_ERR_IO;

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
        cb[0] = 0x2A;   /* WRITE (10) */
        cb[2] = (uint8_t)(lba >> 24); cb[3] = (uint8_t)(lba >> 16);
        cb[4] = (uint8_t)(lba >> 8);  cb[5] = (uint8_t)lba;
        cb[7] = (uint8_t)(n >> 8);    cb[8] = (uint8_t)n;

        if (msc_command(d, cb, 10, (void *)src, n * sec_sz, false) != 0) return VFS_ERR_IO;

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

/* =========================================================================
 * attach
 * ========================================================================= */
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

    /* TEST UNIT READY — "будильник", best effort: некоторые флешки не
     * отвечают на самую первую команду сразу после enumerate, но не
     * возражают ни против неё, ни против следующей READ CAPACITY. */
    uint8_t tur[6];
    memset(tur, 0, sizeof(tur));
    msc_command(d, tur, 6, NULL, 0, true);

    uint8_t cap[8];
    memset(cap, 0, sizeof(cap));
    uint8_t rc10[10];
    memset(rc10, 0, sizeof(rc10));
    rc10[0] = 0x25;   /* READ CAPACITY (10) */
    if (msc_command(d, rc10, 10, cap, sizeof(cap), true) != 0) {
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
