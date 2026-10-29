/* Fexox OS Project, EULA GPLv2 */
 



#ifndef USB_H
#define USB_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

 


#define USB_REQ_DIR_H2D          0x00u    
#define USB_REQ_DIR_D2H          0x80u    
#define USB_REQ_TYPE_STANDARD    0x00u
#define USB_REQ_TYPE_CLASS       0x20u
#define USB_REQ_TYPE_VENDOR      0x40u
#define USB_REQ_RECIP_DEVICE     0x00u
#define USB_REQ_RECIP_INTERFACE  0x01u
#define USB_REQ_RECIP_ENDPOINT   0x02u
#define USB_REQ_RECIP_OTHER      0x03u    

 
#define USB_REQ_GET_STATUS        0x00u
#define USB_REQ_CLEAR_FEATURE     0x01u
#define USB_REQ_SET_FEATURE       0x03u
#define USB_REQ_SET_ADDRESS       0x05u
#define USB_REQ_GET_DESCRIPTOR    0x06u
#define USB_REQ_SET_DESCRIPTOR    0x07u
#define USB_REQ_GET_CONFIGURATION 0x08u
#define USB_REQ_SET_CONFIGURATION 0x09u
#define USB_REQ_SET_INTERFACE     0x0Bu

 
#define USB_DESC_DEVICE           0x01u
#define USB_DESC_CONFIGURATION    0x02u
#define USB_DESC_STRING           0x03u
#define USB_DESC_INTERFACE        0x04u
#define USB_DESC_ENDPOINT         0x05u

 



#define USB_SPEED_LOW    0    
#define USB_SPEED_FULL   1    
#define USB_SPEED_HIGH   2    
#define USB_SPEED_SUPER  3    

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

 


typedef struct usb_hcd usb_hcd_t;

typedef struct {
     














    int (*control_transfer)(usb_hcd_t *hcd, uint8_t dev_addr, uint8_t speed,
                            uint8_t max_packet0, const usb_setup_pkt_t *setup,
                            void *data, uint16_t len);

     






    bool (*port_reset)(usb_hcd_t *hcd, uint8_t port, uint8_t *out_speed);

     

















    int (*bulk_transfer)(usb_hcd_t *hcd, uint8_t dev_addr, uint8_t speed,
                         uint8_t ep_addr, uint16_t max_packet,
                         void *data, uint32_t len, uint8_t *toggle_inout);

     









    void (*set_route)(usb_hcd_t *hcd, uint8_t dev_addr,
                      uint8_t tt_hub_addr, uint8_t tt_port);

     









    bool (*port_connected)(usb_hcd_t *hcd, uint8_t port, bool *out_connected);

     






    int (*int_in_poll)(usb_hcd_t *hcd, uint8_t dev_addr, uint8_t speed,
                       uint8_t ep_addr, uint16_t max_packet,
                       void *data, uint32_t len, uint8_t *toggle_inout,
                       uint32_t timeout_us);

     








    int (*bulk_in_poll)(usb_hcd_t *hcd, uint8_t dev_addr, uint8_t speed,
                        uint8_t ep_addr, uint16_t max_packet,
                        void *data, uint32_t len, uint8_t *toggle_inout,
                        uint32_t timeout_us);

     

    bool supports_hubs;
} usb_hcd_ops_t;

struct usb_hcd {
    const usb_hcd_ops_t *ops;
    void                *priv;     
    const char          *name;     
    uint8_t              num_ports;
};

 


void usb_register_hcd(usb_hcd_t *hcd);

 


void     usb_delay_ms(uint32_t ms);
uint64_t usb_tsc_per_ms(void);

 


#define USB_DEV_LIST_MAX 12
typedef struct {
    uint16_t vid, pid;
    uint8_t  dev_class;    
    uint8_t  depth;        
} usb_dev_info_t;

typedef struct {
    uint8_t hcds;             
    uint8_t devices;          
    uint8_t hubs;             
    uint8_t hid;              
    uint8_t msc;              
    uint8_t split_skipped;    
    uint8_t enum_fail;        
    uint8_t fail_step;        
    uint8_t net;              
    int8_t  fail_rc;          
    uint8_t n_list;
    usb_dev_info_t list[USB_DEV_LIST_MAX];
} usb_stats_t;

const usb_stats_t *usb_get_stats(void);

 



void usb_init(void);

 



void usb_hotplug_poll(void);

#endif  
