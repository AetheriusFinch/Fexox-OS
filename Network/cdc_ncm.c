/* Fexox OS Project, EULA GPLv2 */
 

#include "net_usb.h"
#include "USB.h"
#include "debug_out.h"

extern void *memset(void *dst, int c, size_t n);
extern void *memcpy(void *dst, const void *src, size_t n);

#define NTH16_SIG  0x484D434Eu    
#define NDP16_SIG  0x304D434Eu    

static int cdc_set_interface(usbnet_dev_t *d, uint8_t iface, uint8_t alt) {
    return usbnet_ctrl(d, USB_REQ_DIR_H2D | USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_INTERFACE,
                       USB_REQ_SET_INTERFACE, alt, iface, NULL, 0);
}

 


static uint16_t g_ncm_seq;

static int ncm_tx(void *dev, const uint8_t *frame, uint16_t len) {
    usbnet_dev_t *d = (usbnet_dev_t *)dev;
    uint8_t buf[12 + NET_FRAME_MAX + 16 + 4];
    if ((uint32_t)len + 12 + 16 + 4 > sizeof(buf)) return -1;

    uint32_t ndp_off = (12 + len + 3) & ~3u;           
    uint32_t total = ndp_off + 16;

    memset(buf, 0, total);
    put_le32(buf + 0, NTH16_SIG);
    put_le16(buf + 4, 12);
    put_le16(buf + 6, ++g_ncm_seq);
    put_le16(buf + 8, (uint16_t)(total > 0xFFFF ? 0 : total));
    put_le16(buf + 10, (uint16_t)ndp_off);

    memcpy(buf + 12, frame, len);

    uint8_t *ndp = buf + ndp_off;
    put_le32(ndp + 0, NDP16_SIG);
    put_le16(ndp + 4, 16);
    put_le16(ndp + 6, 0);
    put_le16(ndp + 8, 12);               
    put_le16(ndp + 10, len);
    put_le16(ndp + 12, 0);               
    put_le16(ndp + 14, 0);

    return usbnet_bulk_out(d, buf, total);
}

 




#define NCM_RXBUF_SIZE 16384

typedef struct {
    bool     used;
    uint8_t  buf[NCM_RXBUF_SIZE];
    uint32_t len;
    uint32_t ndp_off;      
    uint32_t entry_off;    
} ncm_ctx_t;

static ncm_ctx_t g_ncm_ctx[USBNET_MAX_DEV];

static ncm_ctx_t *ctx_alloc(void) {
    for (int i = 0; i < USBNET_MAX_DEV; i++)
        if (!g_ncm_ctx[i].used) { memset(&g_ncm_ctx[i], 0, sizeof(g_ncm_ctx[i])); g_ncm_ctx[i].used = true; return &g_ncm_ctx[i]; }
    return NULL;
}

static bool ncm_load_next_ntb(usbnet_dev_t *d, ncm_ctx_t *c) {
    int r = usbnet_bulk_in(d, c->buf, sizeof(c->buf));
    if (r < 12) return false;
    c->len = (uint32_t)r;
    if (get_le32(c->buf) != NTH16_SIG) return false;
    uint16_t hdr_len = get_le16(c->buf + 4);
    if (hdr_len < 12 || hdr_len > c->len) return false;
    c->ndp_off = get_le16(c->buf + 10);
    c->entry_off = 8;                                    
    return true;
}

static int ncm_rx_poll(void *dev, uint8_t *out, uint16_t outsz) {
    usbnet_dev_t *d = (usbnet_dev_t *)dev;
    ncm_ctx_t *c = (ncm_ctx_t *)d->priv;

    for (;;) {
        while (c->ndp_off != 0xFFFFFFFFu && c->ndp_off + 8 <= c->len) {
            uint8_t *ndp = c->buf + c->ndp_off;
            uint32_t sig = get_le32(ndp);
            uint16_t ndp_len = get_le16(ndp + 4);
            uint16_t next_ndp = get_le16(ndp + 6);
            if ((sig & 0x00FFFFFFu) != (NDP16_SIG & 0x00FFFFFFu) || ndp_len < 8 || c->ndp_off + ndp_len > c->len) {
                c->ndp_off = 0xFFFFFFFFu;
                break;
            }
            while (c->entry_off + 4 <= ndp_len) {
                uint16_t d_idx = get_le16(ndp + c->entry_off);
                uint16_t d_len = get_le16(ndp + c->entry_off + 2);
                c->entry_off += 4;
                if (d_idx == 0 && d_len == 0) break;                        
                uint32_t abs = c->ndp_off - c->ndp_off + d_idx;              
                abs = d_idx;
                if ((uint32_t)abs + d_len > c->len || d_len == 0 || d_len > outsz) continue;
                memcpy(out, c->buf + abs, d_len);
                return (int)d_len;
            }
             
            c->ndp_off = next_ndp ? next_ndp : 0xFFFFFFFFu;
            c->entry_off = 8;
        }
        if (!ncm_load_next_ntb(d, c)) return 0;
    }
}

 


int cdc_ncm_try_attach(usbnet_dev_t *d, usbnet_parse_t *p) {
    int ctrl_num = -1;
    for (int i = 0; i < p->n_iface && ctrl_num < 0; i++)
        if (p->iface[i].alt[0].class_ == 0x02 && p->iface[i].alt[0].subclass == 0x0D)
            ctrl_num = p->iface[i].num;
    if (ctrl_num < 0) return -1;

    int data_num = (p->union_master == ctrl_num) ? p->union_slave : -1;
    if (data_num < 0)
        for (int i = 0; i < p->n_iface; i++)
            if (p->iface[i].num != ctrl_num && p->iface[i].alt[0].class_ == 0x0A) { data_num = p->iface[i].num; break; }
    if (data_num < 0) return -1;

    usbnet_iface_t *data_ifc = usbnet_find_iface(p, (uint8_t)data_num);
    uint8_t in_ep, out_ep; uint16_t in_mp, out_mp;
    usbnet_altset_t *chosen = usbnet_find_bulk_alt(data_ifc, &in_ep, &out_ep, &in_mp, &out_mp);
    if (!chosen) return -1;

    ncm_ctx_t *c = ctx_alloc();
    if (!c) { DBG_MSG("NET", "NCM: no free context slots"); return -1; }
    c->ndp_off = 0xFFFFFFFFu;

    d->ctrl_iface = (uint8_t)ctrl_num;
    d->data_iface = (uint8_t)data_num;
    d->bulk_in_ep = in_ep; d->bulk_out_ep = out_ep;
    d->bulk_in_maxp = in_mp; d->bulk_out_maxp = out_mp;
    d->priv = c;

    if (chosen->alt != 0 || data_ifc->n_alt > 1)
        cdc_set_interface(d, (uint8_t)data_num, chosen->alt);

    if (p->have_ecm_desc) {
        char macstr[16];
        if (usbnet_get_string_ascii(d, p->ecm_mac_str_idx, macstr, sizeof(macstr)) == 0)
            usbnet_parse_mac_hex(macstr, d->mac.b);
    }
    if (d->mac.b[0] == 0 && d->mac.b[1] == 0 && d->mac.b[2] == 0 &&
        d->mac.b[3] == 0 && d->mac.b[4] == 0 && d->mac.b[5] == 0) {
         


        d->mac.b[0] = 0x02; d->mac.b[1] = 0xFE; d->mac.b[2] = 0xC0;
        d->mac.b[3] = (uint8_t)(d->addr); d->mac.b[4] = 0x00; d->mac.b[5] = 0x01;
    }

    d->tx = ncm_tx;
    d->rx_poll = ncm_rx_poll;
    d->service = NULL;
    return 0;
}
