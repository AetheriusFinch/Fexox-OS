/* Fexox OS Project, EULA GPLv2 */
 


#include "USBHID.h"
#include "USB.h"
#include "debug_out.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

extern void *memset(void *dst, int c, size_t n);
extern void *memcpy(void *dst, const void *src, size_t n);

 

extern void    ui_set_mouse_pos(int32_t x, int32_t y);
extern void    ui_get_mouse_pos(int32_t *out_x, int32_t *out_y);
extern uint32_t fb_width(void);
extern uint32_t fb_height(void);

 


#define USB_HID_MAX_DEVICES 8
#define USB_HID_PROTO_KEYBOARD 1
#define USB_HID_PROTO_MOUSE    2

typedef struct {
    bool       used;
    usb_hcd_t *hcd;
    uint8_t    addr, speed, maxp0, iface_num, protocol;
    uint8_t    last_report[8];
    uint8_t    last_buttons;    
    uint8_t    fail_count;      
    uint16_t   backoff;         
    uint32_t   total_fail;      
    uint32_t   polls_ok;        
    bool       logged_first;    
     
    uint8_t    int_ep;          
    uint16_t   int_maxp;
    uint8_t    int_toggle;      
    bool       use_int;         
    uint8_t    int_err;         
    uint32_t   int_rx;          
} usb_hid_dev_t;

static bool g_caps_lock = false;    

 






#define USB_HID_MAX_CONSECUTIVE_FAILS 5

static usb_hid_dev_t g_hid[USB_HID_MAX_DEVICES];
static int           g_hid_count = 0;
static uint32_t      g_kbd_keys_total = 0;    

 


#define KBD_QUEUE_SIZE 32
static char g_kbd_queue[KBD_QUEUE_SIZE];
static int  g_kbd_head = 0, g_kbd_tail = 0;

static void kbd_queue_push(char c) {
    int next = (g_kbd_tail + 1) % KBD_QUEUE_SIZE;
    if (next == g_kbd_head) return;    
    g_kbd_queue[g_kbd_tail] = c;
    g_kbd_tail = next;
}

 


static inline uint64_t kbd_irq_save(void) {
    uint64_t f;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(f) :: "memory");
    return f;
}
static inline void kbd_irq_restore(uint64_t f) {
    __asm__ volatile("pushq %0; popfq" :: "r"(f) : "memory", "cc");
}

bool usb_hid_kbd_read_char(char *out_ch) {
    uint64_t fl = kbd_irq_save();
    if (g_kbd_head == g_kbd_tail) { kbd_irq_restore(fl); return false; }
    if (out_ch) *out_ch = g_kbd_queue[g_kbd_head];
    g_kbd_head = (g_kbd_head + 1) % KBD_QUEUE_SIZE;
    kbd_irq_restore(fl);
    return true;
}

void usb_hid_kbd_flush(void) {
    uint64_t fl = kbd_irq_save();
    g_kbd_head = g_kbd_tail;
    kbd_irq_restore(fl);
}

 



static char hid_keycode_to_char(uint8_t code, bool shift) {
    if (code >= 0x04 && code <= 0x1D) {           
        char c = (char)('a' + (code - 0x04));
        bool upper = shift ^ g_caps_lock;         
        return upper ? (char)(c - 'a' + 'A') : c;
    }
    if (code >= 0x59 && code <= 0x62) {           
        static const char kp[] = "1234567890";
        return kp[code - 0x59];
    }
    if (code >= 0x1E && code <= 0x27) {           
        static const char digits[]    = "1234567890";
        static const char digits_sh[] = "!@#$%^&*()";
        int idx = code - 0x1E;
        return shift ? digits_sh[idx] : digits[idx];
    }
    switch (code) {
        case 0x28: return '\n';                
        case 0x2A: return '\b';                
        case 0x2B: return '\t';                
        case 0x2C: return ' ';                 
        case 0x2D: return shift ? '_' : '-';
        case 0x2E: return shift ? '+' : '=';
        case 0x2F: return shift ? '{' : '[';
        case 0x30: return shift ? '}' : ']';
        case 0x31: return shift ? '|' : '\\';
        case 0x33: return shift ? ':' : ';';
        case 0x34: return shift ? '"' : '\'';
        case 0x35: return shift ? '~' : '`';
        case 0x36: return shift ? '<' : ',';
        case 0x37: return shift ? '>' : '.';
        case 0x38: return shift ? '?' : '/';
        case 0x54: return '/';                 
        case 0x55: return '*';                 
        case 0x56: return '-';                 
        case 0x57: return '+';                 
        case 0x58: return '\n';                
        case 0x63: return '.';                 
        case 0x4C: return 0x7F;                
        default:   return 0;    
    }
}

 












static bool     g_ps2_checked = false;
static bool     g_ps2_present = false;
static bool     g_ps2_e0 = false;
static bool     g_ps2_shift_l = false, g_ps2_shift_r = false;
static uint32_t g_ps2_rx = 0;
static uint8_t  g_ps2_last = 0;

 
static uint8_t ps2_set1_to_hid(uint8_t sc, bool e0) {
    static const uint8_t row_num[10] = {0x1E,0x1F,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27};
    static const uint8_t row_q[10]   = {0x14,0x1A,0x08,0x15,0x17,0x1C,0x18,0x0C,0x12,0x13};  
    static const uint8_t row_a[9]    = {0x04,0x16,0x07,0x09,0x0A,0x0B,0x0D,0x0E,0x0F};       
    static const uint8_t row_z[7]    = {0x1D,0x1B,0x06,0x19,0x05,0x11,0x10};                 
    if (e0) {
        switch (sc) {
            case 0x1C: return 0x58;    
            case 0x35: return 0x54;    
            case 0x53: return 0x4C;    
            default:   return 0;
        }
    }
    if (sc >= 0x02 && sc <= 0x0B) return row_num[sc - 0x02];
    if (sc >= 0x10 && sc <= 0x19) return row_q[sc - 0x10];
    if (sc >= 0x1E && sc <= 0x26) return row_a[sc - 0x1E];
    if (sc >= 0x2C && sc <= 0x32) return row_z[sc - 0x2C];
    switch (sc) {
        case 0x0C: return 0x2D;      case 0x0D: return 0x2E;   
        case 0x0E: return 0x2A;      case 0x0F: return 0x2B;   
        case 0x1A: return 0x2F;      case 0x1B: return 0x30;   
        case 0x1C: return 0x28;   
        case 0x27: return 0x33;      case 0x28: return 0x34;   
        case 0x29: return 0x35;      case 0x2B: return 0x31;   
        case 0x33: return 0x36;      case 0x34: return 0x37;   
        case 0x35: return 0x38;      case 0x39: return 0x2C;   
         
        case 0x47: return 0x5F;  case 0x48: return 0x60;  case 0x49: return 0x61;
        case 0x4A: return 0x56;
        case 0x4B: return 0x5C;  case 0x4C: return 0x5D;  case 0x4D: return 0x5E;
        case 0x4E: return 0x57;
        case 0x4F: return 0x59;  case 0x50: return 0x5A;  case 0x51: return 0x5B;
        case 0x52: return 0x62;  case 0x53: return 0x63;
        case 0x37: return 0x55;   
        default:   return 0;
    }
}

 



static uint8_t ps2_set2_to_set1(uint8_t c, bool e0) {
    static const uint8_t map[][2] = {
        {0x16,0x02},{0x1E,0x03},{0x26,0x04},{0x25,0x05},{0x2E,0x06},{0x36,0x07},{0x3D,0x08},
        {0x3E,0x09},{0x46,0x0A},{0x45,0x0B},{0x4E,0x0C},{0x55,0x0D},{0x66,0x0E},{0x0D,0x0F},
        {0x15,0x10},{0x1D,0x11},{0x24,0x12},{0x2D,0x13},{0x2C,0x14},{0x35,0x15},{0x3C,0x16},
        {0x43,0x17},{0x44,0x18},{0x4D,0x19},{0x54,0x1A},{0x5B,0x1B},{0x5A,0x1C},
        {0x1C,0x1E},{0x1B,0x1F},{0x23,0x20},{0x2B,0x21},{0x34,0x22},{0x33,0x23},{0x3B,0x24},
        {0x42,0x25},{0x4B,0x26},{0x4C,0x27},{0x52,0x28},{0x0E,0x29},{0x12,0x2A},{0x5D,0x2B},
        {0x1A,0x2C},{0x22,0x2D},{0x21,0x2E},{0x2A,0x2F},{0x32,0x30},{0x31,0x31},{0x3A,0x32},
        {0x41,0x33},{0x49,0x34},{0x4A,0x35},{0x59,0x36},{0x29,0x39},{0x58,0x3A},
    };
    if (e0) {
        if (c == 0x5A) return 0x1C;    
        if (c == 0x4A) return 0x35;    
        if (c == 0x71) return 0x53;    
        return 0;
    }
    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (map[i][0] == c) return map[i][1];
    return 0;
}

static bool g_ps2_set2 = false;
static bool g_ps2_break2 = false;

static bool ps2_wait_ibf_clear(void) { for (int i = 0; i < 100000; i++) if (!(dbg_inb(0x64) & 0x02)) return true; return false; }
static bool ps2_wait_obf(void)       { for (int i = 0; i < 100000; i++) if (dbg_inb(0x64) & 0x01)    return true; return false; }

void ps2_kbd_poll(void) {
    if (!g_ps2_checked) {
        g_ps2_checked = true;
        uint8_t st = dbg_inb(0x64);
        g_ps2_present = (st != 0xFF);
        dbg_puts(g_ps2_present ? "[KBD] PS/2 controller (8042) present, polling keyboard\n"
                               : "[KBD] no PS/2 controller (0x64 = 0xFF), PS/2 keyboard disabled\n");
        if (g_ps2_present && !(st & 0x01) && ps2_wait_ibf_clear()) {
             
            dbg_outb(0x64, 0x20);
            if (ps2_wait_obf()) {
                uint8_t cb = dbg_inb(0x60);
                g_ps2_set2 = !(cb & 0x40);
                DBG_VAL("KBD", "8042 command byte", cb);
                dbg_puts(g_ps2_set2 ? "[KBD] translation OFF -> scancode set 2\n"
                                    : "[KBD] translation ON -> scancode set 1\n");
            }
        }
    }
    if (!g_ps2_present) return;

    for (int n = 0; n < 16; n++) {
        uint8_t st = dbg_inb(0x64);
        if (!(st & 0x01)) break;           
        if (st & 0x20) break;              
        uint8_t sc = dbg_inb(0x60);

        g_ps2_rx++;
        g_ps2_last = sc;
        if (g_ps2_rx <= 8) { DBG_VAL("KBD", "PS/2 scancode", sc); }

        if (sc == 0xE0) { g_ps2_e0 = true; continue; }
         


        if (sc == 0xE1 || sc == 0xFA || sc == 0xFE || sc == 0x00 || (sc == 0xAA && g_ps2_set2)) { g_ps2_e0 = false; continue; }

        bool brk;
        uint8_t make;
        bool e0 = g_ps2_e0;
        g_ps2_e0 = false;
        if (g_ps2_set2) {
            if (sc == 0xF0) { g_ps2_break2 = true; continue; }
            brk = g_ps2_break2;
            g_ps2_break2 = false;
            make = ps2_set2_to_set1(sc, e0);
            if (!make) continue;
        } else {
            brk  = (sc & 0x80) != 0;
            make = (uint8_t)(sc & 0x7F);
        }

        if (!e0 && make == 0x2A) { g_ps2_shift_l = !brk; continue; }
        if (!e0 && make == 0x36) { g_ps2_shift_r = !brk; continue; }
        if (brk) continue;                               
        if (!e0 && make == 0x3A) { g_caps_lock = !g_caps_lock; continue; }

        uint8_t hid = ps2_set1_to_hid(make, e0);
        if (!hid) continue;
        char ch = hid_keycode_to_char(hid, g_ps2_shift_l || g_ps2_shift_r);
        if (ch) {
            kbd_queue_push(ch);
            g_kbd_keys_total++;
            dbg_puts("[KBD] key '");
            if (ch == '\n') dbg_puts("\\n"); else if (ch == '\b') dbg_puts("\\b");
            else if (ch == '\t') dbg_puts("\\t"); else if (ch == 0x7F) dbg_puts("DEL");
            else dbg_putc(ch);
            dbg_puts("'\n");
        }
    }
}

 


void usb_hid_attach(usb_hcd_t *hcd, uint8_t addr, uint8_t speed, uint8_t maxp0,
                    uint8_t iface_num, uint8_t protocol) {
    if (protocol != USB_HID_PROTO_KEYBOARD && protocol != USB_HID_PROTO_MOUSE) {
        DBG_VAL("HID", "unknown protocol, ignored, addr", addr);
        return;
    }
    if (g_hid_count >= USB_HID_MAX_DEVICES) {
        DBG_MSG("HID", "device limit reached (USB_HID_MAX_DEVICES)");
        return;
    }

     


    usb_setup_pkt_t sp;
    sp.bmRequestType = USB_REQ_DIR_H2D | USB_REQ_TYPE_CLASS | USB_REQ_RECIP_INTERFACE;
    sp.bRequest = 0x0B;    
    sp.wValue   = 0;       
    sp.wIndex   = iface_num;
    sp.wLength  = 0;
    int rc_proto = hcd->ops->control_transfer(hcd, addr, speed, maxp0, &sp, NULL, 0);
    DBG_RC("HID", "SET_PROTOCOL(Boot) rc (0=ok, STALL is acceptable)", rc_proto);

     




    usb_hid_dev_t *d = &g_hid[g_hid_count++];
    memset(d, 0, sizeof(*d));
    d->used      = true;
    d->hcd       = hcd;
    d->addr      = addr;
    d->speed     = speed;
    d->maxp0     = maxp0;
    d->iface_num = iface_num;
    d->protocol  = protocol;

    if (protocol == USB_HID_PROTO_KEYBOARD)
        DBG_VAL("HID", "keyboard attached, addr", addr);
    else
        DBG_VAL("HID", "mouse attached, addr", addr);
}

 





void usb_hid_set_int_ep(uint8_t addr, uint8_t iface, uint8_t ep_addr, uint16_t maxp) {
    for (int i = 0; i < g_hid_count; i++) {
        usb_hid_dev_t *d = &g_hid[i];
        if (!d->used || d->addr != addr || d->iface_num != iface) continue;
        if (d->protocol != USB_HID_PROTO_KEYBOARD || d->int_ep != 0) return;
        d->int_ep    = ep_addr;
        d->int_maxp  = maxp ? maxp : 8;
        d->int_toggle = 0;
        d->use_int   = (d->hcd && d->hcd->ops && d->hcd->ops->int_in_poll) ? true : false;
        dbg_puts("[HID] kbd interrupt-IN ep=0x"); dbg_hex64(ep_addr);
        dbg_puts(" maxp="); dbg_dec64(d->int_maxp);
        dbg_puts(d->use_int ? " -> polling via interrupt-IN\n" : " (HCD has no int_in_poll, GET_REPORT)\n");
        return;
    }
}

 


static void usb_hid_handle_mouse_report(usb_hid_dev_t *d, const uint8_t report[8]) {
    uint8_t buttons = report[0] & 0x07u;
    int8_t  dx = (int8_t)report[1];
    int8_t  dy = (int8_t)report[2];

    d->last_buttons = buttons;

    if (dx != 0 || dy != 0) {
        int32_t x, y;
        ui_get_mouse_pos(&x, &y);
        x += dx;
        y += dy;
        int32_t maxw = (int32_t)fb_width()  - 1;
        int32_t maxh = (int32_t)fb_height() - 1;
        if (x < 0) x = 0; if (y < 0) y = 0;
        if (x > maxw) x = maxw; if (y > maxh) y = maxh;
        ui_set_mouse_pos(x, y);
    }
}

static void usb_hid_handle_kbd_report(usb_hid_dev_t *d, const uint8_t report[8]) {
    uint8_t modifier = report[0];
    bool    shift    = (modifier & 0x22u) != 0;    

     

    bool changed = false;
    for (int k = 0; k < 8; k++) if (report[k] != d->last_report[k]) { changed = true; break; }
    if (changed) {
        static const char hx[] = "0123456789ABCDEF";
        dbg_puts("[HID] kbd report:");
        for (int k = 0; k < 8; k++) {
            dbg_putc(' '); dbg_putc(hx[report[k] >> 4]); dbg_putc(hx[report[k] & 15]);
        }
        dbg_puts("\n");
    }

     


    for (int i = 2; i < 8; i++) {
        uint8_t code = report[i];
        if (code < 4) continue;

        bool was_pressed = false;
        for (int j = 2; j < 8; j++) {
            if (d->last_report[j] == code) { was_pressed = true; break; }
        }
        if (was_pressed) continue;

        if (code == 0x39) { g_caps_lock = !g_caps_lock; continue; }    

        char ch = hid_keycode_to_char(code, shift);
        if (ch != 0) {
            kbd_queue_push(ch);
            g_kbd_keys_total++;
            dbg_puts("[HID] key '");
            if (ch == '\n') dbg_puts("\\n"); else if (ch == '\b') dbg_puts("\\b");
            else if (ch == '\t') dbg_puts("\\t"); else if (ch == 0x7F) dbg_puts("DEL");
            else dbg_putc(ch);
            dbg_puts("'\n");
        } else {
            DBG_VAL("HID", "key without ASCII (ignored), usage", code);
        }
    }

    memcpy(d->last_report, report, 8);
}

 


void usb_hid_poll_all(void) {
    for (int i = 0; i < g_hid_count; i++) {
        usb_hid_dev_t *d = &g_hid[i];
        if (!d->used) continue;

         


        if (d->backoff > 0) { d->backoff--; continue; }

        uint8_t  report[8];
        memset(report, 0, sizeof(report));

         
        if (d->protocol == USB_HID_PROTO_KEYBOARD && d->use_int) {
            for (int burst = 0; burst < 4; burst++) {    
                memset(report, 0, sizeof(report));
                uint32_t want = d->int_maxp < 8 ? d->int_maxp : 8;
                int n = d->hcd->ops->int_in_poll(d->hcd, d->addr, d->speed, d->int_ep,
                                                 d->int_maxp, report, want,
                                                 &d->int_toggle, 1500);
                if (n > 0) {
                    d->int_err = 0;
                    d->int_rx++;
                    d->polls_ok++;
                    if (!d->logged_first) {
                        d->logged_first = true;
                        DBG_VAL("HID", "first keyboard report via interrupt-IN, addr", d->addr);
                    }
                    usb_hid_handle_kbd_report(d, report);
                    continue;                          
                }
                if (n == 0) break;                     
                 
                d->total_fail++;
                if (++d->int_err >= 5) {
                    d->use_int = false;                
                    DBG_MSG("HID", "keyboard interrupt-IN not working -> falling back to GET_REPORT");
                }
                if (d->total_fail <= 3) DBG_RC("HID", "kbd interrupt-IN error rc", n);
                break;
            }
             
            if (d->use_int && d->int_rx != 0) continue;

             









            static bool s_hybrid_logged = false;
            if (!s_hybrid_logged) {
                s_hybrid_logged = true;
                DBG_MSG("HID", "kbd: no interrupt-IN reports yet -> also polling GET_REPORT (hybrid)");
            }
        }

        uint16_t rlen = (d->protocol == USB_HID_PROTO_KEYBOARD) ? 8 : 4;

        usb_setup_pkt_t sp;
        sp.bmRequestType = USB_REQ_DIR_D2H | USB_REQ_TYPE_CLASS | USB_REQ_RECIP_INTERFACE;
        sp.bRequest = 0x01;               
        sp.wValue   = (1u << 8) | 0u;     
        sp.wIndex   = d->iface_num;
        sp.wLength  = rlen;

        int rc = d->hcd->ops->control_transfer(d->hcd, d->addr, d->speed, d->maxp0,
                                               &sp, report, rlen);
        if (rc != 0) {
            d->total_fail++;
            d->fail_count++;
            if (d->total_fail <= 3 || (d->total_fail % 200) == 0) {
                dbg_puts(d->protocol == USB_HID_PROTO_KEYBOARD ? "[HID] kbd" : "[HID] mouse");
                dbg_puts(" GET_REPORT failed, addr="); dbg_dec64(d->addr);
                dbg_puts(" rc="); dbg_putc('-'); dbg_dec64((uint64_t)(-(int64_t)rc));
                dbg_puts(" total_fail="); dbg_dec64(d->total_fail);
                dbg_puts("\n");
            }
            if (d->fail_count >= USB_HID_MAX_CONSECUTIVE_FAILS) {
                d->fail_count = 0;
                d->backoff    = 100;    
            }
            continue;    
        }
        if (!d->logged_first) {
            d->logged_first = true;
            DBG_VAL("HID", "first successful GET_REPORT, addr", d->addr);
        }
        d->polls_ok++;
        d->fail_count = 0;

        if (d->protocol == USB_HID_PROTO_MOUSE)
            usb_hid_handle_mouse_report(d, report);
        else
            usb_hid_handle_kbd_report(d, report);
    }
}

uint8_t usb_hid_mouse_buttons(void) {
    uint8_t b = 0;
    for (int i = 0; i < g_hid_count; i++) {
        if (g_hid[i].used && g_hid[i].protocol == USB_HID_PROTO_MOUSE)
            b |= g_hid[i].last_buttons;
    }
    return b;
}


 



bool usb_hid_get_kbd_stats(uint32_t *ok, uint32_t *fail, uint32_t *keys, uint8_t rep[8], char *mode) {
    for (int i = 0; i < g_hid_count; i++) {
        usb_hid_dev_t *d = &g_hid[i];
        if (d->used && d->protocol == USB_HID_PROTO_KEYBOARD) {
            if (mode) *mode = d->use_int ? (d->int_rx ? 'I' : 'H') : 'G';    
            if (ok)   *ok   = (d->use_int && d->int_rx) ? d->int_rx : d->polls_ok;    
            if (fail) *fail = d->total_fail;
            if (keys) *keys = g_kbd_keys_total;
            if (rep)  for (int k = 0; k < 8; k++) rep[k] = d->last_report[k];
            return true;
        }
    }
    if (g_ps2_present) {                  
        if (mode) *mode = 'P';
        if (ok)   *ok   = g_ps2_rx;        
        if (fail) *fail = 0;
        if (keys) *keys = g_kbd_keys_total;
        if (rep)  { for (int k = 0; k < 8; k++) rep[k] = 0; rep[0] = g_ps2_last; }
        return true;
    }
    return false;
}
