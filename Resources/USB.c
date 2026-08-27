/*
 * USB.c — ядро USB-подсистемы FEXOS: реестр хост-контроллеров и
 * перечисление устройств стандартными control-запросами (см. USB.h).
 *
 * Как и остальные драйверы этого ядра (AHCI.c/IDE.c рядом), файл сам
 * объявляет нужные extern'ы — общего заголовка ядра тут нет.
 */
#include "USB.h"
#include "debug_out.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

extern void *memset(void *dst, int c, size_t n);
extern void *memcpy(void *dst, const void *src, size_t n);

/* Поднимают своё железо и регистрируют найденные контроллеры через
 * usb_register_hcd() — реализованы в UHCI.c/EHCI.c соответственно.
 * Разрешено вызывать, даже если соответствующего контроллера в системе
 * нет — тогда функция просто ничего не найдёт и молча вернётся. */
extern void uhci_pci_scan(void);
extern void ehci_pci_scan(void);

/* Классовые драйверы поверх этого ядра — подключаются автоматически во
 * время перечисления (см. usb_probe_and_dump ниже), реализованы в
 * Resources/USBHID.c / Resources/USBMassStorage.c соответственно. */
extern void usb_hid_attach(usb_hcd_t *hcd, uint8_t dev_addr, uint8_t speed,
                           uint8_t maxp0, uint8_t iface_num, uint8_t protocol);
extern int  usb_msc_attach(usb_hcd_t *hcd, uint8_t dev_addr, uint8_t speed,
                           uint8_t bulk_in_ep, uint16_t bulk_in_maxp,
                           uint8_t bulk_out_ep, uint16_t bulk_out_maxp);

/* rdtsc-калибровка — свои, т.к. Scheduler.c их не экспортирует (см. там
 * же комментарий "static" у rdtsc/tsc_to_ns). ~3 ГГц — то же дефолтное
 * приближение, что используется в Scheduler.c; для busy-wait задержек
 * порядка 1-100мс точность tsc_to_ns не критична. */
static inline uint64_t usb_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
static void usb_delay_ms(uint32_t ms) {
    const uint64_t tsc_per_ms = 3000000ULL; /* ~3 ГГц */
    uint64_t target = usb_rdtsc() + tsc_per_ms * ms;
    while (usb_rdtsc() < target) __asm__ volatile ("pause" ::: "memory");
}

/* =========================================================================
 * Реестр контроллеров
 * ========================================================================= */
#define USB_MAX_HCDS 8
static usb_hcd_t *g_hcds[USB_MAX_HCDS];
static int         g_hcd_count = 0;

void usb_register_hcd(usb_hcd_t *hcd) {
    if (!hcd || g_hcd_count >= USB_MAX_HCDS) return;
    g_hcds[g_hcd_count++] = hcd;
    DBG_VAL("USB", "registered HCD, num_ports", hcd->num_ports);
}

/* =========================================================================
 * Обёртка над ops->control_transfer — собирает usb_setup_pkt_t сама.
 * ========================================================================= */
static int usb_ctrl(usb_hcd_t *hcd, uint8_t addr, uint8_t speed, uint8_t maxp0,
                    uint8_t bmRequestType, uint8_t bRequest,
                    uint16_t wValue, uint16_t wIndex,
                    void *data, uint16_t wLength) {
    usb_setup_pkt_t setup;
    setup.bmRequestType = bmRequestType;
    setup.bRequest      = bRequest;
    setup.wValue        = wValue;
    setup.wIndex        = wIndex;
    setup.wLength       = wLength;
    return hcd->ops->control_transfer(hcd, addr, speed, maxp0, &setup, data, wLength);
}

/* =========================================================================
 * Дамп найденного устройства в лог — пока это и есть весь "драйвер":
 * классовые обработчики (HID/Mass Storage) появятся отдельным слоем
 * поверх usb_ctrl()/control_transfer, см. заголовок USB.h.
 * ========================================================================= */
static const char *usb_class_name(uint8_t cls) {
    switch (cls) {
        case 0x00: return "(см. интерфейс)";
        case 0x01: return "Audio";
        case 0x02: return "CDC (коммуникации)";
        case 0x03: return "HID";
        case 0x06: return "Image";
        case 0x07: return "Printer";
        case 0x08: return "Mass Storage";
        case 0x09: return "Hub";
        case 0x0A: return "CDC-Data";
        case 0xE0: return "Wireless";
        case 0xEF: return "Misc";
        case 0xFE: return "Application";
        case 0xFF: return "Vendor-specific";
        default:   return "?";
    }
}

/* =========================================================================
 * usb_probe_and_dump — один проход по TLV-списку дескрипторов внутри
 * конфигурации: одновременно печатает информативный лог (как раньше
 * usb_dump_device) И подключает классовые драйверы, когда находит
 * подходящий интерфейс:
 *   - HID (class 0x03) — usb_hid_attach() сразу же (клавиатуре/мыши
 *     эндпоинты не нужны, см. Resources/USBHID.c: опрос идёт через
 *     control-transfer, а не через interrupt endpoint).
 *   - Mass Storage BOT (class 0x08, subclass 0x06, protocol 0x50) —
 *     usb_msc_attach() ОТЛОЖЕННО, только когда среди эндпоинтов ЭТОГО
 *     интерфейса найдены оба bulk (IN и OUT) — они идут в дескрипторах
 *     ПОСЛЕ интерфейса, поэтому вызов происходит либо при встрече
 *     следующего интерфейса, либо в конце буфера.
 * ========================================================================= */
static void usb_probe_and_dump(usb_hcd_t *hcd, uint8_t port, uint8_t addr, uint8_t speed,
                               const usb_device_desc_t *dd,
                               const uint8_t *cfg_buf, uint16_t cfg_len) {
    dbg_puts("[USB] port "); dbg_dec64(port);
    dbg_puts(" addr "); dbg_dec64(addr);
    dbg_puts(": VID="); dbg_hex64(dd->idVendor);
    dbg_puts(" PID="); dbg_hex64(dd->idProduct);
    dbg_puts(" class="); dbg_puts(usb_class_name(dd->bDeviceClass));
    dbg_puts("\n");

    if (!cfg_buf || cfg_len < sizeof(usb_config_desc_t)) return;

    uint8_t  cur_class = 0, cur_subclass = 0, cur_proto = 0, cur_iface_num = 0;
    bool     cur_is_msc = false;
    uint8_t  msc_in_ep = 0, msc_out_ep = 0;
    uint16_t msc_in_maxp = 0, msc_out_maxp = 0;
    bool     have_msc_in = false, have_msc_out = false;

    uint16_t off = 0;
    while (off + 2 <= cfg_len) {
        uint8_t len  = cfg_buf[off];
        uint8_t type = cfg_buf[off + 1];
        if (len < 2 || off + len > cfg_len) break;

        if (type == USB_DESC_INTERFACE && len >= sizeof(usb_iface_desc_t)) {
            /* Переходим к новому интерфейсу — если предыдущий был MSC и у
             * него нашлись оба bulk-эндпоинта, подключаем его СЕЙЧАС. */
            if (cur_is_msc && have_msc_in && have_msc_out) {
                usb_msc_attach(hcd, addr, speed, msc_in_ep, msc_in_maxp, msc_out_ep, msc_out_maxp);
            }
            cur_is_msc = false;
            have_msc_in = have_msc_out = false;

            const usb_iface_desc_t *id = (const usb_iface_desc_t *)(cfg_buf + off);
            cur_class     = id->bInterfaceClass;
            cur_subclass  = id->bInterfaceSubClass;
            cur_proto     = id->bInterfaceProtocol;
            cur_iface_num = id->bInterfaceNumber;

            dbg_puts("[USB]   interface "); dbg_dec64(cur_iface_num);
            dbg_puts(": class="); dbg_puts(usb_class_name(cur_class));
            dbg_puts(" subclass="); dbg_hex64(cur_subclass);
            dbg_puts(" proto="); dbg_hex64(cur_proto);
            dbg_puts("\n");

            if (cur_class == 0x03 && cur_subclass == 0x01) {
                /* HID, Boot Protocol поддержан устройством — proto: 1=клавиатура, 2=мышь */
                usb_hid_attach(hcd, addr, speed, dd->bMaxPacketSize0, cur_iface_num, cur_proto);
            }
            if (cur_class == 0x08 && cur_subclass == 0x06 && cur_proto == 0x50) {
                cur_is_msc = true;   /* эндпоинты найдём ниже, в этом же цикле */
            }
        } else if (type == USB_DESC_ENDPOINT && len >= sizeof(usb_ep_desc_t)) {
            const usb_ep_desc_t *ed = (const usb_ep_desc_t *)(cfg_buf + off);
            static const char *kind[4] = { "control", "isoch", "bulk", "interrupt" };
            dbg_puts("[USB]     ep "); dbg_dec64(USB_EP_NUM(ed->bEndpointAddress));
            dbg_puts(USB_EP_IS_IN(ed->bEndpointAddress) ? " IN " : " OUT ");
            dbg_puts(kind[ed->bmAttributes & USB_EP_XFER_MASK]);
            dbg_puts(" maxpkt="); dbg_dec64(ed->wMaxPacketSize);
            dbg_puts("\n");

            if (cur_is_msc && (ed->bmAttributes & USB_EP_XFER_MASK) == USB_EP_XFER_BULK) {
                if (USB_EP_IS_IN(ed->bEndpointAddress)) {
                    msc_in_ep = ed->bEndpointAddress; msc_in_maxp = ed->wMaxPacketSize;
                    have_msc_in = true;
                } else {
                    msc_out_ep = ed->bEndpointAddress; msc_out_maxp = ed->wMaxPacketSize;
                    have_msc_out = true;
                }
            }
        }
        off += len;
    }

    /* Последний интерфейс в буфере — если это был MSC, атач ещё не случился. */
    if (cur_is_msc && have_msc_in && have_msc_out) {
        usb_msc_attach(hcd, addr, speed, msc_in_ep, msc_in_maxp, msc_out_ep, msc_out_maxp);
    }
}

/* =========================================================================
 * usb_enumerate_port — стандартная процедура enumerate (USB 2.0 §9.1.2):
 *   1. port_reset() — уже выполнен вызывающим (usb_init), speed известна
 *   2. GET_DESCRIPTOR(DEVICE, len=8) на адрес 0 — узнать bMaxPacketSize0
 *   3. SET_ADDRESS(next_addr) — тоже на адрес 0
 *   4. GET_DESCRIPTOR(DEVICE, len=18) — уже на новый адрес, полностью
 *   5. GET_DESCRIPTOR(CONFIGURATION, len=9) — узнать wTotalLength
 *   6. GET_DESCRIPTOR(CONFIGURATION, len=wTotalLength) — целиком, с
 *      интерфейсами и эндпоинтами
 *   7. SET_CONFIGURATION(bConfigurationValue)
 * ========================================================================= */
static void usb_enumerate_port(usb_hcd_t *hcd, uint8_t port, uint8_t speed,
                               uint8_t next_addr) {
    /* Шаг 2 — первые 8 байт device descriptor на адресе 0, чтобы узнать
     * реальный bMaxPacketSize0 (спецификация гарантирует, что первые 8
     * байт можно прочитать пакетами максимум по 8 байт независимо от
     * реального EP0 maxpacket — поэтому старт всегда с maxp0=8). */
    usb_device_desc_t dd;
    memset(&dd, 0, sizeof(dd));
    if (usb_ctrl(hcd, 0, speed, 8,
                 USB_REQ_DIR_D2H | USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_DEVICE,
                 USB_REQ_GET_DESCRIPTOR, (USB_DESC_DEVICE << 8), 0,
                 &dd, 8) != 0) {
        DBG_VAL("USB", "port: GET_DESCRIPTOR(8) failed, port", port);
        return;
    }
    uint8_t maxp0 = dd.bMaxPacketSize0 ? dd.bMaxPacketSize0 : 8;

    /* Шаг 3 — SET_ADDRESS. После этого запроса устройство переходит на
     * новый адрес только по завершении STATUS-стадии — драйвер HCD это
     * гарантирует (см. control_transfer в UHCI.c/EHCI.c), так что после
     * возврата usb_ctrl можно смело адресоваться к next_addr. Небольшая
     * пауза (USB 2.0 требует >=2мс на "Set Address Recovery Time"). */
    if (usb_ctrl(hcd, 0, speed, maxp0,
                 USB_REQ_DIR_H2D | USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_DEVICE,
                 USB_REQ_SET_ADDRESS, next_addr, 0, NULL, 0) != 0) {
        DBG_VAL("USB", "port: SET_ADDRESS failed, port", port);
        return;
    }
    usb_delay_ms(10);

    /* Шаг 4 — полный device descriptor, уже на новом адресе. */
    memset(&dd, 0, sizeof(dd));
    if (usb_ctrl(hcd, next_addr, speed, maxp0,
                 USB_REQ_DIR_D2H | USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_DEVICE,
                 USB_REQ_GET_DESCRIPTOR, (USB_DESC_DEVICE << 8), 0,
                 &dd, sizeof(dd)) != 0) {
        DBG_VAL("USB", "port: GET_DESCRIPTOR(18) failed, port", port);
        return;
    }
    maxp0 = dd.bMaxPacketSize0 ? dd.bMaxPacketSize0 : maxp0;

    /* Шаг 5 — заголовок конфигурации (9 байт) — узнать wTotalLength. */
    usb_config_desc_t cd_hdr;
    memset(&cd_hdr, 0, sizeof(cd_hdr));
    if (usb_ctrl(hcd, next_addr, speed, maxp0,
                 USB_REQ_DIR_D2H | USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_DEVICE,
                 USB_REQ_GET_DESCRIPTOR, (USB_DESC_CONFIGURATION << 8), 0,
                 &cd_hdr, sizeof(cd_hdr)) != 0) {
        DBG_VAL("USB", "port: GET_DESCRIPTOR(CFG hdr) failed, port", port);
        usb_probe_and_dump(hcd, port, next_addr, speed, &dd, NULL, 0);
        return;
    }

    /* Шаг 6 — конфигурация целиком (заголовок+интерфейсы+эндпоинты).
     * Ограничиваем разумным потолком — деградировавший/шумный дескриптор
     * не должен вызывать неограниченное чтение. */
    uint16_t total = cd_hdr.wTotalLength;
    if (total < sizeof(cd_hdr)) total = sizeof(cd_hdr);
    if (total > 512) total = 512;

    extern void *kmalloc(uint64_t size);
    extern void  kfree(void *ptr);
    uint8_t *cfg_buf = (uint8_t *)kmalloc(total);
    if (!cfg_buf) {
        DBG_MSG("USB", "port: kmalloc(cfg_buf) failed");
        usb_probe_and_dump(hcd, port, next_addr, speed, &dd, NULL, 0);
        return;
    }
    memset(cfg_buf, 0, total);

    if (usb_ctrl(hcd, next_addr, speed, maxp0,
                 USB_REQ_DIR_D2H | USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_DEVICE,
                 USB_REQ_GET_DESCRIPTOR, (USB_DESC_CONFIGURATION << 8), 0,
                 cfg_buf, total) != 0) {
        DBG_VAL("USB", "port: GET_DESCRIPTOR(CFG full) failed, port", port);
        usb_probe_and_dump(hcd, port, next_addr, speed, &dd, NULL, 0);
        kfree(cfg_buf);
        return;
    }

    /* Шаг 7 — активируем первую (обычно единственную) конфигурацию. */
    const usb_config_desc_t *cd = (const usb_config_desc_t *)cfg_buf;
    if (usb_ctrl(hcd, next_addr, speed, maxp0,
                 USB_REQ_DIR_H2D | USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_DEVICE,
                 USB_REQ_SET_CONFIGURATION, cd->bConfigurationValue, 0,
                 NULL, 0) != 0) {
        DBG_VAL("USB", "port: SET_CONFIGURATION failed, port", port);
        /* не фатально для дампа — покажем что успели узнать */
    }

    usb_probe_and_dump(hcd, port, next_addr, speed, &dd, cfg_buf, total);
    kfree(cfg_buf);
}

/* =========================================================================
 * usb_init — публичная точка входа
 * ========================================================================= */
void usb_init(void) {
    DBG_MSG("USB", "=== usb_init ===");

    g_hcd_count = 0;
    /* ВАЖНО: именно в этом порядке. Во время ehci_pci_scan() порты с
     * не-high-speed устройством сразу передаются companion-контроллеру
     * (см. EHCI.c: ehci_port_reset -> PORTSC.PortOwner) — чтобы к
     * моменту, когда uhci_pci_scan() начнёт опрашивать СВОИ порты, эти
     * устройства на них уже были видны. Если поменять порядок местами,
     * такие устройства будут обнаружены только после следующей
     * перезагрузки (или вообще не будут, пока порт не передёрнут). */
    ehci_pci_scan();
    uhci_pci_scan();

    if (g_hcd_count == 0) {
        DBG_MSG("USB", "no UHCI/EHCI controllers found");
        return;
    }

    uint8_t next_addr = 1;   /* адреса 1..127, 0 зарезервирован под "default state" */

    for (int i = 0; i < g_hcd_count; i++) {
        usb_hcd_t *hcd = g_hcds[i];
        DBG_VAL("USB", "scanning controller ports", hcd->num_ports);

        for (uint8_t p = 0; p < hcd->num_ports; p++) {
            uint8_t speed = USB_SPEED_FULL;
            if (!hcd->ops->port_reset(hcd, p, &speed))
                continue;   /* порт пуст либо не поднялся — пропускаем */

            if (next_addr >= 127) {
                DBG_MSG("USB", "out of USB addresses (>127 devices?), stopping enumeration");
                return;
            }

            DBG_VAL("USB", "device found on port", p);
            usb_enumerate_port(hcd, p, speed, next_addr);
            next_addr++;
        }
    }

    DBG_MSG("USB", "=== usb_init done ===");
}
