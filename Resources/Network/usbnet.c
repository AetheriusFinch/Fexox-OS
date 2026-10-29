/* Fexox OS Project, EULA GPLv2 */
 






#include "net_usb.h"
#include "USB.h"
#include "debug_out.h"

extern void  *memset(void *dst, int c, size_t n);
extern void  *memcpy(void *dst, const void *src, size_t n);

static usbnet_dev_t g_dev[USBNET_MAX_DEV];

 


void usbnet_parse_config(const uint8_t *cfg, uint16_t len, usbnet_parse_t *out) {
    memset(out, 0, sizeof(*out));
    out->union_master = out->union_slave = -1;

    usbnet_iface_t *cur = NULL;
    uint8_t cur_class = 0;
    uint16_t off = 0;
     
    while (off + 2 <= len) {
        uint8_t dlen = cfg[off], dtype = cfg[off + 1];
        if (dlen < 2 || off + dlen > len) break;

        if (dtype == USB_DESC_INTERFACE && dlen >= 9) {
            uint8_t num = cfg[off + 2], alt = cfg[off + 3];
            uint8_t nep = cfg[off + 4];
            cur_class = cfg[off + 5];
            uint8_t subclass = cfg[off + 6], proto = cfg[off + 7];

            cur = usbnet_find_iface(out, num);
            if (!cur && out->n_iface < USBNET_MAX_IFACES) { cur = &out->iface[out->n_iface++]; cur->num = num; }
            if (cur && cur->n_alt < USBNET_MAX_ALTS) {
                usbnet_altset_t *a = &cur->alt[cur->n_alt++];
                a->alt = alt; a->class_ = cur_class; a->subclass = subclass; a->proto = proto; a->n_ep = 0;
                (void)nep;
            }
        } else if (dtype == USB_DESC_ENDPOINT && dlen >= 7 && cur && cur->n_alt > 0) {
            usbnet_altset_t *a = &cur->alt[cur->n_alt - 1];
            if (a->n_ep < USBNET_MAX_EPS) {
                a->ep[a->n_ep].addr = cfg[off + 2];
                a->ep[a->n_ep].attr = cfg[off + 3];
                a->ep[a->n_ep].maxp = (uint16_t)(cfg[off + 4] | (cfg[off + 5] << 8));
                a->n_ep++;
            }
        } else if (dtype == 0x24 && dlen >= 3) {                 
            uint8_t subtype = cfg[off + 2];
            if (subtype == 0x06 && dlen >= 5) {                  
                out->union_master = cfg[off + 3];
                out->union_slave  = cfg[off + 4];
            } else if (subtype == 0x0F && dlen >= 4) {           
                out->have_ecm_desc = true;
                out->ecm_mac_str_idx = cfg[off + 3];
            }
        }
        off += dlen;
    }
}

usbnet_iface_t *usbnet_find_iface(usbnet_parse_t *p, uint8_t num) {
    for (int i = 0; i < p->n_iface; i++) if (p->iface[i].num == num) return &p->iface[i];
    return NULL;
}

usbnet_altset_t *usbnet_find_bulk_alt(usbnet_iface_t *ifc, uint8_t *out_in, uint8_t *out_out,
                                      uint16_t *out_in_mp, uint16_t *out_out_mp) {
    if (!ifc) return NULL;
    for (int a = 0; a < ifc->n_alt; a++) {
        usbnet_altset_t *as = &ifc->alt[a];
        uint8_t in_ep = 0, out_ep = 0; uint16_t in_mp = 0, out_mp = 0; int n_bulk = 0;
        for (int e = 0; e < as->n_ep; e++) {
            if ((as->ep[e].attr & USB_EP_XFER_MASK) != USB_EP_XFER_BULK) continue;
            n_bulk++;
            if (USB_EP_IS_IN(as->ep[e].addr)) { in_ep = as->ep[e].addr; in_mp = as->ep[e].maxp; }
            else                              { out_ep = as->ep[e].addr; out_mp = as->ep[e].maxp; }
        }
        if (n_bulk == 2 && in_ep && out_ep) {
            *out_in = in_ep; *out_out = out_ep; *out_in_mp = in_mp; *out_out_mp = out_mp;
            return as;
        }
    }
    return NULL;
}

 


int usbnet_ctrl(usbnet_dev_t *d, uint8_t bmReq, uint8_t bReq, uint16_t wValue, uint16_t wIndex,
                void *data, uint16_t wLength) {
    usb_setup_pkt_t sp;
    sp.bmRequestType = bmReq; sp.bRequest = bReq; sp.wValue = wValue; sp.wIndex = wIndex; sp.wLength = wLength;
    return d->hcd->ops->control_transfer(d->hcd, d->addr, d->speed, d->maxp0, &sp, data, wLength);
}

int usbnet_bulk_out(usbnet_dev_t *d, const void *data, uint32_t len) {
    if (!d->hcd->ops->bulk_transfer) return -1;
    return d->hcd->ops->bulk_transfer(d->hcd, d->addr, d->speed, d->bulk_out_ep, d->bulk_out_maxp,
                                      (void *)data, len, &d->toggle_out);
}
 









int usbnet_bulk_in(usbnet_dev_t *d, void *data, uint32_t len) {
    const usb_hcd_ops_t *ops = d->hcd->ops;
    if (!ops->bulk_in_poll) return 0;       
    uint8_t *p = (uint8_t *)data;
    uint32_t total = 0;
    uint16_t mp = d->bulk_in_maxp ? d->bulk_in_maxp : 512;
    uint32_t tmo = 300;                     
    int stalls = 0;
    while (total < len) {
        int r = ops->bulk_in_poll(d->hcd, d->addr, d->speed, d->bulk_in_ep, d->bulk_in_maxp,
                                  p + total, len - total, &d->toggle_in, tmo);
        if (r == -1000) break;              
        if (r < 0) return total ? (int)total : r;
        if (r == 0) {
            if (total == 0) break;          
             

            if (++stalls >= 10) break;
            tmo = 2000;
            continue;
        }
        stalls = 0;
        total += (uint32_t)r;
        if ((uint32_t)r % mp != 0) break;   
        tmo = 2000;                         
    }
    return (int)total;
}

int usbnet_get_string_ascii(usbnet_dev_t *d, uint8_t idx, char *out, int outsz) {
    if (idx == 0) return -1;
    uint8_t buf[64];
    int r = usbnet_ctrl(d, USB_REQ_DIR_D2H | USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_DEVICE,
                        USB_REQ_GET_DESCRIPTOR, (uint16_t)((USB_DESC_STRING << 8) | idx), 0x0409, buf, sizeof(buf));
    if (r != 0 || buf[0] < 2) return -1;
    int blen = buf[0]; if (blen > (int)sizeof(buf)) blen = sizeof(buf);
    int n = 0;
    for (int i = 2; i + 1 < blen && n + 1 < outsz; i += 2) {
        uint16_t cp = (uint16_t)(buf[i] | (buf[i + 1] << 8));
        out[n++] = (cp < 128) ? (char)cp : '?';
    }
    out[n] = '\0';
    return 0;
}

int usbnet_parse_mac_hex(const char *s, uint8_t out[6]) {
    int n = 0;
    for (int i = 0; i < 6; i++) {
        uint8_t v = 0;
        for (int k = 0; k < 2; k++) {
            char c = s[n++];
            uint8_t d;
            if (c >= '0' && c <= '9') d = (uint8_t)(c - '0');
            else if (c >= 'A' && c <= 'F') d = (uint8_t)(c - 'A' + 10);
            else if (c >= 'a' && c <= 'f') d = (uint8_t)(c - 'a' + 10);
            else return -1;
            v = (uint8_t)((v << 4) | d);
        }
        out[i] = v;
    }
    return 0;
}

 


static int nif_tx(netif_t *nif, const uint8_t *frame, uint16_t len) {
    usbnet_dev_t *d = (usbnet_dev_t *)nif->drv;
    if (!d || !d->used || !d->tx) return -1;
    return d->tx(d, frame, len);
}
static int nif_rx(netif_t *nif, uint8_t *buf, uint16_t bufsz) {
    usbnet_dev_t *d = (usbnet_dev_t *)nif->drv;
    if (!d || !d->used || !d->rx_poll) return 0;
    return d->rx_poll(d, buf, bufsz);
}
static void nif_service(netif_t *nif) {
    usbnet_dev_t *d = (usbnet_dev_t *)nif->drv;
    if (d && d->used && d->service) d->service(d);
}

 


int usbnet_probe(usb_hcd_t *hcd, uint8_t addr, uint8_t speed, uint8_t maxp0,
                 const uint8_t *cfg, uint16_t cfg_len) {
    if (!cfg || cfg_len < 9) return 0;

    usbnet_parse_t parsed;
    usbnet_parse_config(cfg, cfg_len, &parsed);
    if (parsed.n_iface == 0) return 0;

    int slot = -1;
    for (int i = 0; i < USBNET_MAX_DEV; i++) if (!g_dev[i].used) { slot = i; break; }
    if (slot < 0) { DBG_MSG("NET", "USBNET_MAX_DEV reached, ignoring device"); return 0; }

    usbnet_dev_t *d = &g_dev[slot];
    memset(d, 0, sizeof(*d));
    d->hcd = hcd; d->addr = addr; d->speed = speed; d->maxp0 = maxp0;

    int kind = 0;
    if ((kind = rndis_try_attach(d, &parsed)) == 0) d->kind = USBNET_KIND_RNDIS;
    else if ((kind = cdc_ecm_try_attach(d, &parsed)) == 0) d->kind = USBNET_KIND_ECM;
    else if ((kind = cdc_ncm_try_attach(d, &parsed)) == 0) d->kind = USBNET_KIND_NCM;
    else { DBG_VAL("NET", "no matching driver (RNDIS/ECM/NCM) for device addr", addr); return 0; }

    d->used = true;

    netif_t *nif = netif_create("usb0", &d->mac);
    if (!nif) { DBG_MSG("NET", "netif slot busy (already have a network card)"); d->used = false; return 0; }
    nif->tx = nif_tx; nif->rx_poll = nif_rx; nif->service = nif_service; nif->drv = d;
    d->nif = nif;

    static const char *names[] = { "?", "RNDIS", "CDC-ECM", "CDC-NCM" };
    dbg_puts("[NET] driver: "); dbg_puts(names[d->kind]); dbg_puts("\n");    
    DBG_VAL("NET", "  mac[0..1]", ((uint32_t)d->mac.b[0] << 8) | d->mac.b[1]);

    netif_set_link(nif, true);
    return 1;
}

void usbnet_detach(usb_hcd_t *hcd, uint8_t addr) {
    for (int i = 0; i < USBNET_MAX_DEV; i++) {
        usbnet_dev_t *d = &g_dev[i];
        if (d->used && d->hcd == hcd && d->addr == addr) {
            if (d->nif) netif_remove(d->nif);
            d->used = false;
            DBG_VAL("NET", "USB network card detached, addr", addr);
        }
    }
}
