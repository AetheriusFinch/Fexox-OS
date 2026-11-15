/* Fexox OS Project, EULA GPLv2 */
 

#include "net_usb.h"
#include "USB.h"
#include "debug_out.h"

extern void *memset(void *dst, int c, size_t n);

static int cdc_set_interface(usbnet_dev_t *d, uint8_t iface, uint8_t alt) {
    return usbnet_ctrl(d, USB_REQ_DIR_H2D | USB_REQ_TYPE_STANDARD | USB_REQ_RECIP_INTERFACE,
                       USB_REQ_SET_INTERFACE, alt, iface, NULL, 0);
}

 


static void cdc_set_packet_filter(usbnet_dev_t *d) {
    usbnet_ctrl(d, USB_REQ_DIR_H2D | USB_REQ_TYPE_CLASS | USB_REQ_RECIP_INTERFACE,
               0x43, 0x000C  , d->ctrl_iface, NULL, 0);
}

static int ecm_tx(void *dev, const uint8_t *frame, uint16_t len) {
    return usbnet_bulk_out((usbnet_dev_t *)dev, frame, len);
}
static int ecm_rx_poll(void *dev, uint8_t *out, uint16_t outsz) {
    int r = usbnet_bulk_in((usbnet_dev_t *)dev, out, outsz);
    return r > 0 ? r : 0;
}

int cdc_ecm_try_attach(usbnet_dev_t *d, usbnet_parse_t *p) {
    int ctrl_num = -1;
    for (int i = 0; i < p->n_iface && ctrl_num < 0; i++)
        if (p->iface[i].alt[0].class_ == 0x02 && p->iface[i].alt[0].subclass == 0x06)
            ctrl_num = p->iface[i].num;
    if (ctrl_num < 0 || !p->have_ecm_desc) return -1;                

    int data_num = (p->union_master == ctrl_num) ? p->union_slave : -1;
    if (data_num < 0)
        for (int i = 0; i < p->n_iface; i++)
            if (p->iface[i].num != ctrl_num && p->iface[i].alt[0].class_ == 0x0A) { data_num = p->iface[i].num; break; }
    if (data_num < 0) return -1;

    usbnet_iface_t *data_ifc = usbnet_find_iface(p, (uint8_t)data_num);
    uint8_t in_ep, out_ep; uint16_t in_mp, out_mp;
    usbnet_altset_t *chosen = usbnet_find_bulk_alt(data_ifc, &in_ep, &out_ep, &in_mp, &out_mp);
    if (!chosen) return -1;

    d->ctrl_iface = (uint8_t)ctrl_num;
    d->data_iface = (uint8_t)data_num;
    d->bulk_in_ep = in_ep; d->bulk_out_ep = out_ep;
    d->bulk_in_maxp = in_mp; d->bulk_out_maxp = out_mp;

    if (chosen->alt != 0 || data_ifc->n_alt > 1) {
         

        cdc_set_interface(d, (uint8_t)data_num, chosen->alt);
    }

    char macstr[16];
    if (usbnet_get_string_ascii(d, p->ecm_mac_str_idx, macstr, sizeof(macstr)) != 0 ||
        usbnet_parse_mac_hex(macstr, d->mac.b) != 0) {
        DBG_MSG("NET", "CDC-ECM: cannot read MAC string descriptor");
        return -1;
    }

    cdc_set_packet_filter(d);

    d->tx = ecm_tx;
    d->rx_poll = ecm_rx_poll;
    d->service = NULL;
    return 0;
}
