/* Fexox OS Project, EULA GPLv2 */
 


#include "net_usb.h"
#include "USB.h"
#include "debug_out.h"

extern void  *memset(void *dst, int c, size_t n);
extern void  *memcpy(void *dst, const void *src, size_t n);
extern void  usb_delay_ms(uint32_t ms);

#define RNDIS_PACKET_MSG           0x00000001u
#define RNDIS_INITIALIZE_MSG       0x00000002u
#define RNDIS_INITIALIZE_CMPLT     0x80000002u
#define RNDIS_QUERY_MSG            0x00000004u
#define RNDIS_QUERY_CMPLT          0x80000004u
#define RNDIS_SET_MSG              0x00000005u
#define RNDIS_SET_CMPLT            0x80000005u

#define OID_802_3_PERMANENT_ADDRESS   0x01010101u
#define OID_802_3_CURRENT_ADDRESS     0x01010102u
#define OID_GEN_CURRENT_PACKET_FILTER 0x0001010Eu

#define CDC_SEND_ENCAPSULATED_COMMAND 0x00u
#define CDC_GET_ENCAPSULATED_RESPONSE 0x01u

#define RNDIS_RXBUF_SIZE 16384

typedef struct {
    bool     used;
    uint32_t req_id;
    uint8_t  rxbuf[RNDIS_RXBUF_SIZE];
    uint32_t rx_len, rx_pos;
} rndis_ctx_t;

static rndis_ctx_t g_ctx[USBNET_MAX_DEV];
 
static uint32_t g_rndis_dbg[3];
void rndis_dbg(uint32_t out[3]) { out[0] = g_rndis_dbg[0]; out[1] = g_rndis_dbg[1]; out[2] = g_rndis_dbg[2]; }

static rndis_ctx_t *ctx_alloc(void) {
    for (int i = 0; i < USBNET_MAX_DEV; i++)
        if (!g_ctx[i].used) { memset(&g_ctx[i], 0, sizeof(g_ctx[i])); g_ctx[i].used = true; return &g_ctx[i]; }
    return NULL;
}

static int rndis_send(usbnet_dev_t *d, const uint8_t *msg, uint16_t len) {
    return usbnet_ctrl(d, USB_REQ_DIR_H2D | USB_REQ_TYPE_CLASS | USB_REQ_RECIP_INTERFACE,
                       CDC_SEND_ENCAPSULATED_COMMAND, 0, d->ctrl_iface, (void *)msg, len);
}

static int rndis_recv(usbnet_dev_t *d, uint8_t *buf, uint16_t bufsz, uint32_t timeout_ms) {
    uint32_t waited = 0;
    while (waited <= timeout_ms) {
        memset(buf, 0, bufsz);
        int r = usbnet_ctrl(d, USB_REQ_DIR_D2H | USB_REQ_TYPE_CLASS | USB_REQ_RECIP_INTERFACE,
                            CDC_GET_ENCAPSULATED_RESPONSE, 0, d->ctrl_iface, buf, bufsz);
        if (r == 0 && get_le32(buf) != 0 && get_le32(buf + 4) >= 8 && get_le32(buf + 4) <= bufsz)
            return 0;
        usb_delay_ms(20);
        waited += 20;
    }
    return -1;
}

static int rndis_query(usbnet_dev_t *d, uint32_t oid, uint8_t *out, uint32_t *inout_len) {
    rndis_ctx_t *c = (rndis_ctx_t *)d->priv;
    uint8_t msg[28];
    put_le32(msg + 0, RNDIS_QUERY_MSG);
    put_le32(msg + 4, 28);
    put_le32(msg + 8, ++c->req_id);
    put_le32(msg + 12, oid);
    put_le32(msg + 16, 0);
    put_le32(msg + 20, 20);
    put_le32(msg + 24, 0);
    if (rndis_send(d, msg, sizeof(msg)) != 0) return -1;

    uint8_t rsp[256];
    if (rndis_recv(d, rsp, sizeof(rsp), 800) != 0) return -1;
    if (get_le32(rsp) != RNDIS_QUERY_CMPLT || get_le32(rsp + 8) != c->req_id) return -1;
    if (get_le32(rsp + 12) != 0) return -1;
    uint32_t ilen = get_le32(rsp + 16), ioff = get_le32(rsp + 20);
    uint32_t abs = 8 + ioff;
    if (abs + ilen > sizeof(rsp) || ilen > *inout_len) return -1;
    memcpy(out, rsp + abs, ilen);
    *inout_len = ilen;
    return 0;
}

static int rndis_set(usbnet_dev_t *d, uint32_t oid, const uint8_t *val, uint32_t vlen) {
    rndis_ctx_t *c = (rndis_ctx_t *)d->priv;
    uint8_t msg[28 + 8];
    if (vlen > 8) return -1;
    uint32_t total = 28 + vlen;
    put_le32(msg + 0, RNDIS_SET_MSG);
    put_le32(msg + 4, total);
    put_le32(msg + 8, ++c->req_id);
    put_le32(msg + 12, oid);
    put_le32(msg + 16, vlen);
    put_le32(msg + 20, 20);
    put_le32(msg + 24, 0);
    memcpy(msg + 28, val, vlen);
    if (rndis_send(d, msg, (uint16_t)total) != 0) return -1;

    uint8_t rsp[16];
    if (rndis_recv(d, rsp, sizeof(rsp), 800) != 0) return -1;
    if (get_le32(rsp) != RNDIS_SET_CMPLT || get_le32(rsp + 8) != c->req_id) return -1;
    return get_le32(rsp + 12) == 0 ? 0 : -1;
}

 


static int rndis_tx(void *dev, const uint8_t *frame, uint16_t len) {
    usbnet_dev_t *d = (usbnet_dev_t *)dev;
    uint8_t buf[44 + NET_FRAME_MAX];
    if ((uint32_t)len + 44 > sizeof(buf)) return -1;
    memset(buf, 0, 44);
    put_le32(buf + 0, RNDIS_PACKET_MSG);
    put_le32(buf + 4, 44 + len);
    put_le32(buf + 8, 36);
    put_le32(buf + 12, len);
    memcpy(buf + 44, frame, len);
    return usbnet_bulk_out(d, buf, 44 + len);
}

 



static int rndis_rx_poll(void *dev, uint8_t *out, uint16_t outsz) {
    usbnet_dev_t *d = (usbnet_dev_t *)dev;
    rndis_ctx_t *c = (rndis_ctx_t *)d->priv;

     





    for (;;) {
        while (c->rx_pos + 8 <= c->rx_len) {
            uint32_t type = get_le32(c->rxbuf + c->rx_pos);
            uint32_t mlen = get_le32(c->rxbuf + c->rx_pos + 4);
            if (type != RNDIS_PACKET_MSG || mlen < 44 || mlen > RNDIS_RXBUF_SIZE) {    
                g_rndis_dbg[1]++; c->rx_pos = c->rx_len; break;
            }
            if (c->rx_pos + mlen > c->rx_len) break;                                     
            uint32_t doff = get_le32(c->rxbuf + c->rx_pos + 8);
            uint32_t dlen = get_le32(c->rxbuf + c->rx_pos + 12);
            uint32_t dabs = c->rx_pos + 8 + doff;
            uint32_t mend = c->rx_pos + mlen;
            c->rx_pos = mend;
            if (dabs + dlen > mend || dlen == 0 || dlen > outsz) continue;
            memcpy(out, c->rxbuf + dabs, dlen);
            g_rndis_dbg[0]++;
            return (int)dlen;
        }
         
        uint32_t left = c->rx_len - c->rx_pos;
        if (c->rx_pos > 0 && left > 0) { for (uint32_t i = 0; i < left; i++) c->rxbuf[i] = c->rxbuf[c->rx_pos + i]; }
        c->rx_pos = 0; c->rx_len = left;
        if (left + 1024 > sizeof(c->rxbuf)) { g_rndis_dbg[1]++; c->rx_len = 0; left = 0; }    
        int r = usbnet_bulk_in(d, c->rxbuf + c->rx_len, (uint32_t)(sizeof(c->rxbuf) - c->rx_len));
        if (r <= 0) return 0;
        c->rx_len += (uint32_t)r;
        g_rndis_dbg[2] += (uint32_t)r;
    }
}

 


int rndis_try_attach(usbnet_dev_t *d, usbnet_parse_t *p) {
    int ctrl_num = -1;
    for (int i = 0; i < p->n_iface && ctrl_num < 0; i++) {
        usbnet_iface_t *ifc = &p->iface[i];
        for (int a = 0; a < ifc->n_alt; a++) {
            usbnet_altset_t *as = &ifc->alt[a];
            bool is_rndis_ctrl = (as->class_ == 0x02 && as->subclass == 0x02 && as->proto == 0xFF) ||
                                 (as->class_ == 0xE0 && as->subclass == 0x01 && as->proto == 0x03);
            if (is_rndis_ctrl) { ctrl_num = ifc->num; break; }
        }
    }
    if (ctrl_num < 0) return -1;

    int data_num = (p->union_master == ctrl_num) ? p->union_slave : -1;
    if (data_num < 0) {
        for (int i = 0; i < p->n_iface; i++)
            if (p->iface[i].num != ctrl_num && p->iface[i].alt[0].class_ == 0x0A) { data_num = p->iface[i].num; break; }
    }
    if (data_num < 0) return -1;

    usbnet_iface_t *data_ifc = usbnet_find_iface(p, (uint8_t)data_num);
    uint8_t in_ep, out_ep; uint16_t in_mp, out_mp;
    if (!usbnet_find_bulk_alt(data_ifc, &in_ep, &out_ep, &in_mp, &out_mp)) return -1;

    rndis_ctx_t *c = ctx_alloc();
    if (!c) { DBG_MSG("NET", "RNDIS: no free context slots"); return -1; }

    d->ctrl_iface = (uint8_t)ctrl_num;
    d->data_iface = (uint8_t)data_num;
    d->bulk_in_ep = in_ep; d->bulk_out_ep = out_ep;
    d->bulk_in_maxp = in_mp; d->bulk_out_maxp = out_mp;
    d->priv = c;

    uint8_t msg[24];
    put_le32(msg + 0, RNDIS_INITIALIZE_MSG);
    put_le32(msg + 4, 24);
    put_le32(msg + 8, ++c->req_id);
    put_le32(msg + 12, 1);          
    put_le32(msg + 16, 0);          
    put_le32(msg + 20, 16384);      
    if (rndis_send(d, msg, sizeof(msg)) != 0) { c->used = false; return -1; }

    uint8_t rsp[52];
    if (rndis_recv(d, rsp, sizeof(rsp), 1500) != 0 ||
        get_le32(rsp) != RNDIS_INITIALIZE_CMPLT || get_le32(rsp + 8) != c->req_id || get_le32(rsp + 12) != 0) {
        DBG_MSG("NET", "RNDIS INITIALIZE failed");
        c->used = false;
        return -1;
    }

    uint8_t mac[6]; uint32_t mlen = 6;
    if (rndis_query(d, OID_802_3_PERMANENT_ADDRESS, mac, &mlen) != 0 || mlen != 6) {
        mlen = 6;
        if (rndis_query(d, OID_802_3_CURRENT_ADDRESS, mac, &mlen) != 0 || mlen != 6) {
            DBG_MSG("NET", "RNDIS: cannot query MAC address");
            c->used = false;
            return -1;
        }
    }
    for (int i = 0; i < 6; i++) d->mac.b[i] = mac[i];

    uint8_t filt[4]; put_le32(filt, 0x0000000Bu);       
    rndis_set(d, OID_GEN_CURRENT_PACKET_FILTER, filt, 4);

    d->tx = rndis_tx;
    d->rx_poll = rndis_rx_poll;
    d->service = NULL;
    return 0;
}
