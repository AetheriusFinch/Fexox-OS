/* Fexox OS Project, EULA GPLv2 */
 








#ifndef NET_USB_H
#define NET_USB_H

#include "net.h"

 

struct usb_hcd;

#define USBNET_MAX_DEV     4
#define USBNET_MAX_IFACES  8
#define USBNET_MAX_ALTS    4
#define USBNET_MAX_EPS     4

typedef struct {
    uint8_t addr;                           
    uint8_t attr;                           
    uint16_t maxp;
} usbnet_ep_t;

typedef struct {
    uint8_t     alt;                        
    uint8_t     class_, subclass, proto;
    uint8_t     n_ep;
    usbnet_ep_t ep[USBNET_MAX_EPS];
} usbnet_altset_t;

typedef struct {
    uint8_t         num;                    
    uint8_t         n_alt;
    usbnet_altset_t alt[USBNET_MAX_ALTS];
} usbnet_iface_t;

 
typedef struct {
    usbnet_iface_t iface[USBNET_MAX_IFACES];
    int            n_iface;
     
    int  union_master, union_slave;         
    bool have_ecm_desc;
    uint8_t ecm_mac_str_idx;
    uint8_t ecm_addr[6];                    
    bool ecm_addr_valid;
} usbnet_parse_t;

void usbnet_parse_config(const uint8_t *cfg, uint16_t len, usbnet_parse_t *out);
usbnet_iface_t *usbnet_find_iface(usbnet_parse_t *p, uint8_t num);
 
usbnet_altset_t *usbnet_find_bulk_alt(usbnet_iface_t *ifc, uint8_t *out_in, uint8_t *out_out,
                                      uint16_t *out_in_mp, uint16_t *out_out_mp);

 
typedef struct {
    bool             used;
    struct usb_hcd  *hcd;
    uint8_t          addr, speed, maxp0;
    uint8_t          ctrl_iface, data_iface;

    uint8_t          bulk_in_ep, bulk_out_ep;
    uint16_t         bulk_in_maxp, bulk_out_maxp;
    uint8_t          toggle_in, toggle_out;

    mac_t            mac;
    netif_t         *nif;

    int              kind;                  
    void            *priv;                  

     
    int  (*tx)(void *dev, const uint8_t *frame, uint16_t len);
    int  (*rx_poll)(void *dev, uint8_t *buf, uint16_t bufsz);
    void (*service)(void *dev);
} usbnet_dev_t;

enum { USBNET_KIND_NONE = 0, USBNET_KIND_RNDIS, USBNET_KIND_ECM, USBNET_KIND_NCM };

 
int  usbnet_ctrl(usbnet_dev_t *d, uint8_t bmReq, uint8_t bReq, uint16_t wValue, uint16_t wIndex,
                 void *data, uint16_t wLength);
int  usbnet_bulk_out(usbnet_dev_t *d, const void *data, uint32_t len);
int  usbnet_bulk_in(usbnet_dev_t *d, void *data, uint32_t len);
 
int  usbnet_get_string_ascii(usbnet_dev_t *d, uint8_t idx, char *out, int outsz);
 
int  usbnet_parse_mac_hex(const char *s, uint8_t out[6]);

 
int  rndis_try_attach(usbnet_dev_t *d, usbnet_parse_t *p);
int  cdc_ecm_try_attach(usbnet_dev_t *d, usbnet_parse_t *p);
int  cdc_ncm_try_attach(usbnet_dev_t *d, usbnet_parse_t *p);

#endif  
