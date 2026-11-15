/* Fexox OS Project, EULA GPLv2 */
#ifndef USBHID_H
#define USBHID_H

#include <stdint.h>
#include <stdbool.h>

 






struct usb_hcd;    

 



void usb_hid_attach(struct usb_hcd *hcd, uint8_t dev_addr, uint8_t speed,
                    uint8_t maxp0, uint8_t iface_num, uint8_t protocol);

 


void usb_hid_poll_all(void);

 



uint8_t usb_hid_mouse_buttons(void);

 




bool usb_hid_kbd_read_char(char *out_ch);

 

void usb_hid_kbd_flush(void);

#endif  
