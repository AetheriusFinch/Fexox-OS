/* Fexox OS Project, EULA GPLv2 */
#ifndef USBMASSSTORAGE_H
#define USBMASSSTORAGE_H

#include <stdint.h>

 






struct usb_hcd;    

 









int usb_msc_attach(struct usb_hcd *hcd, uint8_t dev_addr, uint8_t speed,
                   uint8_t bulk_in_ep, uint16_t bulk_in_maxp,
                   uint8_t bulk_out_ep, uint16_t bulk_out_maxp);

 
void usb_msc_get_stats(uint32_t *errors, uint32_t *recovered, int *last_rc, uint64_t *last_lba);

#endif  
