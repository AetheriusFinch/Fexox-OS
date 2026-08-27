/*
 * USB.h — ядро USB-подсистемы FEXOS (не зависит от конкретного контроллера)
 *
 * Архитектура: любой хост-контроллер (UHCI.c, EHCI.c, в будущем OHCI/xHCI)
 * реализует usb_hcd_ops_t (control_transfer + port_reset) и регистрируется
 * через usb_register_hcd(). Само ядро (USB.c) ничего не знает о регистрах
 * конкретного контроллера — оно только гоняет стандартные control-запросы
 * через ops->control_transfer и разбирает дескрипторы.
 *
 * Текущий охват:
 *   - Перечисление устройств: reset порта -> адрес -> device descriptor ->
 *     config descriptor -> SET_CONFIGURATION.
 *   - Классовые драйверы поверх ядра (подключаются автоматически во время
 *     перечисления, см. usb_enumerate_port() в USB.c):
 *       * HID (клавиатура/мышь, Boot Protocol) — Resources/USBHID.c
 *       * Mass Storage BOT (SCSI/Bulk-Only) — Resources/USBMassStorage.c
 *     Оба задокументированы каждый в своей шапке файла, включая границы
 *     первой версии (что сознательно не реализовано).
 *   - Все структуры HCD и все временные буферы под DMA обязаны лежать
 *     физически < 4 ГБ (см. pmm_alloc_frames_range в MemoryControl.c) —
 *     справедливо и для UHCI (аппаратно всегда 32-бит), и для EHCI в
 *     этом драйвере (64-bit addressing capability мы не включаем —
 *     не нужно при DIRECT_MAP-модели памяти этого ядра).
 */
#ifndef USB_H
#define USB_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* =========================================================================
 * bmRequestType (USB 2.0 spec, §9.3)
 * ========================================================================= */
#define USB_REQ_DIR_H2D          0x00u   /* host -> device (OUT) */
#define USB_REQ_DIR_D2H          0x80u   /* device -> host (IN) */
#define USB_REQ_TYPE_STANDARD    0x00u
#define USB_REQ_TYPE_CLASS       0x20u
#define USB_REQ_TYPE_VENDOR      0x40u
#define USB_REQ_RECIP_DEVICE     0x00u
#define USB_REQ_RECIP_INTERFACE  0x01u
#define USB_REQ_RECIP_ENDPOINT   0x02u

/* Стандартные bRequest */
#define USB_REQ_GET_STATUS        0x00u
#define USB_REQ_CLEAR_FEATURE     0x01u
#define USB_REQ_SET_FEATURE       0x03u
#define USB_REQ_SET_ADDRESS       0x05u
#define USB_REQ_GET_DESCRIPTOR    0x06u
#define USB_REQ_SET_DESCRIPTOR    0x07u
#define USB_REQ_GET_CONFIGURATION 0x08u
#define USB_REQ_SET_CONFIGURATION 0x09u
#define USB_REQ_SET_INTERFACE     0x0Bu

/* Типы дескрипторов */
#define USB_DESC_DEVICE           0x01u
#define USB_DESC_CONFIGURATION    0x02u
#define USB_DESC_STRING           0x03u
#define USB_DESC_INTERFACE        0x04u
#define USB_DESC_ENDPOINT         0x05u

/* Скорость устройства — влияет на кодирование транзакций у HCD (низкая
 * скорость требует бита LS в UHCI TD, EHCI низкоскоростные устройства
 * обслуживает только через split-транзакции/companion-контроллер, что в
 * этой первой версии НЕ поддержано, см. EHCI.c). */
#define USB_SPEED_LOW    0   /* 1.5 Мбит/с */
#define USB_SPEED_FULL   1   /* 12 Мбит/с */
#define USB_SPEED_HIGH   2   /* 480 Мбит/с, только EHCI */

#pragma pack(push, 1)

typedef struct {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} usb_setup_pkt_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} usb_device_desc_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} usb_config_desc_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} usb_iface_desc_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} usb_ep_desc_t;

#pragma pack(pop)

#define USB_EP_NUM(addr)   ((addr) & 0x0Fu)
#define USB_EP_IS_IN(addr) (((addr) & 0x80u) != 0)
#define USB_EP_XFER_MASK    0x03u
#define USB_EP_XFER_CONTROL  0x00u
#define USB_EP_XFER_ISOCH    0x01u
#define USB_EP_XFER_BULK     0x02u
#define USB_EP_XFER_INTR     0x03u

/* =========================================================================
 * Абстракция хост-контроллера — реализуется UHCI.c/EHCI.c
 * ========================================================================= */
typedef struct usb_hcd usb_hcd_t;

typedef struct {
    /*
     * control_transfer — полная control-транзакция (SETUP [+ DATA] +
     * STATUS), синхронно (функция не возвращается, пока HC не завершит
     * или не истечёт таймаут — драйверы этого ядра, как AHCI/IDE рядом,
     * работают через polling, без прерываний).
     *
     *   dev_addr    — текущий адрес устройства (0 — устройство ещё не
     *                 адресовано, используется только на этапе enumerate)
     *   speed       — USB_SPEED_*
     *   max_packet0 — размер пакета EP0 (до первого GET_DESCRIPTOR — 8,
     *                 дальше берётся из usb_device_desc_t.bMaxPacketSize0)
     *   data/len    — буфер DATA-стадии; data может быть NULL при len==0
     *
     * Возвращает 0 при успехе, отрицательный код при ошибке/таймауте.
     */
    int (*control_transfer)(usb_hcd_t *hcd, uint8_t dev_addr, uint8_t speed,
                            uint8_t max_packet0, const usb_setup_pkt_t *setup,
                            void *data, uint16_t len);

    /*
     * port_reset — выдать USB reset на порт (обязательно перед выдачей
     * адреса свежеподключённому устройству, см. usb_enumerate_port).
     * out_speed заполняется по факту сброса (для UHCI — по PORTSC.LSDA,
     * для EHCI — full/high по PORTSC после reset, low не поддержан, см.
     * EHCI.c). Возвращает false, если порт пуст/устройство не поднялось.
     */
    bool (*port_reset)(usb_hcd_t *hcd, uint8_t port, uint8_t *out_speed);

    /*
     * bulk_transfer — одна bulk-транзакция (без SETUP/STATUS стадий, в
     * отличие от control_transfer) — нужна классовым драйверам поверх
     * (Mass Storage BOT, см. Resources/USBMassStorage.c). В отличие от
     * control-адреса EP0, bulk-эндпоинты помнят Data Toggle МЕЖДУ
     * вызовами — поэтому toggle_inout передаётся вызывающим (он же его
     * хранит, per-endpoint) и обновляется драйвером по факту отправки.
     *
     *   ep_addr     — адрес эндпоинта СО старшим битом направления
     *                 (0x80 = IN), как в usb_ep_desc_t.bEndpointAddress
     *   max_packet  — wMaxPacketSize эндпоинта (из дескриптора)
     *   data/len    — буфер целиком; максимум зависит от реализации HCD
     *                 (см. комментарий у bulk_buf в UHCI.c/EHCI.c — сейчас
     *                 64 КБ за один вызов, для большего вызывающий сам
     *                 разбивает на несколько вызовов)
     *
     * Возвращает 0 при успехе, отрицательное при ошибке/таймауте/STALL.
     */
    int (*bulk_transfer)(usb_hcd_t *hcd, uint8_t dev_addr, uint8_t speed,
                         uint8_t ep_addr, uint16_t max_packet,
                         void *data, uint32_t len, uint8_t *toggle_inout);
} usb_hcd_ops_t;

struct usb_hcd {
    const usb_hcd_ops_t *ops;
    void                *priv;    /* приватные данные конкретного драйвера — uhci_ctrl_t или ehci_ctrl_t */
    const char          *name;    /* "uhci0", "ehci0", ... — для логов */
    uint8_t              num_ports;
};

/* Зарегистрировать хост-контроллер (вызывается из uhci_pci_scan()/
 * ehci_pci_scan() после успешного поднятия железа). usb_init() сама
 * пройдётся по портам через ops->port_reset и перечислит устройства. */
void usb_register_hcd(usb_hcd_t *hcd);

/* Инициализация USB-подсистемы целиком: сканирует PCI (uhci_pci_scan() +
 * ehci_pci_scan()), затем перечисляет устройства на всех портах всех
 * найденных контроллеров. Вызывать один раз при старте ядра, ПОСЛЕ того
 * как PMM/kmalloc и PCI config space доступны. */
void usb_init(void);

#endif /* USB_H */
