/* Fexox OS Project, EULA GPLv2 */
 


#include "USB.h"
#include "Network/net.h"
#include "debug_out.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

extern void *memset(void *dst, int c, size_t n);
extern void *memcpy(void *dst, const void *src, size_t n);

 



extern void uhci_pci_scan(void);
extern void ehci_pci_scan(void);
extern void xhci_pci_scan(void);

 


extern void usb_hid_attach(usb_hcd_t *hcd, uint8_t dev_addr, uint8_t speed,
                           uint8_t maxp0, uint8_t iface_num, uint8_t protocol);
extern void usb_hid_set_int_ep(uint8_t addr, uint8_t iface, uint8_t ep_addr, uint16_t maxp);
extern int  usb_msc_attach(usb_hcd_t *hcd, uint8_t dev_addr, uint8_t speed,
                           uint8_t bulk_in_ep, uint16_t bulk_in_maxp,
                           uint8_t bulk_out_ep, uint16_t bulk_out_maxp);

 





static inline uint64_t usb_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
static inline void usb_outb(uint16_t port, uint8_t v) {
    __asm__ volatile ("outb %0,%1" :: "a"(v), "Nd"(port));
}
static inline uint8_t usb_inb(uint16_t port) {
    uint8_t v; __asm__ volatile ("inb %1,%0" : "=a"(v) : "Nd"(port)); return v;
}

uint64_t usb_tsc_per_ms(void) {
    static uint64_t cached = 0;
    if (cached) return cached;

    uint64_t flags;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(flags) :: "memory");

    uint8_t gate = (uint8_t)(usb_inb(0x61) & ~0x03u);    
    usb_outb(0x61, gate);
    usb_outb(0x43, 0xB0);                                 
    usb_outb(0x42, (uint8_t)(11932 & 0xFF));              
    usb_outb(0x42, (uint8_t)(11932 >> 8));
    uint64_t t0 = usb_rdtsc();
    usb_outb(0x61, (uint8_t)(gate | 0x01));               

    bool done = false;
    for (uint32_t i = 0; i < 5000000u; i++) {             
        if (usb_inb(0x61) & 0x20) { done = true; break; }
    }
    uint64_t t1 = usb_rdtsc();
    usb_outb(0x61, gate);

    __asm__ volatile ("push %0; popfq" :: "r"(flags) : "memory", "cc");

    uint64_t per_ms = done ? (t1 - t0) / 10ULL : 0;
    if (per_ms < 500000ULL || per_ms > 10000000ULL) {    
        DBG_MSG("USB", "PIT TSC calibration failed, using 3 GHz fallback");
        per_ms = 3000000ULL;
    } else {
        DBG_VAL("USB", "TSC ticks per ms", per_ms);
    }
    cached = per_ms;
    return cached;
}

void usb_delay_ms(uint32_t ms) {
    uint64_t target = usb_rdtsc() + usb_tsc_per_ms() * ms;
    while (usb_rdtsc() < target) __asm__ volatile ("pause" ::: "memory");
}

 


static usb_stats_t g_stats;
const usb_stats_t *usb_get_stats(void) { return &g_stats; }

static uint8_t g_next_addr = 1;    
static bool usb_alloc_addr(uint8_t *out) {
    if (g_next_addr >= 127) return false;
    *out = g_next_addr++;
    return true;
}

 


#define USB_MAX_HCDS 8
static usb_hcd_t *g_hcds[USB_MAX_HCDS];
static int         g_hcd_count = 0;

 

void usb_register_hcd(usb_hcd_t *hcd) {
    if (g_hcd_count >= USB_MAX_HCDS) {
        DBG_MSG("USB", "USB_MAX_HCDS reached, controller not registered");
        return;
    }
    g_hcds[g_hcd_count++] = hcd;
}

 


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

 




static const char *usb_class_name(uint8_t cls) {
    switch (cls) {
        case 0x00: return "(see interface)";
        case 0x01: return "Audio";
        case 0x02: return "CDC (communications)";
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

 









static void usb_stats_note_device(const usb_device_desc_t *dd, uint8_t depth) {
    g_stats.devices++;
    if (g_stats.n_list < USB_DEV_LIST_MAX) {
        usb_dev_info_t *e = &g_stats.list[g_stats.n_list++];
        e->vid = dd->idVendor; e->pid = dd->idProduct;
        e->dev_class = dd->bDeviceClass; e->depth = depth;
    }
}

static void usb_try_msc_attach(usb_hcd_t *hcd, uint8_t addr, uint8_t speed,
                               uint8_t in_ep, uint16_t in_maxp,
                               uint8_t out_ep, uint16_t out_maxp) {
    if (usb_msc_attach(hcd, addr, speed, in_ep, in_maxp, out_ep, out_maxp) == 0)
        g_stats.msc++;
}

static void usb_try_net_attach(usb_hcd_t *hcd, uint8_t addr, uint8_t speed, uint8_t maxp0,
                               const uint8_t *cfg_buf, uint16_t cfg_len) {
    DBG_VAL("USB", "net probe: addr", addr);
    DBG_VAL("USB", "net probe: cfg_len", cfg_len);
    int rc = usbnet_probe(hcd, addr, speed, maxp0, cfg_buf, cfg_len);
    DBG_RC("USB", "net probe: usbnet_probe rc (1 = NIC attached, 0 = not a NIC)", rc);
    if (rc == 1) {
        g_stats.net++;
        DBG_MSG("USB", "net probe: USB network adapter attached");
    }
}

static bool usb_probe_and_dump(usb_hcd_t *hcd, uint8_t port, uint8_t addr, uint8_t speed,
                               const usb_device_desc_t *dd,
                               const uint8_t *cfg_buf, uint16_t cfg_len, uint8_t depth) {
    dbg_puts("[USB] port "); dbg_dec64(port);
    dbg_puts(" addr "); dbg_dec64(addr);
    dbg_puts(": VID="); dbg_hex64(dd->idVendor);
    dbg_puts(" PID="); dbg_hex64(dd->idProduct);
    dbg_puts(" class="); dbg_puts(usb_class_name(dd->bDeviceClass));
    dbg_puts("\n");

    usb_stats_note_device(dd, depth);
    bool is_hub = (dd->bDeviceClass == 0x09);

    if (!cfg_buf || cfg_len < sizeof(usb_config_desc_t)) return is_hub;

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
            if (cur_is_msc && have_msc_in && have_msc_out) {
                usb_try_msc_attach(hcd, addr, speed, msc_in_ep, msc_in_maxp, msc_out_ep, msc_out_maxp);
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

            if (cur_class == 0x09) is_hub = true;

            if (cur_class == 0x03 && cur_subclass == 0x01) {
                usb_hid_attach(hcd, addr, speed, dd->bMaxPacketSize0, cur_iface_num, cur_proto);
                g_stats.hid++;
            }
            if (cur_class == 0x08 && cur_subclass == 0x06 && cur_proto == 0x50) {
                cur_is_msc = true;
            }
        } else if (type == USB_DESC_ENDPOINT && len >= sizeof(usb_ep_desc_t)) {
            const usb_ep_desc_t *ed = (const usb_ep_desc_t *)(cfg_buf + off);
            static const char *kind[4] = { "control", "isoch", "bulk", "interrupt" };
            dbg_puts("[USB]     ep "); dbg_dec64(USB_EP_NUM(ed->bEndpointAddress));
            dbg_puts(USB_EP_IS_IN(ed->bEndpointAddress) ? " IN " : " OUT ");
            dbg_puts(kind[ed->bmAttributes & USB_EP_XFER_MASK]);
            dbg_puts(" maxpkt="); dbg_dec64(ed->wMaxPacketSize);
            dbg_puts("\n");

             


            if (cur_class == 0x03 && cur_subclass == 0x01 &&
                (ed->bmAttributes & USB_EP_XFER_MASK) == USB_EP_XFER_INTR &&
                USB_EP_IS_IN(ed->bEndpointAddress)) {
                usb_hid_set_int_ep(addr, cur_iface_num, ed->bEndpointAddress, ed->wMaxPacketSize);
            }

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

    if (cur_is_msc && have_msc_in && have_msc_out) {
        usb_try_msc_attach(hcd, addr, speed, msc_in_ep, msc_in_maxp, msc_out_ep, msc_out_maxp);
    }

     





    if (!is_hub) usb_try_net_attach(hcd, addr, speed, dd->bMaxPacketSize0, cfg_buf, cfg_len);

    return is_hub;
}

 
static void usb_enumerate_device(usb_hcd_t *hcd, uint8_t port, uint8_t speed,
                                 uint8_t next_addr, uint8_t depth,
                                 uint8_t tt_hub, uint8_t tt_port);

 












#define USB_HOTPLUG_MAX_PORTS 16
typedef struct { bool occupied; uint8_t addr; } hotplug_port_t;
static hotplug_port_t g_hotplug[USB_MAX_HCDS][USB_HOTPLUG_MAX_PORTS];

 
















#define HUB_PORT_STAT_CONNECTION   (1u << 0)
#define HUB_PORT_STAT_ENABLE       (1u << 1)
#define HUB_PORT_STAT_RESET        (1u << 4)
#define HUB_PORT_STAT_LOW_SPEED    (1u << 9)
#define HUB_PORT_STAT_HIGH_SPEED   (1u << 10)
#define HUB_PORT_CHG_RESET         (1u << 4)

#define HUB_FEAT_PORT_RESET        4u
#define HUB_FEAT_PORT_POWER        8u
#define HUB_FEAT_C_PORT_CONNECTION 16u
#define HUB_FEAT_C_PORT_RESET      20u

#define USB_HUB_MAX_DEPTH 5

static int usb_hub_port_feature(usb_hcd_t *hcd, uint8_t addr, uint8_t speed, uint8_t maxp0,
                                uint8_t port, uint16_t feature, bool set) {
    return usb_ctrl(hcd, addr, speed, maxp0,
                    USB_REQ_DIR_H2D | USB_REQ_TYPE_CLASS | USB_REQ_RECIP_OTHER,
                    set ? USB_REQ_SET_FEATURE : USB_REQ_CLEAR_FEATURE,
                    feature, port, NULL, 0);
}

static int usb_hub_port_status(usb_hcd_t *hcd, uint8_t addr, uint8_t speed, uint8_t maxp0,
                               uint8_t port, uint16_t *st, uint16_t *chg) {
    uint8_t b[4];
    memset(b, 0, sizeof(b));
    int r = usb_ctrl(hcd, addr, speed, maxp0,
                     USB_REQ_DIR_D2H | USB_REQ_TYPE_CLASS | USB_REQ_RECIP_OTHER,
                     USB_REQ_GET_STATUS, 0, port, b, 4);
    if (r != 0) return r;
    *st  = (uint16_t)(b[0] | (b[1] << 8));
    *chg = (uint16_t)(b[2] | (b[3] << 8));
    return 0;
}

 













#define USB_HOTPLUG_MAX_HUBS     4
#define USB_HOTPLUG_MAX_HUBPORTS 15

typedef struct {
    bool       used;
    usb_hcd_t *hcd;
    uint8_t    hub_addr, hub_speed, hub_maxp0, hub_ports, depth;
    uint8_t    tt_hub, tt_port;              
    struct { bool occupied; uint8_t addr; } port[USB_HOTPLUG_MAX_HUBPORTS];
} hotplug_hub_t;

static hotplug_hub_t g_hotplug_hubs[USB_HOTPLUG_MAX_HUBS];

static hotplug_hub_t *hotplug_hub_register(usb_hcd_t *hcd, uint8_t hub_addr, uint8_t hub_speed,
                                           uint8_t hub_maxp0, uint8_t hub_ports, uint8_t depth,
                                           uint8_t tt_hub, uint8_t tt_port) {
    for (int i = 0; i < USB_HOTPLUG_MAX_HUBS; i++) {
        if (g_hotplug_hubs[i].used && g_hotplug_hubs[i].hcd == hcd && g_hotplug_hubs[i].hub_addr == hub_addr)
            return &g_hotplug_hubs[i];                
    }
    for (int i = 0; i < USB_HOTPLUG_MAX_HUBS; i++) {
        if (!g_hotplug_hubs[i].used) {
            hotplug_hub_t *h = &g_hotplug_hubs[i];
            memset(h, 0, sizeof(*h));
            h->used = true; h->hcd = hcd; h->hub_addr = hub_addr; h->hub_speed = hub_speed;
            h->hub_maxp0 = hub_maxp0; h->hub_ports = hub_ports; h->depth = depth;
            h->tt_hub = tt_hub; h->tt_port = tt_port;
            return h;
        }
    }
    DBG_MSG("USB", "hotplug: USB_HOTPLUG_MAX_HUBS reached, this hub won't get hotplug polling");
    return NULL;
}

 





static uint8_t usb_hub_reset_and_enumerate_port(usb_hcd_t *hcd, uint8_t hub_addr, uint8_t hub_speed,
                                                uint8_t hub_maxp0, uint8_t p, uint8_t depth,
                                                uint8_t hub_tt_hub, uint8_t hub_tt_port) {
    uint16_t st = 0, chg = 0;
    if (usb_hub_port_feature(hcd, hub_addr, hub_speed, hub_maxp0, p, HUB_FEAT_PORT_RESET, true) != 0)
        return 0;
    bool reset_done = false;
    for (int t = 0; t < 60; t++) {    
        usb_delay_ms(10);
        if (usb_hub_port_status(hcd, hub_addr, hub_speed, hub_maxp0, p, &st, &chg) != 0) break;
        if ((chg & HUB_PORT_CHG_RESET) || !(st & HUB_PORT_STAT_RESET)) { reset_done = true; break; }
    }
    usb_hub_port_feature(hcd, hub_addr, hub_speed, hub_maxp0, p, HUB_FEAT_C_PORT_RESET, false);
    if (!reset_done) { DBG_VAL("USB", "hub: port reset timeout, port", p); return 0; }
    usb_delay_ms(20);    

    if (usb_hub_port_status(hcd, hub_addr, hub_speed, hub_maxp0, p, &st, &chg) != 0) return 0;
    if (!(st & HUB_PORT_STAT_CONNECTION) || !(st & HUB_PORT_STAT_ENABLE)) return 0;

    uint8_t dev_speed = (st & HUB_PORT_STAT_HIGH_SPEED) ? USB_SPEED_HIGH
                      : (st & HUB_PORT_STAT_LOW_SPEED)  ? USB_SPEED_LOW
                      :                                   USB_SPEED_FULL;

    uint8_t tt_hub = 0, tt_port = 0;
    if (hub_speed == USB_SPEED_HIGH) {
        if (dev_speed != USB_SPEED_HIGH) {
            if (!hcd->ops->set_route) {
                DBG_VAL("USB", "hub: LS/FS behind HS hub, HCD has no split support, port", p);
                g_stats.split_skipped++;
                return 0;
            }
            tt_hub = hub_addr; tt_port = p;
        }
    } else {
        tt_hub = hub_tt_hub; tt_port = hub_tt_port;
    }

    uint8_t addr;
    if (!usb_alloc_addr(&addr)) { DBG_MSG("USB", "out of USB addresses"); return 0; }
    DBG_VAL("USB", "hub: device on hub port", p);
    usb_enumerate_device(hcd, p, dev_speed, addr, (uint8_t)(depth + 1), tt_hub, tt_port);
    return addr;
}

static void usb_enum_hub(usb_hcd_t *hcd, uint8_t hub_addr, uint8_t hub_speed,
                         uint8_t hub_maxp0, uint8_t depth,
                         uint8_t hub_tt_hub, uint8_t hub_tt_port) {
    g_stats.hubs++;
    if (depth >= USB_HUB_MAX_DEPTH) { DBG_MSG("USB", "hub: nesting too deep, skipped"); return; }
    if (hub_speed == USB_SPEED_LOW) return;    

     
    uint8_t hd[8];
    memset(hd, 0, sizeof(hd));
    if (usb_ctrl(hcd, hub_addr, hub_speed, hub_maxp0,
                 USB_REQ_DIR_D2H | USB_REQ_TYPE_CLASS | USB_REQ_RECIP_DEVICE,
                 USB_REQ_GET_DESCRIPTOR, (0x29u << 8), 0, hd, 7) != 0) {
        DBG_VAL("USB", "hub: GET_DESCRIPTOR(hub) failed, addr", hub_addr);
        g_stats.enum_fail++;
        return;
    }
    uint8_t nports = hd[2];
    uint32_t pwr_ms = (uint32_t)hd[5] * 2u;    
    if (nports == 0) return;
    if (nports > 15) nports = 15;
    DBG_VAL("USB", "hub: ports", nports);

     




    hotplug_hub_t *reg = hotplug_hub_register(hcd, hub_addr, hub_speed, hub_maxp0, nports, depth, hub_tt_hub, hub_tt_port);

     



    for (uint8_t p = 1; p <= nports; p++)
        usb_hub_port_feature(hcd, hub_addr, hub_speed, hub_maxp0, p, HUB_FEAT_PORT_POWER, true);
    usb_delay_ms((pwr_ms < 100u ? 100u : pwr_ms) + 100u);

    for (uint8_t p = 1; p <= nports; p++) {
        uint16_t st = 0, chg = 0;
        if (usb_hub_port_status(hcd, hub_addr, hub_speed, hub_maxp0, p, &st, &chg) != 0) continue;
        if (chg & 0x0001u)    
            usb_hub_port_feature(hcd, hub_addr, hub_speed, hub_maxp0, p, HUB_FEAT_C_PORT_CONNECTION, false);
        if (!(st & HUB_PORT_STAT_CONNECTION)) continue;

        uint8_t addr = usb_hub_reset_and_enumerate_port(hcd, hub_addr, hub_speed, hub_maxp0, p, depth, hub_tt_hub, hub_tt_port);
        if (reg && p - 1 < USB_HOTPLUG_MAX_HUBPORTS) {
            reg->port[p - 1].occupied = (addr != 0);
            reg->port[p - 1].addr = addr;
        }
    }
}

 









static void usb_enumerate_device(usb_hcd_t *hcd, uint8_t port, uint8_t speed,
                                 uint8_t next_addr, uint8_t depth,
                                 uint8_t tt_hub, uint8_t tt_port) {
    usb_device_desc_t dd;
    memset(&dd, 0, sizeof(dd));

     

    if (tt_hub && hcd->ops->set_route) hcd->ops->set_route(hcd, 0, tt_hub, tt_port);

     


    int r = -1;
    for (int attempt = 0; attempt < 3 && r != 0; attempt++) {
        if (attempt) usb_delay_ms(30);
        r = usb_ctrl(hcd, 0, speed, 8,
                     USB_REQ_DIR_D2H | USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_DEVICE,
                     USB_REQ_GET_DESCRIPTOR, (USB_DESC_DEVICE << 8), 0,
                     &dd, 8);
    }
    if (r != 0) {
        DBG_VAL("USB", "port: GET_DESCRIPTOR(8) failed, port", port);
        g_stats.enum_fail++;
        if (!g_stats.fail_step) { g_stats.fail_step = 2; g_stats.fail_rc = (int8_t)r; }
        return;
    }
    uint8_t maxp0 = dd.bMaxPacketSize0 ? dd.bMaxPacketSize0 : 8;

    r = usb_ctrl(hcd, 0, speed, maxp0,
                 USB_REQ_DIR_H2D | USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_DEVICE,
                 USB_REQ_SET_ADDRESS, next_addr, 0, NULL, 0);
    if (r != 0) {
        DBG_VAL("USB", "port: SET_ADDRESS failed, port", port);
        g_stats.enum_fail++;
        if (!g_stats.fail_step) { g_stats.fail_step = 3; g_stats.fail_rc = (int8_t)r; }
        return;
    }
    if (tt_hub && hcd->ops->set_route) hcd->ops->set_route(hcd, next_addr, tt_hub, tt_port);
    usb_delay_ms(10);    

    memset(&dd, 0, sizeof(dd));
    r = usb_ctrl(hcd, next_addr, speed, maxp0,
                 USB_REQ_DIR_D2H | USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_DEVICE,
                 USB_REQ_GET_DESCRIPTOR, (USB_DESC_DEVICE << 8), 0,
                 &dd, sizeof(dd));
    if (r != 0) {
        DBG_VAL("USB", "port: GET_DESCRIPTOR(18) failed, port", port);
        g_stats.enum_fail++;
        if (!g_stats.fail_step) { g_stats.fail_step = 4; g_stats.fail_rc = (int8_t)r; }
        return;
    }
    maxp0 = dd.bMaxPacketSize0 ? dd.bMaxPacketSize0 : maxp0;

    usb_config_desc_t cd_hdr;
    memset(&cd_hdr, 0, sizeof(cd_hdr));
    if (usb_ctrl(hcd, next_addr, speed, maxp0,
                 USB_REQ_DIR_D2H | USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_DEVICE,
                 USB_REQ_GET_DESCRIPTOR, (USB_DESC_CONFIGURATION << 8), 0,
                 &cd_hdr, sizeof(cd_hdr)) != 0) {
        DBG_VAL("USB", "port: GET_DESCRIPTOR(CFG hdr) failed, port", port);
        g_stats.enum_fail++;
        usb_probe_and_dump(hcd, port, next_addr, speed, &dd, NULL, 0, depth);
        return;
    }

    uint16_t total = cd_hdr.wTotalLength;
    if (total < sizeof(cd_hdr)) total = sizeof(cd_hdr);
    if (total > 512) total = 512;

    extern void *kmalloc(uint64_t size);
    extern void  kfree(void *ptr);
    uint8_t *cfg_buf = (uint8_t *)kmalloc(total);
    if (!cfg_buf) {
        DBG_MSG("USB", "port: kmalloc(cfg_buf) failed");
        usb_probe_and_dump(hcd, port, next_addr, speed, &dd, NULL, 0, depth);
        return;
    }
    memset(cfg_buf, 0, total);

    if (usb_ctrl(hcd, next_addr, speed, maxp0,
                 USB_REQ_DIR_D2H | USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_DEVICE,
                 USB_REQ_GET_DESCRIPTOR, (USB_DESC_CONFIGURATION << 8), 0,
                 cfg_buf, total) != 0) {
        DBG_VAL("USB", "port: GET_DESCRIPTOR(CFG full) failed, port", port);
        g_stats.enum_fail++;
        usb_probe_and_dump(hcd, port, next_addr, speed, &dd, NULL, 0, depth);
        kfree(cfg_buf);
        return;
    }

    const usb_config_desc_t *cd = (const usb_config_desc_t *)cfg_buf;
    if (usb_ctrl(hcd, next_addr, speed, maxp0,
                 USB_REQ_DIR_H2D | USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_DEVICE,
                 USB_REQ_SET_CONFIGURATION, cd->bConfigurationValue, 0,
                 NULL, 0) != 0) {
        DBG_VAL("USB", "port: SET_CONFIGURATION failed, port", port);
    }

    bool is_hub = usb_probe_and_dump(hcd, port, next_addr, speed, &dd, cfg_buf, total, depth);
    kfree(cfg_buf);

    if (is_hub && hcd->ops->supports_hubs)
        usb_enum_hub(hcd, next_addr, speed, maxp0, depth, tt_hub, tt_port);
}

 


void usb_init(void) {
    DBG_MSG("USB", "=== usb_init ===");

    g_hcd_count = 0;
    g_next_addr = 1;
    memset(&g_stats, 0, sizeof(g_stats));
    (void)usb_tsc_per_ms();    

     


    xhci_pci_scan();

     




    ehci_pci_scan();
    uhci_pci_scan();

    g_stats.hcds = (uint8_t)g_hcd_count;
    if (g_hcd_count == 0) {
        DBG_MSG("USB", "no xHCI/UHCI/EHCI controllers found");
        return;
    }

    for (int i = 0; i < g_hcd_count; i++) {
        usb_hcd_t *hcd = g_hcds[i];
        DBG_VAL("USB", "scanning controller ports", hcd->num_ports);

        for (uint8_t p = 0; p < hcd->num_ports; p++) {
            if (i < USB_MAX_HCDS && p < USB_HOTPLUG_MAX_PORTS) g_hotplug[i][p].occupied = false;

            uint8_t speed = USB_SPEED_FULL;
            if (!hcd->ops->port_reset(hcd, p, &speed))
                continue;    

            uint8_t addr;
            if (!usb_alloc_addr(&addr)) {
                DBG_MSG("USB", "out of USB addresses (>127 devices?), stopping enumeration");
                return;
            }

            DBG_VAL("USB", "device found on port", p);
            usb_enumerate_device(hcd, p, speed, addr, 0, 0, 0);

             



            if (i < USB_MAX_HCDS && p < USB_HOTPLUG_MAX_PORTS) {
                g_hotplug[i][p].occupied = true;
                g_hotplug[i][p].addr = addr;
            }
        }
    }

    DBG_MSG("USB", "=== usb_init done ===");
}

 


















void usb_hotplug_poll(void) {
    for (int i = 0; i < g_hcd_count; i++) {
        usb_hcd_t *hcd = g_hcds[i];
        if (!hcd->ops->port_connected) continue;
        uint8_t maxp = hcd->num_ports < USB_HOTPLUG_MAX_PORTS ? hcd->num_ports : USB_HOTPLUG_MAX_PORTS;

        for (uint8_t p = 0; p < maxp; p++) {
            hotplug_port_t *hp = &g_hotplug[i][p];
            bool connected = false;
            if (!hcd->ops->port_connected(hcd, p, &connected)) continue;    

            if (!hp->occupied && connected) {
                uint8_t speed = USB_SPEED_FULL;
                if (!hcd->ops->port_reset(hcd, p, &speed)) continue;        
                uint8_t addr;
                if (!usb_alloc_addr(&addr)) { DBG_MSG("USB", "hotplug: out of USB addresses"); continue; }
                DBG_VAL("USB", "hotplug: device connected, root port", p);
                usb_enumerate_device(hcd, p, speed, addr, 0, 0, 0);
                hp->occupied = true;
                hp->addr = addr;
            } else if (hp->occupied && !connected) {
                DBG_VAL("USB", "hotplug: device disconnected, root port", p);
                usbnet_detach(hcd, hp->addr);       
                hp->occupied = false;
            }
        }
    }

    for (int i = 0; i < USB_HOTPLUG_MAX_HUBS; i++) {
        hotplug_hub_t *h = &g_hotplug_hubs[i];
        if (!h->used) continue;
        uint8_t maxp = h->hub_ports < USB_HOTPLUG_MAX_HUBPORTS ? h->hub_ports : USB_HOTPLUG_MAX_HUBPORTS;

        for (uint8_t pi = 0; pi < maxp; pi++) {
            uint8_t p = (uint8_t)(pi + 1);         
            uint16_t st = 0, chg = 0;
             

            if (usb_hub_port_status(h->hcd, h->hub_addr, h->hub_speed, h->hub_maxp0, p, &st, &chg) != 0) continue;
            if (chg & 0x0001u)
                usb_hub_port_feature(h->hcd, h->hub_addr, h->hub_speed, h->hub_maxp0, p, HUB_FEAT_C_PORT_CONNECTION, false);
            bool connected = (st & HUB_PORT_STAT_CONNECTION) != 0;

            if (!h->port[pi].occupied && connected) {
                DBG_VAL("USB", "hotplug: device connected, hub port", p);
                uint8_t addr = usb_hub_reset_and_enumerate_port(h->hcd, h->hub_addr, h->hub_speed, h->hub_maxp0,
                                                                p, h->depth, h->tt_hub, h->tt_port);
                h->port[pi].occupied = (addr != 0);
                h->port[pi].addr = addr;
            } else if (h->port[pi].occupied && !connected) {
                DBG_VAL("USB", "hotplug: device disconnected, hub port", p);
                usbnet_detach(h->hcd, h->port[pi].addr);
                h->port[pi].occupied = false;
            }
        }
    }
}
