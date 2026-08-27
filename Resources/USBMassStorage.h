#ifndef USBMASSSTORAGE_H
#define USBMASSSTORAGE_H

#include <stdint.h>

/*
 * USBMassStorage.h — класс-драйвер USB Mass Storage, Bulk-Only Transport
 * (BOT) + SCSI transparent command set (bInterfaceSubClass==0x06,
 * bInterfaceProtocol==0x50). Подробности и сознательные границы — см.
 * шапку USBMassStorage.c.
 */

struct usb_hcd;   /* opaque — полное определение в USB.h */

/* Вызывается из USB.c (usb_enumerate_port) при обнаружении интерфейса
 * Mass Storage BOT — с уже найденными bulk IN/OUT эндпоинтами (адрес со
 * старшим битом направления + wMaxPacketSize, как в usb_ep_desc_t).
 * Пытается опросить устройство (TEST UNIT READY + READ CAPACITY) и, если
 * получилось, регистрирует его как блочное устройство через
 * blkdev_register() (VFS.h) — "uda", "udb", ... (аналогично sdN/hdN у
 * AHCI/IDE, свой префикс, чтобы не путать). Возвращает 0 при успехе,
 * отрицательное — если устройство не ответило (например, флешка без
 * карты в кардридере) — это не ошибка загрузки, просто устройство не
 * стало диском. */
int usb_msc_attach(struct usb_hcd *hcd, uint8_t dev_addr, uint8_t speed,
                   uint8_t bulk_in_ep, uint16_t bulk_in_maxp,
                   uint8_t bulk_out_ep, uint16_t bulk_out_maxp);

#endif /* USBMASSSTORAGE_H */
